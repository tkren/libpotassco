//
// Copyright (c) 2016, Benjamin Kaufmann
//
// This file is part of Potassco.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <potassco/aspif_text.h>
#include <potassco/string_convert.h>
#include <cctype>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>
namespace Potassco {
AspifTextInput::AspifTextInput(AbstractProgram* out) : out_(out), strStart_(0), strPos_(0) {}
bool AspifTextInput::doAttach(bool& inc) {
	char n = peek(true);
	if (out_ && (!n || std::islower(static_cast<unsigned char>(n)) || std::strchr(".#%{:", n))) {
		while (n == '%') {
			skipLine();
			n = peek(true);
		}
		inc = match("#incremental", false) && require(match("."), "unrecognized directive");
		out_->initProgram(inc);
		return true;
	}
	return false;
}

bool AspifTextInput::doParse() {
	out_->beginStep();
	if (!parseStatements()) { return false; }
	out_->endStep();
	BasicStack().swap(data_);
	return true;
}

bool AspifTextInput::parseStatements() {
	require(out_ != 0, "output not set");
	for (char c; (c = peek(true)) != 0;) {
		data_.clear();
		if      (c == '.') { match("."); }
		else if (c == '#') { if (!matchDirective()) break; }
		else if (c == '%') { skipLine(); }
		else               { matchRule(c); }
	}
	data_.clear();
	return true;
}

void AspifTextInput::matchRule(char c) {
	Head_t ht = Head_t::Disjunctive;
	Body_t bt = Body_t::Normal;
	if (c != ':') {
		if (c == '{') { match("{"); ht = Head_t::Choice; matchAtoms(";,"); match("}"); }
		else          { matchAtoms(";|"); }
	}
	else { data_.push(0u); }
	if (match(":-", false)) {
		c = peek(true);
		if (!StreamType::isDigit(c) && c != '-') {
			matchLits();
		}
		else {
			Weight_t bound = matchInt();
			matchAgg();
			data_.push(bound);
			bt = Body_t::Sum;
		}
	}
	else {
		data_.push(0u);
	}
	match(".");
	if (bt == Body_t::Normal) {
		LitSpan  body = data_.popSpan<Lit_t>(data_.pop<uint32_t>());
		AtomSpan head = data_.popSpan<Atom_t>(data_.pop<uint32_t>());
		out_->rule(ht, head, body);
	}
	else {
		typedef WeightLitSpan WLitSpan;
		Weight_t bound = data_.pop<Weight_t>();
		WLitSpan body = data_.popSpan<WeightLit_t>(data_.pop<uint32_t>());
		AtomSpan head = data_.popSpan<Atom_t>(data_.pop<uint32_t>());
		out_->rule(ht, head, bound, body);
	}
}

bool AspifTextInput::matchDirective() {
	if (match("#minimize", false)) {
		matchAgg();
		Weight_t prio = match("@", false) ? matchInt() : 0;
		match(".");
		out_->minimize(prio, data_.popSpan<WeightLit_t>(data_.pop<uint32_t>()));
	}
	else if (match("#project", false)) {
		uint32_t n = 0;
		if (match("{", false) && !match("}", false)) {
			matchAtoms(",");
			match("}");
			n = data_.pop<uint32_t>();
		}
		match(".");
		out_->project(data_.popSpan<Atom_t>(n));
	}
	else if (match("#output", false)) {
		matchTerm();
		matchCondition();
		match(".");
		LitSpan   cond = data_.popSpan<Lit_t>(data_.pop<uint32_t>());
		StringSpan str = data_.popSpan<char>(data_.pop<uint32_t>());
		out_->output(str, cond);
	}
	else if (match("#external", false)) {
		Atom_t  a = matchId();
		Value_t v = Value_t::False;
		match(".");
		if (match("[", false)) {
			if      (match("true", false))    { v = Value_t::True; }
			else if (match("free", false))    { v = Value_t::Free; }
			else if (match("release", false)) { v = Value_t::Release; }
			else                              { match("false"); }
			match("]");
		}
		out_->external(a, v);
	}
	else if (match("#assume", false)) {
		uint32_t n = 0;
		if (match("{", false) && !match("}", false)) {
			matchLits();
			match("}");
			n = data_.pop<uint32_t>();
		}
		match(".");
		out_->assume(data_.popSpan<Lit_t>(n));
	}
	else if (match("#heuristic", false)) {
		Atom_t a = matchId();
		matchCondition();
		match(".");
		match("[");
		int v = matchInt();
		int p = 0;
		if (match("@", false)) { p = matchInt(); require(p >= 0, "positive priority expected"); }
		match(",");
		int h = -1;
		for (unsigned x = 0; x <= static_cast<unsigned>(Heuristic_t::eMax); ++x) {
			if (match(toString(static_cast<Heuristic_t>(x)), false)) {
				h = static_cast<int>(x);
				break;
			}
		}
		require(h >= 0, "unrecognized heuristic modification");
		skipws();
		match("]");
		out_->heuristic(a, static_cast<Heuristic_t>(h), v, static_cast<unsigned>(p), data_.popSpan<Lit_t>(data_.pop<uint32_t>()));
	}
	else if (match("#edge", false)) {
		int s, t;
		match("("), s = matchInt(), match(","), t = matchInt(), match(")");
		matchCondition();
		match(".");
		out_->acycEdge(s, t, data_.popSpan<Lit_t>(data_.pop<uint32_t>()));
	}
	else if (match("#step", false)) {
		require(incremental(), "#step requires incremental program");
		match(".");
		return false;
	}
	else if (match("#incremental", false)) {
		match(".");
	}
	else {
		require(false, "unrecognized directive");
	}
	return true;
}

void AspifTextInput::skipws() {
	stream()->skipWs();
}
bool AspifTextInput::match(const char* term, bool req) {
	if (ProgramReader::match(term, false)) { skipws(); return true; }
	else if (!req) { return false; }
	else {
		startString();
		push('\'');
		while (*term) { push(*term++); }
		term = "' expected";
		while (*term) { push(*term++); }
		push('\0');
		endString();
		return require(false, data_.popSpan<char>(data_.pop<uint32_t>()).first);
	}
}
void AspifTextInput::matchAtoms(const char* seps) {
	for (uint32_t n = 0;;) {
		data_.push(matchId());
		++n;
		if (!std::strchr(seps, stream()->peek())) {
			data_.push(n);
			break;
		}
		stream()->get();
		skipws();
	}
}
void AspifTextInput::matchLits() {
	uint32_t n = 1;
	do {
		data_.push(matchLit());
	} while (match(",", false) && ++n);
	data_.push(n);
}
void AspifTextInput::matchCondition() {
	if (match(":", false)) { matchLits(); }
	else                   { data_.push(0u); }
}
void AspifTextInput::matchAgg() {
	uint32_t n = 0;
	if (match("{") && !match("}", false)) {
		do {
			WeightLit_t wl = {matchLit(), 1};
			if (match("=", false)) { wl.weight = matchInt(); }
			data_.push(wl);
		}
		while (++n && match(",", false));
		match("}");
	}
	data_.push(n);
}

Lit_t AspifTextInput::matchLit() {
	int s = match("not ", false) ? -1 : 1;
	return static_cast<Lit_t>(matchId()) * s;
}

int AspifTextInput::matchInt() {
	int i = ProgramReader::matchInt();
	skipws();
	return i;
}
Atom_t AspifTextInput::matchId() {
	char c = stream()->get();
	char n = stream()->peek();
	require(std::islower(static_cast<unsigned char>(c)) != 0, "<id> expected");
	require(std::islower(static_cast<unsigned char>(n)) == 0, "<pos-integer> expected");
	if (c == 'x' && (BufferedStream::isDigit(n) || n == '_')) {
		if (n == '_') { stream()->get(); }
		int i = matchInt();
		require(i > 0, "<pos-integer> expected");
		return static_cast<Atom_t>(i);
	}
	else {
		skipws();
		return static_cast<Atom_t>(c - 'a') + 1;
	}
}
void AspifTextInput::startString() {
	strStart_ = strPos_ = data_.top();
}
void AspifTextInput::endString() {
	data_.push(uint32_t(strPos_ - strStart_));
}
void AspifTextInput::push(char c) {
	if (strPos_ == data_.top()) { data_.push(0); }
	*(char*)data_.get(strPos_++) = c;
}

void AspifTextInput::matchTerm() {
	startString();
	char c = stream()->peek();
	if (std::islower(static_cast<unsigned char>(c)) != 0 || c == '_') {
		do { push(stream()->get()); } while (std::isalnum(static_cast<unsigned char>(c = stream()->peek())) != 0 || c == '_');
		skipws();
		if (match("(", false)) {
			push('(');
			for (;;) {
				matchAtomArg();
				if (!match(",", false)) break;
				push(',');
			}
			match(")");
			push(')');
		}
	}
	else if (c == '"') { matchStr(); }
	else { require(false, "<term> expected"); }
	skipws();
	endString();
}
void AspifTextInput::matchAtomArg() {
	char c;
	for (int p = 0; (c = stream()->peek()) != 0; ) {
		if (c == '"') {
			matchStr();
		}
		else {
			if      (c == ')' && --p < 0) { break; }
			else if (c == ',' &&  p == 0) { break; }
			p += int(c == '(');
			push(stream()->get());
			skipws();
		}
	}
}
void AspifTextInput::matchStr() {
	match("\""), push('"');
	bool quoted = false;
	for (char c; (c = stream()->peek()) != 0 && (c != '\"' || quoted);) {
		quoted = !quoted && c == '\\';
		push(stream()->get());
	}
	match("\""), push('"');
}
/////////////////////////////////////////////////////////////////////////////////////////
// AspifTextOutput
/////////////////////////////////////////////////////////////////////////////////////////
struct AspifTextOutput::Extra {
	typedef std::vector<std::string> StringVec;
	typedef std::vector<Id_t> AtomMap;
	StringVec strings;
	AtomMap   atoms; // maps into strings
	void reset() { strings.clear(); atoms.clear(); }
};
AspifTextOutput::AspifTextOutput(std::ostream& os) : os_(os), step_(-1) {
	extra_ = new Extra();
}
AspifTextOutput::~AspifTextOutput() {
	delete extra_;
}
Id_t AspifTextOutput::addString(const StringSpan& str) {
	Id_t id = static_cast<Id_t>(extra_->strings.size());
	extra_->strings.push_back(std::string(Potassco::begin(str), Potassco::end(str)));
	return id;
}
void AspifTextOutput::addAtom(Atom_t id, const StringSpan& str) {
	if (id >= extra_->atoms.size()) { extra_->atoms.resize(id + 1, idMax); }
	extra_->atoms[id] = addString(str);
}
std::ostream& AspifTextOutput::printName(std::ostream& os, Lit_t lit) const {
	if (lit < 0) { os << "not "; }
	Atom_t id = Potassco::atom(lit);
	if (id < extra_->atoms.size() && extra_->atoms[id] < extra_->strings.size()) {
		os << extra_->strings[extra_->atoms[id]];
	}
	else {
		os << "x_" << id;
	}
	return os;
}
void AspifTextOutput::initProgram(bool incremental) {
	step_ = incremental ? 0 : -1;
	extra_->reset();
}
void AspifTextOutput::beginStep() {
	if (step_ >= 0) {
		if (step_) { os_ << "% #program step(" << step_ << ").\n"; theory_.update(); }
		else       { os_ << "% #program base.\n"; }
		++step_;
	}
}
void AspifTextOutput::rule(Head_t ht, const AtomSpan& head, const LitSpan& body) {
	push(Directive_t::Rule).push(ht).pushSpan(head).push(Body_t::Normal).pushSpan(body);
}
void AspifTextOutput::rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& lits) {
	if (size(lits) == 0) {
		AspifTextOutput::rule(ht, head, toSpan<Lit_t>());
	}
	push(Directive_t::Rule).push(ht).pushSpan(head);
	uint32_t top = directives_.top();
	Weight_t min = weight(*begin(lits)), max = min;
	push(static_cast<uint32_t>(Body_t::Sum)).push(bound).push(static_cast<uint32_t>(size(lits)));
	for (const WeightLit_t* it = begin(lits), *end = Potassco::end(lits); it != end; ++it) {
		push(Potassco::lit(*it)).push(Potassco::weight(*it));
		if (Potassco::weight(*it) < min) { min = Potassco::weight(*it); }
		if (Potassco::weight(*it) > max) { max = Potassco::weight(*it); }
	}
	if (min == max) {
		directives_.setTop(top);
		bound = (bound + min-1)/min;
		push(static_cast<uint32_t>(Body_t::Count)).push(bound).push(static_cast<uint32_t>(size(lits)));
		for (const WeightLit_t* it = begin(lits), *end = Potassco::end(lits); it != end; ++it) {
			push(Potassco::lit(*it));
		}
	}
}
void AspifTextOutput::minimize(Weight_t prio, const WeightLitSpan& lits) {
	push(Directive_t::Minimize).pushSpan(lits).push(prio);
}
void AspifTextOutput::output(const StringSpan& str, const LitSpan& cond) {
	bool isAtom = size(str) > 0 && (std::islower(static_cast<unsigned char>(*begin(str))) || *begin(str) == '_');
	if (size(cond) == 1 && lit(*begin(cond)) > 0 && isAtom) {
		addAtom(Potassco::atom(*begin(cond)), str);
	}
	else {
		push(Directive_t::Output).push(addString(str)).pushSpan(cond);
	}
}
void AspifTextOutput::external(Atom_t a, Value_t v) {
	push(Directive_t::External).push(a).push(v);
}
void AspifTextOutput::assume(const LitSpan& lits) {
	push(Directive_t::Assume).pushSpan(lits);
}
void AspifTextOutput::project(const AtomSpan& atoms) {
	push(Directive_t::Project).pushSpan(atoms);
}
void AspifTextOutput::acycEdge(int s, int t, const LitSpan& condition) {
	push(Directive_t::Edge).push(s).push(t).pushSpan(condition);
}
void AspifTextOutput::heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& condition) {
	push(Directive_t::Heuristic).push(a).pushSpan(condition).push(bias).push(prio).push(t);
}
void AspifTextOutput::theoryTerm(Id_t termId, int number) {
	theory_.addTerm(termId, number);
}
void AspifTextOutput::theoryTerm(Id_t termId, const StringSpan& name) {
	theory_.addTerm(termId, name);
}
void AspifTextOutput::theoryTerm(Id_t termId, int compound, const IdSpan& args) {
	theory_.addTerm(termId, compound, args);
}
void AspifTextOutput::theoryElement(Id_t id, const IdSpan& terms, const LitSpan& cond) {
	if (size(cond) != 0) { throw std::logic_error("TODO - theory conditions not yet supported!"); }
	theory_.addElement(id, terms, 0);
}
void AspifTextOutput::theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements) {
	theory_.addAtom(atomOrZero, termId, elements);
}
void AspifTextOutput::theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs) {
	theory_.addAtom(atomOrZero, termId, elements, op, rhs);
}
void AspifTextOutput::writeDirectives() {
	const char* sep = 0, *term = 0;
	front_ = 0;
	for (Directive_t x; (x = pop<Directive_t>()) != Directive_t::End;) {
		switch (x) {
			case Directive_t::Rule:
				sep = term = "";
				if (pop<uint32_t>() != 0) { os_ << "{"; term = "}"; }
				for (uint32_t n = pop<uint32_t>(); n--; sep = !*term ? "|" : ";") { printName(os_ << sep, pop<Atom_t>()); }
				os_ << term; sep = " :- ";
				switch (uint32_t x = pop<uint32_t>()) {
					case Body_t::Normal:
						for (uint32_t n = pop<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, pop<Lit_t>()); }
						break;
					case Body_t::Count: // fall through
					case Body_t::Sum:
						os_ << sep << pop<Weight_t>();
						sep = "{";
						for (uint32_t n = pop<uint32_t>(); n--; sep = "; ") {
							printName(os_ << sep, pop<Lit_t>());
							if (x == Body_t::Sum) { os_ << "=" << pop<Weight_t>(); }
						}
						os_ << "}";
						break;
				}
				os_ << ".";
				break;
			case Directive_t::Minimize:
				sep = "#minimize{";
				for (uint32_t n = pop<uint32_t>(); n--; sep = "; ") {
					WeightLit_t lit = pop<WeightLit_t>();
					printName(os_ << sep, Potassco::lit(lit));
					os_ << "=" << Potassco::weight(lit);
				}
				os_ << "}@" << pop<Weight_t>() << ".";
				break;
			case Directive_t::Project:
				sep = "#project{";
				for (uint32_t n = pop<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, pop<Lit_t>()); }
				os_ << "}.";
				break;
			case Directive_t::Output:
				os_ << "#show " << extra_->strings[pop<uint32_t>()];
				sep = " : ";
				for (uint32_t n = pop<uint32_t>(); n--; sep = ", ") {
					printName(os_ << sep, pop<Lit_t>());
				}
				os_ << ".";
				break;
			case Directive_t::External:
				sep = "#external ";
				printName(os_ << sep, pop<Atom_t>()) << ".";
				switch (pop<uint32_t>()) {
					default: break;
					case Value_t::Free:    os_ << " [free]"; break;
					case Value_t::True:    os_ << " [true]"; break;
					case Value_t::Release: os_ << " [release]"; break;
				}
				break;
			case Directive_t::Assume:
				sep = "#assume{";
				for (uint32_t n = pop<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, pop<Lit_t>()); }
				os_ << "}.";
				break;
			case Directive_t::Heuristic:
				os_ << "#heuristic ";
				printName(os_, pop<Atom_t>());
				sep = " : ";
				for (uint32_t n = pop<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, pop<Lit_t>()); }
				os_ << ". [" << pop<int32_t>();
				if (uint32_t p = pop<uint32_t>()) { os_ << "@" << p; }
				os_ << ", " << toString(static_cast<Heuristic_t>(pop<uint32_t>())) << "]";
				break;
			case Directive_t::Edge:
				os_ << "#edge(" << pop<int32_t>() << ",";
				os_ << pop<int32_t>() << ")";
				sep = " : ";
				for (uint32_t n = pop<uint32_t>(); n--; sep = ", ") { printName(os_ << sep, pop<Lit_t>()); }
				os_ << ".";
				break;
			default: break;
		}
		os_ << "\n";
	}
}
void AspifTextOutput::writeTheories() {
	struct BuildStr : public Potassco::TheoryData::Visitor {
		BuildStr(std::ostream& o) : out(o) {}
		virtual void visit(const Potassco::TheoryData& data, const Potassco::TheoryAtom& a) {
			std::string rhs, op;
			data.accept(a, *this);
			if (a.guard()) { rhs = popStack(1 + (a.rhs() != 0), " "); }
			if (a.size())  { op  = popStack(a.size(), "; "); }
			out << "&" << popStack(1, "") << "{" << op << "}";
			if (!rhs.empty()) { out << " " << rhs; }
			out << ".\n";
		}
		virtual void visit(const Potassco::TheoryData& data, Potassco::Id_t, const Potassco::TheoryElement& e) {
			data.accept(e, *this);
			if (e.size() > 1) { exp.push_back(popStack(e.size(), ", ")); }
		}
		virtual void visit(const Potassco::TheoryData& data, Potassco::Id_t, const Potassco::TheoryTerm& t) {
			switch (t.type()) {
				case Potassco::Theory_t::Number: exp.push_back(Potassco::toString(t.number())); break;
				case Potassco::Theory_t::Symbol: exp.push_back(t.symbol()); break;
				case Potassco::Theory_t::Compound: {
					data.accept(t, *this);
					const char* parens = Potassco::toString(t.isTuple() ? t.tuple() : Potassco::Tuple_t::Paren);
					std::string op = popStack((uint32_t)t.isFunction(), "");
					if (t.isFunction() && op.size() <= 2 && t.size() == 2 && std::strstr("<=>=+-*/", op.c_str()) != 0) {
						std::swap(exp.back(), op);
						exp.push_back(op);
						exp.push_back(popStack(3, " "));
					}
					else {
						op.append(1, parens[0]).append(popStack(t.size(), ", ")).append(1, parens[1]);
						exp.push_back(op);
					}
				}
			}
		}
		std::string popStack(uint32_t n, const char* delim) {
			assert(n <= exp.size());
			std::string op;
			for (std::vector<std::string>::size_type i = exp.size() - n; i != exp.size(); ++i) {
				if (!op.empty()) { op += delim; }
				op += exp[i];
			}
			exp.erase(exp.end() - n, exp.end());
			return op;
		}
		std::vector<std::string> exp;
		std::ostream& out;
	} toStr(os_);
	theory_.accept(toStr);
}
void AspifTextOutput::endStep() {
	directives_.push(Directive_t::End);
	writeDirectives();
	directives_.clear();
	writeTheories();
	if (step_ < 0) { theory_.reset(); }
}

}
