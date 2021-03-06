

// This file was automatically generated by Coco/R; don't modify it.
#include "LuaParser.h"
#include "LjasErrors.h"
#include <QtDebug>
#include <QFileInfo>

namespace Lua {

using namespace Ljas;

static QString coco_string_create( const wchar_t* str )
{
    return QString::fromStdWString(str);
}

int Parser::peek( quint8 la )
{
	if( la == 0 )
		return d_cur.d_type;
	else if( la == 1 )
		return d_next.d_type;
	else
    {
    	// in case Lexer::setIgnoreComments is false we still don't want 
    	// to see them in prefix logic 
        int t = scanner->peekToken( la - 1 ).d_type;
        while( t == Tok_Comment )
            t = scanner->peekToken( ++la - 1 ).d_type;
        return t;
    }
}


void Parser::SynErr(int n, const char* ctx) {
    if (errDist >= minErrDist)
    {
       SynErr(d_next.d_sourcePath,d_next.d_lineNr, d_next.d_colNr, n, errors, ctx);
    }
	errDist = 0;
}

void Parser::SemErr(const char* msg) {
	if (errDist >= minErrDist) errors->error(Errors::Semantics,d_cur.d_sourcePath,d_cur.d_lineNr, d_cur.d_colNr, msg);
	errDist = 0;
}

void Parser::Get() {
	for (;;) {
		d_cur = d_next;
		d_next = scanner->nextToken();
        bool deliverToParser = false;
        switch( d_next.d_type )
        {
        case Tok_Invalid:
        	if( !d_next.d_val.isEmpty() )
            	SynErr( d_next.d_type, d_next.d_val );
            // else errors already handeled in lexer
            break;
        case Tok_Comment:
            d_comments.append(d_next);
            break;
        default:
            deliverToParser = true;
            break;
        }

        if( deliverToParser )
        {
            if( d_next.d_type == Tok_Eof )
                d_next.d_type = _EOF;

            la->kind = d_next.d_type;
            if (la->kind <= maxT)
            {
                ++errDist;
                break;
            }
        }

		d_next = d_cur;
	}
}

void Parser::Expect(int n, const char* ctx ) {
	if (la->kind==n) Get(); else { SynErr(n, ctx); }
}

void Parser::ExpectWeak(int n, int follow) {
	if (la->kind == n) Get();
	else {
		SynErr(n);
		while (!StartOf(follow)) Get();
	}
}

bool Parser::WeakSeparator(int n, int syFol, int repFol) {
	if (la->kind == n) {Get(); return true;}
	else if (StartOf(repFol)) {return false;}
	else {
		SynErr(n);
		while (!(StartOf(syFol) || StartOf(repFol) || StartOf(0))) {
			Get();
		}
		return StartOf(syFol);
	}
}

void Parser::Lua() {
		d_stack.push(&d_root); 
		chunk();
		d_stack.pop(); 
}

void Parser::chunk() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_chunk, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		while (StartOf(1)) {
			stat();
			if (la->kind == _T_Semi) {
				Get();
				addTerminal(); 
			}
		}
		if (la->kind == _T_break || la->kind == _T_return) {
			laststat();
			if (la->kind == _T_Semi) {
				Get();
				addTerminal(); 
			}
		}
		d_stack.pop(); 
}

void Parser::stat() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_stat, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		switch (la->kind) {
		case _T_Lpar: case _T_Name: {
			assigOrCall_();
			break;
		}
		case _T_do: {
			dostat_();
			break;
		}
		case _T_while: {
			whilestat_();
			break;
		}
		case _T_repeat: {
			repeatstat_();
			break;
		}
		case _T_if: {
			ifstat_();
			break;
		}
		case _T_for: {
			forstat_();
			break;
		}
		case _T_function: {
			gfuncdecl_();
			break;
		}
		case _T_local: {
			localdecl_();
			break;
		}
		default: SynErr(62,__FUNCTION__); break;
		}
		d_stack.pop(); 
}

void Parser::laststat() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_laststat, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		if (la->kind == _T_return) {
			Get();
			addTerminal(); 
			if (StartOf(2)) {
				explist();
			}
		} else if (la->kind == _T_break) {
			Get();
			addTerminal(); 
		} else SynErr(63,__FUNCTION__);
		d_stack.pop(); 
}

void Parser::block() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_block, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		chunk();
		d_stack.pop(); 
}

void Parser::assigOrCall_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_assigOrCall_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		prefixexp();
		if (la->kind == _T_Comma || la->kind == _T_Eq) {
			assignment_();
		}
		d_stack.pop(); 
}

