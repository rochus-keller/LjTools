#ifndef EXPRESSIONPARSER_H
#define EXPRESSIONPARSER_H

/*
* Copyright 2019 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the JuaJIT BC Viewer application.
*
* The following is the license that applies to this copy of the
* application. For a license to use the application under conditions
* other than those described here, please email to me@rochus-keller.ch.
*
* GNU General Public License Usage
* This file may be used under the terms of the GNU General Public
* License (GPL) versions 2.0 or 3.0 as published by the Free Software
* Foundation and appearing in the file LICENSE.GPL included in
* the packaging of this file. Please review the following information
* to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
* http://www.gnu.org/copyleft/gpl.html.
*/

// adopted from NMR Application Framework, see https://github.com/rochus-keller/NAF

#include <QVariant>
#include <QTextStream>
class QIODevice;

typedef struct lua_State lua_State;

namespace Lua
{
	class Engine2;

    class SourceBuffer
    {
    public:
        SourceBuffer( const QByteArray& source = QByteArray() );
        bool peek( char* buf, int size);
        bool read( char* buf, int size);
        void eat(int count = -1);
        void rewind();
        bool atEnd() const { return d_eaten == d_source.size(); }
    private:
        QByteArray d_source;
        int d_eaten;
        int d_peekPtr;
    };

    class Lexer
    {
    public:
        struct Token
        {
            enum Type {
                Invalid, Nil, Bool, Number, String, Name,
                LBrack, RBrack, // []
                LBrace, RBrace, // ()
                Dot, Op
            };
            Type d_type;
            QVariant d_val;
            Token( Type t = Invalid, const QVariant& v = QVariant()):d_type(t),d_val(v){}
            bool isValid() const { return d_type != Invalid; }
        };
        enum Operator
        {
            NoOp,
            // Gleiche Reihenfolge wie in lcode.h
            Plus, Minus, Star, Slash, Percent,   /* '+' '-' '*' '/' '%' */
            Hat, Concat,                               /* power and concat (right associative) */
            Neq, Eq,                                   /* equality and inequality */
            Lt, Leq, Gt, Geq,                          /* order */
            And, Or,                                   /* order */
            /*Unary Minus, */ Not, Pound               // Unary
        };
        bool process( const QByteArray& );
        const QString& getError() const { return d_error; }
		void dump( QTextStream& ) const;
        Token next();
        Token peek(int i = 0 );
    protected:
        struct LexEx : public std::exception {};
        Operator fetchOp();
        bool fetchName( QByteArray& );
        bool fetchString( QByteArray& str );
        bool fetchNumber( double& d );
        void eatSpace();
        bool error( const char* msg );
        Token fetchNext();
   private:
        QList<Token> d_tokens;
        int d_cur;
        SourceBuffer d_source;
        QString d_error;
    };

    class ExpressionParser
    {
    public:

        enum AstNodeType {
            Invalid,
            Constant,   // nil | false | true | Number | String
            BinOp,      // exp binop exp
            UnOp,       // unop exp
            DotOp,      // prefixexp '.' Name
            BraceOp,    // '(' exp ')'
            IndexOp,    // prefixexp '[' exp ']'
            Name        // Name
        };
        struct AstNode
        {
            AstNode(AstNodeType t = Invalid,const QVariant& v = QVariant(), AstNode* left = 0, AstNode* right = 0):
                d_type(t),d_val(v),d_left(left),d_right(right){}

            ~AstNode();
            AstNodeType d_type;
            QVariant d_val;
            AstNode* d_left;
            AstNode* d_right;
			void dump( QTextStream&, int level = 0 ) const;
        };
        ExpressionParser();
        ~ExpressionParser();
        bool parse( const QByteArray & );
		bool parseAndPrint( const QByteArray &, Engine2*, bool doDump = false );
        const AstNode* getTop() const { return d_top; }
        const QString& getError() const { return d_error; }
		int execute(Engine2*);
		bool executeAndPrint(Engine2*);
		void dump(QTextStream&) const;
    protected:
        struct ParsEx : public std::exception {};
        void prefixexp(AstNode*);
        void primaryexp(AstNode*);
        void simpleexp( AstNode* );
        void expr(AstNode*);
        Lexer::Operator subexpr( AstNode*, quint32 limit, int level);
        void error( const char* msg );
		int depthFirstExec(Engine2 *e, AstNode*);
    private:
        AstNode* d_top;
        Lexer d_lex;
        QString d_error;
    };
}

#endif // EXPRESSIONPARSER_H
