#ifndef __LUA_TOKENTYPE__
#define __LUA_TOKENTYPE__
// This file was automatically generated by EbnfStudio; don't modify it!


#include <QByteArray>

namespace Lua {
	enum TokenType {
		Tok_Invalid = 0,

		TT_Literals,
		Tok_Hash,
		Tok_Percent,
		Tok_Lpar,
		Tok_Rpar,
		Tok_Star,
		Tok_Plus,
		Tok_Comma,
		Tok_Minus,
		Tok_2Minus,
		Tok_2MinusLbrack,
		Tok_Dot,
		Tok_2Dot,
		Tok_3Dot,
		Tok_Slash,
		Tok_Colon,
		Tok_Semi,
		Tok_Lt,
		Tok_Leq,
		Tok_Eq,
		Tok_2Eq,
		Tok_Gt,
		Tok_Geq,
		Tok_Lbrack,
		Tok_Rbrack,
		Tok_Rbrack2Minus,
		Tok_Hat,
		Tok_Lbrace,
		Tok_Rbrace,
		Tok_TildeEq,

		TT_Keywords,
		Tok_and,
		Tok_break,
		Tok_do,
		Tok_else,
		Tok_elseif,
		Tok_end,
		Tok_false,
		Tok_for,
		Tok_function,
		Tok_if,
		Tok_in,
		Tok_local,
		Tok_nil,
		Tok_not,
		Tok_or,
		Tok_repeat,
		Tok_return,
		Tok_then,
		Tok_true,
		Tok_until,
		Tok_while,

		TT_Specials,
		Tok_Name,
		Tok_Number,
		Tok_String,
		Tok_Comment,
		Tok_Designator,
		Tok_Eof,

		TT_MaxToken,

		TT_Max
	};

	const char* tokenTypeString( int ); // Pretty with punctuation chars
	const char* tokenTypeName( int ); // Just the names without punctuation chars
	bool tokenTypeIsLiteral( int );
	bool tokenTypeIsKeyword( int );
	bool tokenTypeIsSpecial( int );
	TokenType tokenTypeFromString( const QByteArray& str, int* pos = 0 );
}
#endif // __LUA_TOKENTYPE__