void Parser::dostat_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_dostat_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_do,__FUNCTION__);
		addTerminal(); 
		block();
		Expect(_T_end,__FUNCTION__);
		addTerminal(); 
		d_stack.pop(); 
}

void Parser::whilestat_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_whilestat_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_while,__FUNCTION__);
		addTerminal(); 
		exp();
		Expect(_T_do,__FUNCTION__);
		addTerminal(); 
		block();
		Expect(_T_end,__FUNCTION__);
		addTerminal(); 
		d_stack.pop(); 
}

void Parser::repeatstat_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_repeatstat_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_repeat,__FUNCTION__);
		addTerminal(); 
		block();
		Expect(_T_until,__FUNCTION__);
		addTerminal(); 
		exp();
		d_stack.pop(); 
}

void Parser::ifstat_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_ifstat_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_if,__FUNCTION__);
		addTerminal(); 
		exp();
		Expect(_T_then,__FUNCTION__);
		addTerminal(); 
		block();
		while (la->kind == _T_elseif) {
			Get();
			addTerminal(); 
			exp();
			Expect(_T_then,__FUNCTION__);
			addTerminal(); 
			block();
		}
		if (la->kind == _T_else) {
			Get();
			addTerminal(); 
			block();
		}
		Expect(_T_end,__FUNCTION__);
		addTerminal(); 
		d_stack.pop(); 
}

void Parser::forstat_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_forstat_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_for,__FUNCTION__);
		addTerminal(); 
		Expect(_T_Name,__FUNCTION__);
		addTerminal(); 
		if (la->kind == _T_Eq) {
			Get();
			addTerminal(); 
			exp();
			Expect(_T_Comma,__FUNCTION__);
			addTerminal(); 
			exp();
			if (la->kind == _T_Comma) {
				Get();
				addTerminal(); 
				exp();
			}
			Expect(_T_do,__FUNCTION__);
			addTerminal(); 
			block();
			Expect(_T_end,__FUNCTION__);
			addTerminal(); 
		} else if (la->kind == _T_Comma || la->kind == _T_in) {
			while (la->kind == _T_Comma) {
				Get();
				addTerminal(); 
				Expect(_T_Name,__FUNCTION__);
				addTerminal(); 
			}
			Expect(_T_in,__FUNCTION__);
			addTerminal(); 
			explist();
			Expect(_T_do,__FUNCTION__);
			addTerminal(); 
			block();
			Expect(_T_end,__FUNCTION__);
			addTerminal(); 
		} else SynErr(64,__FUNCTION__);
		d_stack.pop(); 
}

void Parser::gfuncdecl_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_gfuncdecl_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_function,__FUNCTION__);
		addTerminal(); 
		funcname();
		funcbody();
		d_stack.pop(); 
}

void Parser::localdecl_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_localdecl_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_local,__FUNCTION__);
		addTerminal(); 
		if (la->kind == _T_function) {
			lfuncdecl_();
		} else if (la->kind == _T_Name) {
			lvardecl_();
		} else SynErr(65,__FUNCTION__);
		d_stack.pop(); 
}

void Parser::exp() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_exp, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		switch (la->kind) {
		case _T_nil: {
			Get();
			addTerminal(); 
			exp_nlr_();
			break;
		}
		case _T_false: {
			Get();
			addTerminal(); 
			exp_nlr_();
			break;
		}
		case _T_true: {
			Get();
			addTerminal(); 
			exp_nlr_();
			break;
		}
		case _T_Number: {
			Get();
			addTerminal(); 
			exp_nlr_();
			break;
		}
		case _T_String: {
			Get();
			addTerminal(); 
			exp_nlr_();
			break;
		}
		case _T_3Dot: {
			Get();
			addTerminal(); 
			exp_nlr_();
			break;
		}
		case _T_function: {
			lambdecl_();
			exp_nlr_();
			break;
		}
		case _T_Lpar: case _T_Name: {
			prefixexp();
			exp_nlr_();
			break;
		}
		case _T_Lbrace: {
			tableconstructor();
			exp_nlr_();
			break;
		}
		case _T_Hash: case _T_Minus: case _T_not: {
			unop();
			exp();
			exp_nlr_();
			break;
		}
		default: SynErr(66,__FUNCTION__); break;
		}
		d_stack.pop(); 
}

