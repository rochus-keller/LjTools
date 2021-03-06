// This file was automatically generated by EbnfStudio; don't modify it!
#include "LuaTokenType.h"

namespace Lua {
	const char* tokenTypeString( int r ) {
		switch(r) {
			case Tok_Invalid: return "<invalid>";
			case Tok_Hash: return "#";
			case Tok_Percent: return "%";
			case Tok_Lpar: return "(";
			case Tok_Rpar: return ")";
			case Tok_Star: return "*";
			case Tok_Plus: return "+";
			case Tok_Comma: return ",";
			case Tok_Minus: return "-";
			case Tok_2Minus: return "--";
			case Tok_2MinusLbrack: return "--[";
			case Tok_Dot: return ".";
			case Tok_2Dot: return "..";
			case Tok_3Dot: return "...";
			case Tok_Slash: return "/";
			case Tok_Colon: return ":";
			case Tok_Semi: return ";";
			case Tok_Lt: return "<";
			case Tok_Leq: return "<=";
			case Tok_Eq: return "=";
			case Tok_2Eq: return "==";
			case Tok_Gt: return ">";
			case Tok_Geq: return ">=";
			case Tok_Lbrack: return "[";
			case Tok_Rbrack: return "]";
			case Tok_Rbrack2Minus: return "]--";
			case Tok_Hat: return "^";
			case Tok_Lbrace: return "{";
			case Tok_Rbrace: return "}";
			case Tok_TildeEq: return "~=";
			case Tok_and: return "and";
			case Tok_break: return "break";
			case Tok_do: return "do";
			case Tok_else: return "else";
			case Tok_elseif: return "elseif";
			case Tok_end: return "end";
			case Tok_false: return "false";
			case Tok_for: return "for";
			case Tok_function: return "function";
			case Tok_if: return "if";
			case Tok_in: return "in";
			case Tok_local: return "local";
			case Tok_nil: return "nil";
			case Tok_not: return "not";
			case Tok_or: return "or";
			case Tok_repeat: return "repeat";
			case Tok_return: return "return";
			case Tok_then: return "then";
			case Tok_true: return "true";
			case Tok_until: return "until";
			case Tok_while: return "while";
			case Tok_Name: return "Name";
			case Tok_Number: return "Number";
			case Tok_String: return "String";
			case Tok_Comment: return "Comment";
			case Tok_Designator: return "Designator";
			case Tok_Eof: return "<eof>";
			default: return "";
		}
	}
	const char* tokenTypeName( int r ) {
		switch(r) {
			case Tok_Invalid: return "Tok_Invalid";
			case Tok_Hash: return "Tok_Hash";
			case Tok_Percent: return "Tok_Percent";
			case Tok_Lpar: return "Tok_Lpar";
			case Tok_Rpar: return "Tok_Rpar";
			case Tok_Star: return "Tok_Star";
			case Tok_Plus: return "Tok_Plus";
			case Tok_Comma: return "Tok_Comma";
			case Tok_Minus: return "Tok_Minus";
			case Tok_2Minus: return "Tok_2Minus";
			case Tok_2MinusLbrack: return "Tok_2MinusLbrack";
			case Tok_Dot: return "Tok_Dot";
			case Tok_2Dot: return "Tok_2Dot";
			case Tok_3Dot: return "Tok_3Dot";
			case Tok_Slash: return "Tok_Slash";
			case Tok_Colon: return "Tok_Colon";
			case Tok_Semi: return "Tok_Semi";
			case Tok_Lt: return "Tok_Lt";
			case Tok_Leq: return "Tok_Leq";
			case Tok_Eq: return "Tok_Eq";
			case Tok_2Eq: return "Tok_2Eq";
			case Tok_Gt: return "Tok_Gt";
			case Tok_Geq: return "Tok_Geq";
			case Tok_Lbrack: return "Tok_Lbrack";
			case Tok_Rbrack: return "Tok_Rbrack";
			case Tok_Rbrack2Minus: return "Tok_Rbrack2Minus";
			case Tok_Hat: return "Tok_Hat";
			case Tok_Lbrace: return "Tok_Lbrace";
			case Tok_Rbrace: return "Tok_Rbrace";
			case Tok_TildeEq: return "Tok_TildeEq";
			case Tok_and: return "Tok_and";
			case Tok_break: return "Tok_break";
			case Tok_do: return "Tok_do";
			case Tok_else: return "Tok_else";
			case Tok_elseif: return "Tok_elseif";
			case Tok_end: return "Tok_end";
			case Tok_false: return "Tok_false";
			case Tok_for: return "Tok_for";
			case Tok_function: return "Tok_function";
			case Tok_if: return "Tok_if";
			case Tok_in: return "Tok_in";
			case Tok_local: return "Tok_local";
			case Tok_nil: return "Tok_nil";
			case Tok_not: return "Tok_not";
			case Tok_or: return "Tok_or";
			case Tok_repeat: return "Tok_repeat";
			case Tok_return: return "Tok_return";
			case Tok_then: return "Tok_then";
			case Tok_true: return "Tok_true";
			case Tok_until: return "Tok_until";
			case Tok_while: return "Tok_while";
			case Tok_Name: return "Tok_Name";
			case Tok_Number: return "Tok_Number";
			case Tok_String: return "Tok_String";
			case Tok_Comment: return "Tok_Comment";
			case Tok_Designator: return "Tok_Designator";
			case Tok_Eof: return "Tok_Eof";
			default: return "";
		}
	}
	bool tokenTypeIsLiteral( int r ) {
		return r > TT_Literals && r < TT_Keywords;
	}
	bool tokenTypeIsKeyword( int r ) {
		return r > TT_Keywords && r < TT_Specials;
	}
	bool tokenTypeIsSpecial( int r ) {
		return r > TT_Specials && r < TT_Max;
	}
	static inline char at( const QByteArray& str, int i ){
		return ( i >= 0 && i < str.size() ? str[i] : 0 );
	}
	TokenType tokenTypeFromString( const QByteArray& str, int* pos ) {
		int i = ( pos != 0 ? *pos: 0 );
		TokenType res = Tok_Invalid;
		switch( at(str,i) ){
		case '#':
			res = Tok_Hash; i += 1;
			break;
		case '%':
			res = Tok_Percent; i += 1;
			break;
		case '(':
			res = Tok_Lpar; i += 1;
			break;
		case ')':
			res = Tok_Rpar; i += 1;
			break;
		case '*':
			res = Tok_Star; i += 1;
			break;
		case '+':
			res = Tok_Plus; i += 1;
			break;
		case ',':
			res = Tok_Comma; i += 1;
			break;
		case '-':
			if( at(str,i+1) == '-' ){
				if( at(str,i+2) == '[' ){
					res = Tok_2MinusLbrack; i += 3;
				} else {
					res = Tok_2Minus; i += 2;
				}
			} else {
				res = Tok_Minus; i += 1;
			}
			break;
		case '.':
			if( at(str,i+1) == '.' ){
				if( at(str,i+2) == '.' ){
					res = Tok_3Dot; i += 3;
				} else {
					res = Tok_2Dot; i += 2;
				}
			} else {
				res = Tok_Dot; i += 1;
			}
			break;
		case '/':
			res = Tok_Slash; i += 1;
			break;
		case ':':
			res = Tok_Colon; i += 1;
			break;
		case ';':
			res = Tok_Semi; i += 1;
			break;
		case '<':
			if( at(str,i+1) == '=' ){
				res = Tok_Leq; i += 2;
			} else {
				res = Tok_Lt; i += 1;
			}
			break;
		case '=':
			if( at(str,i+1) == '=' ){
				res = Tok_2Eq; i += 2;
			} else {
				res = Tok_Eq; i += 1;
			}
			break;
		case '>':
			if( at(str,i+1) == '=' ){
				res = Tok_Geq; i += 2;
			} else {
				res = Tok_Gt; i += 1;
			}
			break;
		case '[':
			res = Tok_Lbrack; i += 1;
			break;
		case ']':
			if( at(str,i+1) == '-' ){
				if( at(str,i+2) == '-' ){
					res = Tok_Rbrack2Minus; i += 3;
				}
			} else {
				res = Tok_Rbrack; i += 1;
			}
			break;
		case '^':
			res = Tok_Hat; i += 1;
			break;
		case 'a':
			if( at(str,i+1) == 'n' ){
				if( at(str,i+2) == 'd' ){
					res = Tok_and; i += 3;
				}
			}
			break;
		case 'b':
			if( at(str,i+1) == 'r' ){
				if( at(str,i+2) == 'e' ){
					if( at(str,i+3) == 'a' ){
						if( at(str,i+4) == 'k' ){
							res = Tok_break; i += 5;
						}
					}
				}
			}
			break;
		case 'd':
			if( at(str,i+1) == 'o' ){
				res = Tok_do; i += 2;
			}
			break;
		case 'e':
			switch( at(str,i+1) ){
			case 'l':
				if( at(str,i+2) == 's' ){
					if( at(str,i+3) == 'e' ){
						if( at(str,i+4) == 'i' ){
							if( at(str,i+5) == 'f' ){
								res = Tok_elseif; i += 6;
							}
						} else {
							res = Tok_else; i += 4;
						}
					}
				}
				break;
			case 'n':
				if( at(str,i+2) == 'd' ){
					res = Tok_end; i += 3;
				}
				break;
			}
			break;
		case 'f':
			switch( at(str,i+1) ){
			case 'a':
				if( at(str,i+2) == 'l' ){
					if( at(str,i+3) == 's' ){
						if( at(str,i+4) == 'e' ){
							res = Tok_false; i += 5;
						}
					}
				}
				break;
			case 'o':
				if( at(str,i+2) == 'r' ){
					res = Tok_for; i += 3;
				}
				break;
			case 'u':
				if( at(str,i+2) == 'n' ){
					if( at(str,i+3) == 'c' ){
						if( at(str,i+4) == 't' ){
							if( at(str,i+5) == 'i' ){
								if( at(str,i+6) == 'o' ){
									if( at(str,i+7) == 'n' ){
										res = Tok_function; i += 8;
									}
								}
							}
						}
					}
				}
				break;
			}
			break;
		case 'i':
			switch( at(str,i+1) ){
			case 'f':
				res = Tok_if; i += 2;
				break;
			case 'n':
				res = Tok_in; i += 2;
				break;
			}
			break;
		case 'l':
			if( at(str,i+1) == 'o' ){
				if( at(str,i+2) == 'c' ){
					if( at(str,i+3) == 'a' ){
						if( at(str,i+4) == 'l' ){
							res = Tok_local; i += 5;
						}
					}
				}
			}
			break;
		case 'n':
			switch( at(str,i+1) ){
			case 'i':
				if( at(str,i+2) == 'l' ){
					res = Tok_nil; i += 3;
				}
				break;
			case 'o':
				if( at(str,i+2) == 't' ){
					res = Tok_not; i += 3;
				}
				break;
			}
			break;
		case 'o':
			if( at(str,i+1) == 'r' ){
				res = Tok_or; i += 2;
			}
			break;
		case 'r':
			if( at(str,i+1) == 'e' ){
				switch( at(str,i+2) ){
				case 'p':
					if( at(str,i+3) == 'e' ){
						if( at(str,i+4) == 'a' ){
							if( at(str,i+5) == 't' ){
								res = Tok_repeat; i += 6;
							}
						}
					}
					break;
				case 't':
					if( at(str,i+3) == 'u' ){
						if( at(str,i+4) == 'r' ){
							if( at(str,i+5) == 'n' ){
								res = Tok_return; i += 6;
							}
						}
					}
					break;
				}
			}
			break;
		case 't':
			switch( at(str,i+1) ){
			case 'h':
				if( at(str,i+2) == 'e' ){
					if( at(str,i+3) == 'n' ){
						res = Tok_then; i += 4;
					}
				}
				break;
			case 'r':
				if( at(str,i+2) == 'u' ){
					if( at(str,i+3) == 'e' ){
						res = Tok_true; i += 4;
					}
				}
				break;
			}
			break;
		case 'u':
			if( at(str,i+1) == 'n' ){
				if( at(str,i+2) == 't' ){
					if( at(str,i+3) == 'i' ){
						if( at(str,i+4) == 'l' ){
							res = Tok_until; i += 5;
						}
					}
				}
			}
			break;
		case 'w':
			if( at(str,i+1) == 'h' ){
				if( at(str,i+2) == 'i' ){
					if( at(str,i+3) == 'l' ){
						if( at(str,i+4) == 'e' ){
							res = Tok_while; i += 5;
						}
					}
				}
			}
			break;
		case '{':
			res = Tok_Lbrace; i += 1;
			break;
		case '}':
			res = Tok_Rbrace; i += 1;
			break;
		case '~':
			if( at(str,i+1) == '=' ){
				res = Tok_TildeEq; i += 2;
			}
			break;
		}
		if(pos) *pos = i;
		return res;
	}
}