void Parser::explist() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_explist, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		exp();
		while (la->kind == _T_Comma) {
			Get();
			addTerminal(); 
			exp();
		}
		d_stack.pop(); 
}

void Parser::funcname() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_funcname, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_Name,__FUNCTION__);
		addTerminal(); 
		while (la->kind == _T_Dot) {
			desig_();
		}
		if (la->kind == _T_Colon) {
			Get();
			addTerminal(); 
			Expect(_T_Name,__FUNCTION__);
			addTerminal(); 
		}
		d_stack.pop(); 
}

void Parser::funcbody() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_funcbody, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_Lpar,__FUNCTION__);
		addTerminal(); 
		if (la->kind == _T_3Dot || la->kind == _T_Name) {
			parlist();
		}
		Expect(_T_Rpar,__FUNCTION__);
		addTerminal(); 
		block();
		Expect(_T_end,__FUNCTION__);
		addTerminal(); 
		d_stack.pop(); 
}

void Parser::lfuncdecl_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_lfuncdecl_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_function,__FUNCTION__);
		addTerminal(); 
		Expect(_T_Name,__FUNCTION__);
		addTerminal(); 
		funcbody();
		d_stack.pop(); 
}

void Parser::lvardecl_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_lvardecl_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		namelist();
		if (la->kind == _T_Eq) {
			Get();
			addTerminal(); 
			explist();
		}
		d_stack.pop(); 
}

void Parser::namelist() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_namelist, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_Name,__FUNCTION__);
		addTerminal(); 
		while (peek(1) == _T_Comma && peek(2) == _T_Name ) {
			Expect(_T_Comma,__FUNCTION__);
			addTerminal(); 
			Expect(_T_Name,__FUNCTION__);
			addTerminal(); 
		}
		d_stack.pop(); 
}

void Parser::prefixexp() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_prefixexp, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		if (la->kind == _T_Name) {
			Get();
			addTerminal(); 
		} else if (la->kind == _T_Lpar) {
			Get();
			addTerminal(); 
			exp();
			Expect(_T_Rpar,__FUNCTION__);
			addTerminal(); 
		} else SynErr(67,__FUNCTION__);
		while (StartOf(3)) {
			if (la->kind == _T_Lbrack) {
				index_();
			} else if (la->kind == _T_Dot) {
				desig_();
			} else {
				call_();
			}
		}
		d_stack.pop(); 
}

void Parser::assignment_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_assignment_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		while (la->kind == _T_Comma) {
			Get();
			addTerminal(); 
			prefixexp();
		}
		Expect(_T_Eq,__FUNCTION__);
		addTerminal(); 
		explist();
		d_stack.pop(); 
}

void Parser::call_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_call_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		if (la->kind == _T_Colon) {
			Get();
			addTerminal(); 
			Expect(_T_Name,__FUNCTION__);
			addTerminal(); 
		}
		args();
		d_stack.pop(); 
}

void Parser::args() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_args, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		if (la->kind == _T_Lpar) {
			Get();
			addTerminal(); 
			if (StartOf(2)) {
				explist();
			}
			Expect(_T_Rpar,__FUNCTION__);
			addTerminal(); 
		} else if (la->kind == _T_Lbrace) {
			tableconstructor();
		} else if (la->kind == _T_String) {
			Get();
			addTerminal(); 
		} else SynErr(68,__FUNCTION__);
		d_stack.pop(); 
}

void Parser::desig_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_desig_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_Dot,__FUNCTION__);
		addTerminal(); 
		Expect(_T_Name,__FUNCTION__);
		addTerminal(); 
		d_stack.pop(); 
}

void Parser::exp_nlr_() {
		if (StartOf(4)) {
			binop();
			exp();
			exp_nlr_();
		}
}

void Parser::lambdecl_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_lambdecl_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_function,__FUNCTION__);
		addTerminal(); 
		funcbody();
		d_stack.pop(); 
}

void Parser::tableconstructor() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_tableconstructor, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_Lbrace,__FUNCTION__);
		addTerminal(); 
		if (StartOf(5)) {
			fieldlist();
		}
		Expect(_T_Rbrace,__FUNCTION__);
		addTerminal(); 
		d_stack.pop(); 
}

void Parser::unop() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_unop, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		if (la->kind == _T_Minus) {
			Get();
			addTerminal(); 
		} else if (la->kind == _T_not) {
			Get();
			addTerminal(); 
		} else if (la->kind == _T_Hash) {
			Get();
			addTerminal(); 
		} else SynErr(69,__FUNCTION__);
		d_stack.pop(); 
}

void Parser::binop() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_binop, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		switch (la->kind) {
		case _T_Plus: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Minus: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Star: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Slash: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Hat: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Percent: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_2Dot: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Lt: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Leq: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Gt: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_Geq: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_2Eq: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_TildeEq: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_and: {
			Get();
			addTerminal(); 
			break;
		}
		case _T_or: {
			Get();
			addTerminal(); 
			break;
		}
		default: SynErr(70,__FUNCTION__); break;
		}
		d_stack.pop(); 
}

void Parser::index_() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_index_, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		Expect(_T_Lbrack,__FUNCTION__);
		addTerminal(); 
		exp();
		Expect(_T_Rbrack,__FUNCTION__);
		addTerminal(); 
		d_stack.pop(); 
}

void Parser::parlist() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_parlist, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		if (la->kind == _T_Name) {
			namelist();
			if (la->kind == _T_Comma) {
				Get();
				addTerminal(); 
				Expect(_T_3Dot,__FUNCTION__);
				addTerminal(); 
			}
		} else if (la->kind == _T_3Dot) {
			Get();
			addTerminal(); 
		} else SynErr(71,__FUNCTION__);
		d_stack.pop(); 
}

void Parser::fieldlist() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_fieldlist, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		field();
		while (( peek(1) == _T_Comma || peek(1) == _T_Semi ) && !( peek(2) == _T_Rbrace ) ) {
			fieldsep();
			field();
		}
		if (la->kind == _T_Comma || la->kind == _T_Semi) {
			fieldsep();
		}
		d_stack.pop(); 
}

void Parser::field() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_field, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		if (la->kind == _T_Lbrack) {
			index_();
			Expect(_T_Eq,__FUNCTION__);
			addTerminal(); 
			exp();
		} else if (peek(1) == _T_Name && peek(2) == _T_Eq ) {
			Expect(_T_Name,__FUNCTION__);
			addTerminal(); 
			Expect(_T_Eq,__FUNCTION__);
			addTerminal(); 
			exp();
		} else if (StartOf(2)) {
			exp();
		} else SynErr(72,__FUNCTION__);
		d_stack.pop(); 
}

void Parser::fieldsep() {
		Lua::SynTree* n = new Lua::SynTree( Lua::SynTree::R_fieldsep, d_next ); d_stack.top()->d_children.append(n); d_stack.push(n); 
		if (la->kind == _T_Comma) {
			Get();
			addTerminal(); 
		} else if (la->kind == _T_Semi) {
			Get();
			addTerminal(); 
		} else SynErr(73,__FUNCTION__);
		d_stack.pop(); 
}




// If the user declared a method Init and a mehtod Destroy they should
// be called in the contructur and the destructor respctively.
//
// The following templates are used to recognize if the user declared
// the methods Init and Destroy.

template<typename T>
struct ParserInitExistsRecognizer {
	template<typename U, void (U::*)() = &U::Init>
	struct ExistsIfInitIsDefinedMarker{};

	struct InitIsMissingType {
		char dummy1;
	};
	
	struct InitExistsType {
		char dummy1; char dummy2;
	};

	// exists always
	template<typename U>
	static InitIsMissingType is_here(...);

	// exist only if ExistsIfInitIsDefinedMarker is defined
	template<typename U>
	static InitExistsType is_here(ExistsIfInitIsDefinedMarker<U>*);

	enum { InitExists = (sizeof(is_here<T>(NULL)) == sizeof(InitExistsType)) };
};

template<typename T>
struct ParserDestroyExistsRecognizer {
	template<typename U, void (U::*)() = &U::Destroy>
	struct ExistsIfDestroyIsDefinedMarker{};

	struct DestroyIsMissingType {
		char dummy1;
	};
	
	struct DestroyExistsType {
		char dummy1; char dummy2;
	};

	// exists always
	template<typename U>
	static DestroyIsMissingType is_here(...);

	// exist only if ExistsIfDestroyIsDefinedMarker is defined
	template<typename U>
	static DestroyExistsType is_here(ExistsIfDestroyIsDefinedMarker<U>*);

	enum { DestroyExists = (sizeof(is_here<T>(NULL)) == sizeof(DestroyExistsType)) };
};

// The folloing templates are used to call the Init and Destroy methods if they exist.

// Generic case of the ParserInitCaller, gets used if the Init method is missing
template<typename T, bool = ParserInitExistsRecognizer<T>::InitExists>
struct ParserInitCaller {
	static void CallInit(T *t) {
		// nothing to do
	}
};

// True case of the ParserInitCaller, gets used if the Init method exists
template<typename T>
struct ParserInitCaller<T, true> {
	static void CallInit(T *t) {
		t->Init();
	}
};

// Generic case of the ParserDestroyCaller, gets used if the Destroy method is missing
template<typename T, bool = ParserDestroyExistsRecognizer<T>::DestroyExists>
struct ParserDestroyCaller {
	static void CallDestroy(T *t) {
		// nothing to do
	}
};

// True case of the ParserDestroyCaller, gets used if the Destroy method exists
template<typename T>
struct ParserDestroyCaller<T, true> {
	static void CallDestroy(T *t) {
		t->Destroy();
	}
};

void Parser::Parse() {
	d_cur = Token();
	d_next = Token();
	Get();
	Lua();
	Expect(0,__FUNCTION__);
}

Parser::Parser(Lexer *scanner, Errors* err) {
	maxT = 61;

	ParserInitCaller<Parser>::CallInit(this);
	la = &d_dummy;
	minErrDist = 2;
	errDist = minErrDist;
	this->scanner = scanner;
	errors = err;
}

bool Parser::StartOf(int s) {
	const bool T = true;
	const bool x = false;

	static bool set[6][63] = {
		{T,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x},
		{x,x,x,x, T,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,T,x, x,x,x,T, T,T,x,T, x,x,x,T, x,x,x,x, T,x,T,x, x,x,x,x, x,x,x},
		{x,x,T,x, T,x,x,x, x,T,x,x, x,x,T,x, x,x,x,x, x,x,x,x, x,x,x,x, T,x,x,x, x,x,x,x, x,x,T,x, T,x,x,x, T,T,x,x, x,x,T,x, x,x,T,T, T,x,x,x, x,x,x},
		{x,x,x,x, T,x,x,x, x,x,x,x, T,x,x,x, T,x,x,x, x,x,x,x, T,x,x,x, T,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x,x, T,x,x,x, x,x,x},
		{x,x,x,T, x,x,T,T, x,T,x,x, x,T,x,T, x,x,T,T, x,T,T,T, x,x,x,T, x,x,T,x, T,x,x,x, x,x,x,x, x,x,x,x, x,x,T,x, x,x,x,x, x,x,x,x, x,x,x,x, x,x,x},
		{x,x,T,x, T,x,x,x, x,T,x,x, x,x,T,x, x,x,x,x, x,x,x,x, T,x,x,x, T,x,x,x, x,x,x,x, x,x,T,x, T,x,x,x, T,T,x,x, x,x,T,x, x,x,T,T, T,x,x,x, x,x,x}
	};



	return set[s][la->kind];
}

Parser::~Parser() {
	ParserDestroyCaller<Parser>::CallDestroy(this);
}

void Parser::SynErr(const QString& sourcePath, int line, int col, int n, Errors* err, const char* ctx, const QString& str ) {
	QString s;
	QString ctxStr;
	if( ctx )
		ctxStr = QString( " in %1" ).arg(ctx);
    if( n == 0 )
        s = QString("EOF expected%1").arg(ctxStr);
    else if( n < TT_Specials )
        s = QString("'%2' expected%1").arg(ctxStr).arg(tokenTypeString(n));
    else if( n <= TT_Max )
        s = QString("%2 expected%1").arg(ctxStr).arg(tokenTypeString(n));
    else
	switch (n) {
			case 0: s = coco_string_create(L"EOF expected"); break;
			case 1: s = coco_string_create(L"T_Literals_ expected"); break;
			case 2: s = coco_string_create(L"T_Hash expected"); break;
			case 3: s = coco_string_create(L"T_Percent expected"); break;
			case 4: s = coco_string_create(L"T_Lpar expected"); break;
			case 5: s = coco_string_create(L"T_Rpar expected"); break;
			case 6: s = coco_string_create(L"T_Star expected"); break;
			case 7: s = coco_string_create(L"T_Plus expected"); break;
			case 8: s = coco_string_create(L"T_Comma expected"); break;
			case 9: s = coco_string_create(L"T_Minus expected"); break;
			case 10: s = coco_string_create(L"T_2Minus expected"); break;
			case 11: s = coco_string_create(L"T_2MinusLbrack expected"); break;
			case 12: s = coco_string_create(L"T_Dot expected"); break;
			case 13: s = coco_string_create(L"T_2Dot expected"); break;
			case 14: s = coco_string_create(L"T_3Dot expected"); break;
			case 15: s = coco_string_create(L"T_Slash expected"); break;
			case 16: s = coco_string_create(L"T_Colon expected"); break;
			case 17: s = coco_string_create(L"T_Semi expected"); break;
			case 18: s = coco_string_create(L"T_Lt expected"); break;
			case 19: s = coco_string_create(L"T_Leq expected"); break;
			case 20: s = coco_string_create(L"T_Eq expected"); break;
			case 21: s = coco_string_create(L"T_2Eq expected"); break;
			case 22: s = coco_string_create(L"T_Gt expected"); break;
			case 23: s = coco_string_create(L"T_Geq expected"); break;
			case 24: s = coco_string_create(L"T_Lbrack expected"); break;
			case 25: s = coco_string_create(L"T_Rbrack expected"); break;
			case 26: s = coco_string_create(L"T_Rbrack2Minus expected"); break;
			case 27: s = coco_string_create(L"T_Hat expected"); break;
			case 28: s = coco_string_create(L"T_Lbrace expected"); break;
			case 29: s = coco_string_create(L"T_Rbrace expected"); break;
			case 30: s = coco_string_create(L"T_TildeEq expected"); break;
			case 31: s = coco_string_create(L"T_Keywords_ expected"); break;
			case 32: s = coco_string_create(L"T_and expected"); break;
			case 33: s = coco_string_create(L"T_break expected"); break;
			case 34: s = coco_string_create(L"T_do expected"); break;
			case 35: s = coco_string_create(L"T_else expected"); break;
			case 36: s = coco_string_create(L"T_elseif expected"); break;
			case 37: s = coco_string_create(L"T_end expected"); break;
			case 38: s = coco_string_create(L"T_false expected"); break;
			case 39: s = coco_string_create(L"T_for expected"); break;
			case 40: s = coco_string_create(L"T_function expected"); break;
			case 41: s = coco_string_create(L"T_if expected"); break;
			case 42: s = coco_string_create(L"T_in expected"); break;
			case 43: s = coco_string_create(L"T_local expected"); break;
			case 44: s = coco_string_create(L"T_nil expected"); break;
			case 45: s = coco_string_create(L"T_not expected"); break;
			case 46: s = coco_string_create(L"T_or expected"); break;
			case 47: s = coco_string_create(L"T_repeat expected"); break;
			case 48: s = coco_string_create(L"T_return expected"); break;
			case 49: s = coco_string_create(L"T_then expected"); break;
			case 50: s = coco_string_create(L"T_true expected"); break;
			case 51: s = coco_string_create(L"T_until expected"); break;
			case 52: s = coco_string_create(L"T_while expected"); break;
			case 53: s = coco_string_create(L"T_Specials_ expected"); break;
			case 54: s = coco_string_create(L"T_Name expected"); break;
			case 55: s = coco_string_create(L"T_Number expected"); break;
			case 56: s = coco_string_create(L"T_String expected"); break;
			case 57: s = coco_string_create(L"T_Comment expected"); break;
			case 58: s = coco_string_create(L"T_Designator expected"); break;
			case 59: s = coco_string_create(L"T_Eof expected"); break;
			case 60: s = coco_string_create(L"T_MaxToken_ expected"); break;
			case 61: s = coco_string_create(L"??? expected"); break;
			case 62: s = coco_string_create(L"invalid stat"); break;
			case 63: s = coco_string_create(L"invalid laststat"); break;
			case 64: s = coco_string_create(L"invalid forstat_"); break;
			case 65: s = coco_string_create(L"invalid localdecl_"); break;
			case 66: s = coco_string_create(L"invalid exp"); break;
			case 67: s = coco_string_create(L"invalid prefixexp"); break;
			case 68: s = coco_string_create(L"invalid args"); break;
			case 69: s = coco_string_create(L"invalid unop"); break;
			case 70: s = coco_string_create(L"invalid binop"); break;
			case 71: s = coco_string_create(L"invalid parlist"); break;
			case 72: s = coco_string_create(L"invalid field"); break;
			case 73: s = coco_string_create(L"invalid fieldsep"); break;

		default:
		{
			s = QString( "generic error %1").arg(n);
		}
		break;
	}
    if( !str.isEmpty() )
        s = QString("%1 %2").arg(s).arg(str);
	if( err )
		err->error(Errors::Syntax, sourcePath, line, col, s);
	else
		qCritical() << "Error Parser" << line << col << s;
	//count++;
}

} // namespace

