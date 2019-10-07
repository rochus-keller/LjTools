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

// Fragmente aus lparser.c mit folgender Lizenz
/*
** $Id: lparser.c,v 2.42.1.4 2011/10/21 19:31:42 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#include "ExpressionParser.h"
#include <lua.hpp>
#include "Engine2.h"
#include <QtDebug>
#include <ctype.h>
#include <math.h>
using namespace Lua;

ExpressionParser::ExpressionParser():d_top(0)
{
}

ExpressionParser::~ExpressionParser()
{
    if( d_top )
        delete d_top;
}

bool Lua::ExpressionParser::parse(const QByteArray & str)
{
    // http://www.lua.org/manual/5.1/manual.html Subset
    // exp ::=  nil | false | true | Number | String | prefixexp | exp binop exp | unop exp
    // prefixexp ::= var | '(' exp ')'
    // var ::=  Name | prefixexp '[' exp ']' | prefixexp '.' Name
    // binop ::= '+' | '-' | '*' | '/' | '^' | '%' | '..' | '<' | '<=' | '>' | '>=' | '==' | '~=' | and | or
    // unop ::= '-' | not | '#'
    // Leider ist die Syntax mehrdeutig; Lösungen siehe http://www.engr.mun.ca/~theo/Misc/exp_parsing.htm
    // Siehe auch http://lua-users.org/lists/lua-l/2011-10/msg00438.html

    if( d_top )
        delete d_top;
    if( str.isEmpty() )
    {
        d_top = new AstNode();
        return true;
    }
    if( !d_lex.process( str ) )
    {
        d_error = d_lex.getError();
        return false;
    }
    try
    {
        d_lex.next();
        AstNode tmp;
        expr( &tmp );
        d_top = tmp.d_left;
        tmp.d_left = 0;
    }catch( const ParsEx& )
    {
        return false;
    }
    return true;
}

bool ExpressionParser::parseAndPrint(const QByteArray & source, Engine2 *e, bool doDump )
{
    Q_ASSERT( e != 0 );
    if( parse( source ) )
    {
        if( doDump )
        {
            Q_ASSERT( getTop() != 0 );
            QByteArray str;
			QTextStream out( &str );
            dump( out );
            e->print(str);
        }
        return true;
    }else
    {
        if( !getError().isEmpty() )
            e->error( getError().toLatin1() );
        else
            e->error( "unknown expression parser error");
        return false;
    }
}

int ExpressionParser::execute(Engine2 * e)
{
    Q_ASSERT( e != 0 );
    d_error.clear();
    if( d_top == 0 )
        return 0;
    try
    {
        if( d_top->d_type == Invalid )
        {
            lua_pushnil(e->getCtx());
            return 1;
        }
        if( d_top->d_type == Name )
        {
            e->pushLocalOrGlobal( d_top->d_val.toByteArray() );
            return 1;
        }else
            return depthFirstExec( e, d_top );
    }catch( const ParsEx& )
    {
        return 0;
    }
}

bool ExpressionParser::executeAndPrint(Engine2 * e)
{
    if( execute( e ) > 0 )
    {
        switch( lua_type( e->getCtx(), -1 ) )
        {
        case LUA_TNIL:
        case LUA_TNUMBER:
        case LUA_TBOOLEAN:
        case LUA_TSTRING:
            e->print( e->getValueString(-1) );
            break;
        default:
            e->print( e->getTypeName( -1 ) + ": " + e->getValueString(-1) );
            break;
        }
        e->pop();
        return true;
    }else
    {
        if( !getError().isEmpty() )
            e->error( getError().toLatin1() );
        else
            e->error( "unknown execution error");
        return false;
    }
}

void ExpressionParser::dump(QTextStream &out) const
{
    d_lex.dump( out );
    if( d_top )
        d_top->dump(out);
    else
        out << "No top node" << endl;
}

void ExpressionParser::error(const char *msg)
{
    d_error = msg;
    throw ParsEx();
}

static inline void _evalName( Engine2* e, ExpressionParser::AstNode* n, int arg )
{
    Q_ASSERT( n != 0 );
    if( n->d_type == ExpressionParser::Name )
    {
        const QByteArray name = lua_tostring( e->getCtx(), arg );
        e->pushLocalOrGlobal( name );
        // _printStack( e, 4, "in evalName" );
        lua_replace( e->getCtx(), (arg<0)?(arg - 1):arg );
    }
}

#define luai_nummod(a,b)	((a) - floor((a)/(b))*(b))

static bool _compareSimpleOrUser(Engine2* e, int op )
{
    switch( lua_type( e->getCtx(), -2 ) )
    {
    case LUA_TNIL:
    case LUA_TNUMBER:
    case LUA_TBOOLEAN:
    case LUA_TSTRING:
    case LUA_TLIGHTUSERDATA:
    case LUA_TUSERDATA:
        switch(op)
        {
        case Lexer::Neq:
            lua_pushboolean( e->getCtx(), !lua_equal( e->getCtx(), -2, -1 ) );
            break;
        case Lexer::Eq:
            lua_pushboolean( e->getCtx(), lua_equal( e->getCtx(), -2, -1 ) );
            break;
        case Lexer::Lt:
            lua_pushboolean( e->getCtx(), lua_lessthan( e->getCtx(), -2, -1 ) );
            break;
        case Lexer::Leq:
            lua_pushboolean( e->getCtx(), lua_lessthan( e->getCtx(), -2, -1 ) || lua_equal( e->getCtx(), -2, -1 ) );
            break;
        case Lexer::Gt:
            lua_pushboolean( e->getCtx(), !lua_lessthan( e->getCtx(), -2, -1 ) && !lua_equal( e->getCtx(), -2, -1 ) );
            break;
        case Lexer::Geq:
            lua_pushboolean( e->getCtx(), !lua_lessthan( e->getCtx(), -2, -1 ) || lua_equal( e->getCtx(), -2, -1 ) );
            break;
        default:
            return false;
        }
        return true;
    default:
        break;
    }
    return false;
}

int ExpressionParser::depthFirstExec(Engine2* e, ExpressionParser::AstNode * n)
{
    if( n == 0 )
        return 0;
    if( n->d_left )
        depthFirstExec( e, n->d_left );
    if( n->d_right )
        depthFirstExec( e, n->d_right );

    switch( n->d_type )
    {
    case Constant:
        switch( n->d_val.type() )
        {
        case QVariant::Invalid:
            lua_pushnil( e->getCtx() );
            return 1;
        case QVariant::Bool:
            lua_pushboolean( e->getCtx(), n->d_val.toBool() );
            return 1;
        case QVariant::Double:
            lua_pushnumber( e->getCtx(), n->d_val.toDouble());
            return 1;
        default:
            lua_pushstring( e->getCtx(), n->d_val.toString().toLatin1() );
            return 1;
        }
        break;
    case BinOp:
        _evalName( e, n->d_left, -2 );
        _evalName( e, n->d_right, -1 );
        switch( n->d_val.toInt() )
        {
        case Lexer::Plus:
            lua_pushnumber( e->getCtx(), lua_tonumber( e->getCtx(), -2 ) + lua_tonumber( e->getCtx(), -1 ) );
            break;
        case Lexer::Minus:
            lua_pushnumber( e->getCtx(), lua_tonumber( e->getCtx(), -2 ) - lua_tonumber( e->getCtx(), -1 ) );
            break;
        case Lexer::Star:
            lua_pushnumber( e->getCtx(), lua_tonumber( e->getCtx(), -2 ) * lua_tonumber( e->getCtx(), -1 ) );
            break;
        case Lexer::Slash:
            lua_pushnumber( e->getCtx(), lua_tonumber( e->getCtx(), -2 ) / lua_tonumber( e->getCtx(), -1 ) );
            break;
        case Lexer::Percent:
            lua_pushnumber( e->getCtx(), luai_nummod( lua_tonumber( e->getCtx(), -2 ),
                                                      lua_tonumber( e->getCtx(), -1 ) ) );
            break;
        case Lexer::Hat:
            lua_pushnumber( e->getCtx(), ::pow( lua_tonumber( e->getCtx(), -2 ),
                                                      lua_tonumber( e->getCtx(), -1 ) ) );
            break;
        case Lexer::Concat:
            lua_pushstring( e->getCtx(), QByteArray( lua_tostring( e->getCtx(), -2 ) +
                                                     QByteArray( lua_tostring( e->getCtx(), -1 ) ) ) );
            break;
        case Lexer::Neq:
        case Lexer::Eq:
        case Lexer::Lt:
        case Lexer::Leq:
        case Lexer::Gt:
        case Lexer::Geq:
            if( !_compareSimpleOrUser( e, n->d_val.toInt() ) )
            {
                lua_pop( e->getCtx(), 2 );
                error("binary operation not supported for operands");
            }
            break;
        case Lexer::And:
            lua_pushboolean( e->getCtx(), lua_toboolean( e->getCtx(), -2 ) && lua_toboolean( e->getCtx(), -1 ) );
            break;
        case Lexer::Or:
            lua_pushboolean( e->getCtx(), lua_toboolean( e->getCtx(), -2 ) || lua_toboolean( e->getCtx(), -1 ) );
            break;
        default:
            lua_pop( e->getCtx(), 2 );
            error("invalid binary operator");
            break;
        }
        // Stack: val1, val2, res
        lua_replace( e->getCtx(), -3 );
        // Stack: res, val2
        lua_pop( e->getCtx(), 1 );
        // Stack: res
        return 1;
    case UnOp:
        _evalName( e, n->d_left, -1 );
        switch( n->d_val.toInt() )
        {
        case Lexer::Minus:
            lua_pushnumber( e->getCtx(), -lua_tonumber( e->getCtx(), -1 ) );
            lua_replace( e->getCtx(), -2 );
            return 1;
        case Lexer::Not:
            lua_pushboolean( e->getCtx(), !lua_toboolean( e->getCtx(), -1 ) );
            lua_replace( e->getCtx(), -2 );
            return 1;
        case Lexer::Pound:
            switch( lua_type( e->getCtx(), -1 ) )
            {
            case LUA_TTABLE:
            case LUA_TSTRING:
                lua_pushnumber( e->getCtx(), lua_objlen( e->getCtx(), -1 ) );
                lua_replace( e->getCtx(), -2 );
                return 1;
            default:
                lua_pop( e->getCtx(), 1 );
                error("invalid operand for # operator");
                break;
            }
            break;
        default:
            lua_pop( e->getCtx(), 1 );
            error("invalid unary operator");
            break;
        }
        break;
    case DotOp:
    case IndexOp:
        // Start einer Kette der Form a.b.c; nur der unterste Node (hier a) hat links einen Name
        //_printStack( e, 4, "before evalName" );
        _evalName( e, n->d_left, -2 );
        //_printStack( e, 4, "after evalName" );
        if( lua_isuserdata( e->getCtx(), -2 ) )
            lua_gettable( e->getCtx(), -2 ); // Zugriff mit Metamethoden
        else if( lua_istable( e->getCtx(), -2 ) )
        {
            lua_rawget( e->getCtx(), -2 );
        }else
        {
            lua_pop(e->getCtx(), 2);
            error( "invalid left operand in index operation");
        }
        // Stack: user|table, val
        lua_remove( e->getCtx(), -2 );
        // Stack: val
        return 1;
    case Name:
        lua_pushstring( e->getCtx(), n->d_val.toByteArray() );
        return 1;
    default:
        break;
    }
    return 0;
}

Lexer::Operator Lexer::fetchOp()
{
    char ch;
    char ch2[3];
    d_source.peek( &ch, 1 );
    switch( ch )
    {
    case '+':
        d_source.eat();
        return Plus;
    case '-':
        d_source.eat();
        return Minus;
    case '*':
        d_source.eat();
        return Star;
    case '/':
        d_source.eat();
        return Slash;
    case '^':
        d_source.eat();
        return Hat;
    case '%':
        d_source.eat();
        return Percent;
    case '<': // '<' | '<='
        d_source.peek( ch2, 1 );
        if( ch2[0] == '=' )
        {
            d_source.eat();
            return Leq;
        }else
        {
            d_source.eat(1);
            return Lt;
        }
    case '>':
        d_source.peek( ch2, 1 );
        if( ch2[0] == '=' )
        {
            d_source.eat();
            return Geq;
        }else
        {
            d_source.eat(1);
            return Gt;
        }
    case '.':
        d_source.peek( ch2, 1 );
        if( ch2[0] == '.' )
        {
            d_source.eat();
            return Concat;
        }
        break;
    case '=':
        d_source.peek( ch2, 1 );
        if( ch2[0] == '=' )
        {
            d_source.eat();
            return Eq;
        }
        break;
    case '~':
        d_source.peek( ch2, 1 );
        if( ch2[0] == '=' )
        {
            d_source.eat();
            return Neq;
        }
        break;
    case '#':
        d_source.eat();
        return Pound;
    case 'a':
        d_source.peek( ch2, 2 );
        if( ch2[0] == 'n' && ch2[1] == 'd' )
        {
            d_source.eat();
            return And;
        }
        break;
    case 'o':
        d_source.peek( ch2, 1 );
        if( ch2[0] == 'r' )
        {
            d_source.eat();
            return Or;
        }
        break;
    case 'n':
        d_source.peek( ch2, 2 );
        if( ch2[0] == 'o' && ch2[1] == 't' )
        {
            d_source.eat();
            return Not;
        }
        break;
    }
    d_source.rewind();
    return NoOp;
}

bool Lexer::error(const char *msg)
{
    d_source.rewind();
    d_error = msg;
    throw LexEx();
    return false;
}

Lexer::Token Lexer::fetchNext()
{
    QByteArray str;
    double number;
    char ch;

    if( d_source.atEnd() )
        return Token();

    eatSpace();

    if( fetchString( str ) ) // | String
        return Token( Token::String, str );

    if( fetchNumber( number ) ) // | Number
        return Token( Token::Number, number );

    if( fetchName( str) ) // | nil | false | true
    {
        if( str == "nil" )
            return Token( Token::Nil );
        else if( str == "false" )
            return Token( Token::Bool, false );
        else if( str == "true" )
            return Token( Token::Bool, true );
        else
            return Token( Token::Name, str );
    }
    Operator op = fetchOp();
    if( op != NoOp ) // exp binop exp
        return Token( Token::Op, op );

    d_source.peek( &ch, 1 );
    switch( ch )
    {
    case '(':
        d_source.eat();
        return Token( Token::LBrace );
    case ')':
        d_source.eat();
        return Token( Token::RBrace );
    case '[':
        d_source.eat();
        return Token( Token::LBrack );
    case ']':
        d_source.eat();
        return Token( Token::RBrack );
    case '.':
        d_source.eat();
        return Token( Token::Dot );
    }
    d_source.rewind();
    error( QByteArray("invalid token detected") );
    return Token();
}

bool Lexer::fetchName(QByteArray & out)
{
    // any string of letters, digits, and underscores, not beginning with a digit
    char ch;
    d_source.peek( &ch, 1 );
    if( ::isalpha(ch) || ch == '_' )
    {
        QByteArray name;
        while( ::isalpha(ch) || ::isdigit(ch) || ch == '_' )
        {
            name += ch;
            d_source.peek( &ch, 1 );
        }
        d_source.eat(name.size());
        out = name;
        return true;
    }
    d_source.rewind();
    return false;
}

bool Lexer::fetchString( QByteArray &str)
{
    char ch;
    d_source.peek( &ch, 1 );
    QByteArray tmp;
    bool escape = false;
    if( ch == '"' )
    {
        d_source.peek( &ch, 1 );
        while( escape || ch != '"' )
        {
            escape = ch == '\\';
            tmp += ch;
            d_source.peek( &ch, 1 );
        }
        str = tmp;
        d_source.eat();
        return true;
    }else if( ch == '\'' )
    {
        d_source.peek( &ch, 1 );
        while( escape || ch != '\'' )
        {
            escape = ch == '\\';
            tmp += ch;
            d_source.peek( &ch, 1 );
        }
        str = tmp;
        d_source.eat();
        return true;
    }else if( ch == '[' )
    {
        d_source.peek( &ch, 1 );
        QByteArray endToken = "]";
        if( ch == '=' )
        {
            // Es ist ein Starter der Form [====[
            while( ch != 0 && ch != '[' )
            {
                endToken += "=";
                d_source.peek( &ch, 1 );
            }

        }else if( ch == '[' )
            // es ist ein Starter der Form [[
            endToken += "]";
        else
        {
            // Es ist kein String
            d_source.rewind();
            return false;
        }
        do
        {
            d_source.peek( &ch, 1 );
            tmp += ch;
        }while( ch != 0 && !tmp.endsWith( endToken ) );
        str = tmp;
        str.chop( endToken.size() );
        d_source.eat();
        return true;
    }
    d_source.rewind();
    return false;
}

static inline bool _isHexChar( char ch )
{
    return ( ch >= 'a' && ch <= 'f' ) || ( ch >= 'A' && ch <= 'F' );
}

bool Lexer::fetchNumber( double &d)
{
    char ch;
    d_source.peek( &ch, 1 );
    QByteArray number;

    enum State { Vorkomma, Nachkomma, Exponent2, Exponent1 };
    State state = Vorkomma;
    // Erstes Zeichen
    if( ch == '.' )
    {
        state = Nachkomma;
    }else if( !::isdigit( ch ) )
    {
        d_source.rewind();
        return false;
    }

    char ch2;
    d_source.peek( &ch2, 1 );
    if( state == Nachkomma && !::isdigit(ch2) )
    {
        d_source.rewind();
        return false; // wir haben einen Punkt gefunden, der aber nicht von einer Zahl gefolgt wird
    }

    bool ok;
    if( ch2 == 'x' )
    {
        if( ch != '0')
            return error("invalid number format");
        // Parse Hex
        while( true )
        {
            d_source.peek( &ch2, 1 );
            if( ::isdigit( ch2 ) || _isHexChar( ch2 ) )
                number += ch2;
            else
            {
                // Zahl ist fertig
                d = number.toUInt( &ok, 16 );
                if( !ok )
                    return error("invalid hex format");
                else
                {
                    d_source.eat( number.size() + 2 );
                    return true;
                }
            }
        }
    }else
    {
        // Parse Decimal
        number += ch;
        int anzNachkomma = 0;
        while( true )
        {
            switch( state )
            {
            case Vorkomma:
                if( ch2 == '.' )
                {
                    state = Nachkomma;
                }else if( ::isdigit( ch2 ) )
                    ;
                else if( ch2 == 'e' || ch2 == 'E' )
                {
                    state = Exponent1;
                    ch2 = 'e';
                }else
                {
                    d = number.toUInt( &ok );
                    if( !ok )
                        return error("invalid integer");
                    else
                    {
                        d_source.eat( number.size() );
                        return true;
                    }
                }
                number += ch2;
                break;
            case Nachkomma:
                if( ::isdigit( ch2 ) )
                    ;
                else if( ch2 == 'e' || ch2 == 'E' )
                {
                    if( anzNachkomma == 0 )
                        return error("invalid number format");
                    state = Exponent1;
                    ch2 = 'e';
                }else
                {
                    // Zahl ist fertig
                    if( anzNachkomma == 0 )
                        return error("invalid number format");
                    d = number.toDouble( &ok );
                    if( !ok )
                        return error("invalid decimal format");
                    else
                    {
                        d_source.eat( number.size() );
                        return true;
                    }
                }
                anzNachkomma++;
                number += ch2;
                break;
            case Exponent1:
                if( ::isdigit( ch2 ) || ch2 == '+' || ch2 == '-' )
                    state = Exponent2;
                else
                    return error("invalid number format");
                number += ch2;
                break;
            case Exponent2:
                if( ::isdigit( ch2 ) )
                    ;
                else
                {
                    // Zahl ist fertig
                    d = number.toDouble( &ok );
                    if( !ok )
                        return error("invalid exponential format");
                    else
                    {
                        d_source.eat( number.size() );
                        return true;
                    }
                }
                number += ch2;
                break;
            }
            d_source.peek( &ch2, 1 );
        }
    }
    d_source.rewind();
    return false;
}

void Lexer::eatSpace()
{
    char ch;
    d_source.peek( &ch, 1 );
    while( ::isspace(ch) )
    {
        d_source.eat();
        d_source.peek( &ch, 1 );
    }
    d_source.rewind();
}

bool SourceBuffer::peek(char *buf, int size)
{
    bool ok = true;
    int i;
    for( i = 0; i < size; i++ )
    {
        if( d_peekPtr + i < d_source.size() )
            buf[i] = d_source[ d_peekPtr + i ];
        else
        {
            buf[i] = 0;
            ok = false;
        }
    }
    d_peekPtr += i;
    return ok;
}

bool SourceBuffer::read(char *buf, int size)
{
    peek( buf, size );
    eat();
    return true;
}

void SourceBuffer::eat(int count)
{
    // Nach peak steht d_peekPtr auf dem nächsten zu lesenden Zeichen
    if( count == -1 || d_peekPtr - d_eaten < count )
        d_eaten = d_peekPtr;
    else
    {
        d_eaten += count;
        d_peekPtr = d_eaten;
    }
    for( int i = 0; i < d_eaten && i < d_source.size(); i++ )
        d_source[i] = '@'; // TEST
}

void SourceBuffer::rewind()
{
    d_peekPtr = d_eaten;
}

ExpressionParser::AstNode::~AstNode()
{
    if( d_left )
        delete d_left;
    if( d_right )
        delete d_right;
}

static QString _toTypeStr( int t )
{
    switch( t )
    {
    case ExpressionParser::Constant:
        return "Constant";
    case ExpressionParser::BinOp:
        return "Binop";
    case ExpressionParser::UnOp:
        return "Unop";
    case ExpressionParser::DotOp:
        return "DotOp";
    case ExpressionParser::BraceOp:
        return "BraceOp";
    case ExpressionParser::IndexOp:
        return "BracketOp";
    case ExpressionParser::Name:
        return "NameFetch";
    default:
        break;
    }
    return "Invalid";
}

static QString _toOpStr( int o )
{
    switch(o)
    {
    case Lexer::Plus:
        return "+";
    case Lexer::Minus:
        return "-";
    case Lexer::Star:
        return "*";
    case Lexer::Slash:
        return "/";
    case Lexer::Hat:
        return "^";
    case Lexer::Percent:
        return "%";
    case Lexer::Concat:
        return "..";
    case Lexer::Lt:
        return "<";
    case Lexer::Leq:
        return "<=";
    case Lexer::Gt:
        return ">";
    case Lexer::Geq:
        return ">=";
    case Lexer::Eq:
        return "==";
    case Lexer::Neq:
        return "~=";
    case Lexer::And:
        return "and";
    case Lexer::Or:
        return "or";
    case Lexer::Not:
        return "not";
    case Lexer::Pound:
        return "#";
    default:
        break;
    }
    return "?";
}

static QString _toValStr( int t, const QVariant& v )
{
    switch( t )
    {
    case ExpressionParser::Constant:
        if( v.isNull() )
            return "nil";
        else if( v.type() == QVariant::ByteArray )
            return QString("\"%1\"").arg( v.toString() );
        else
            return v.toString();
    case ExpressionParser::Name:
        return v.toString();
    case ExpressionParser::BinOp:
    case ExpressionParser::UnOp:
        return _toOpStr( v.toInt() );
    default:
        return "<none>";
    }
}

void ExpressionParser::AstNode::dump(QTextStream &out, int level) const
{
    const QString white( level * 4, QChar(' ') );
    out << white << "Token: " << _toTypeStr( d_type ) << " Value: " << _toValStr( d_type, d_val ) << endl;
    if( d_left )
        d_left->dump( out, level + 1 );
    if( d_right )
        d_right->dump( out, level + 1 );

}

SourceBuffer::SourceBuffer(const QByteArray &source):d_source(source),d_eaten(0),d_peekPtr(0)
{
}

bool Lexer::process(const QByteArray &buf)
{
    d_source = SourceBuffer(buf);
    d_tokens.clear();
    d_error.clear();
    d_cur = 0;
    try
    {
        Token t = fetchNext();
        while( t.isValid() )
        {
            d_tokens.append(t);
            t = fetchNext();
        }
    }catch( const LexEx& )
    {
        return false;
    }
    return true;
}

static QString _toValStr2( int t, const QVariant& v )
{
    switch( t )
    {
    case Lexer::Token::Bool:
    case Lexer::Token::Number:
    case Lexer::Token::Name:
        return QString("=%1").arg( v.toString() );
    case Lexer::Token::String:
        return QString("=\"%1\"").arg( v.toString() );
    case Lexer::Token::Op:
        return QString("='%1'").arg( _toOpStr( v.toInt() ) );
    default:
        return QString();
    }
}

static QString _toTypeStr2( int t )
{
    switch( t )
    {
    case Lexer::Token::Nil:
        return "Nil";
    case Lexer::Token::Bool:
        return "Bool";
    case Lexer::Token::Number:
        return "Number";
    case Lexer::Token::String:
        return "String";
    case Lexer::Token::Name:
        return "Name";
    case Lexer::Token::LBrack:
        return "LBrack";
    case Lexer::Token::RBrack:
        return "RBrack";
    case Lexer::Token::LBrace:
        return "LBrace";
    case Lexer::Token::RBrace:
        return "RBrace";
    case Lexer::Token::Dot:
        return "Dot";
    case Lexer::Token::Op:
        return "Op";
    default:
        break;
    }
    return "Invalid";
}

void Lexer::dump(QTextStream &out) const
{
    foreach( const Token& t, d_tokens )
    {
        out << _toTypeStr2( t.d_type ) << _toValStr2( t.d_type, t.d_val ) << QString(" ");
    }
    out << endl;
}

Lexer::Token Lexer::next()
{
    if( d_cur < d_tokens.size() )
    {
        Token t = d_tokens[d_cur];
        d_cur++;
        return t;
    }else
        return Token();
}

Lexer::Token Lexer::peek(int i)
{
    i++;
    if( d_cur - i >= 0 )
        return d_tokens[ d_cur - i ];
    else
        return Token();
}

void ExpressionParser::prefixexp(AstNode* n)
{
    /* prefixexp -> NAME | '(' expr ')' */

    Lexer::Token tok = d_lex.peek();
    switch( tok.d_type )
    {
    case Lexer::Token::LBrace:
        tok = d_lex.next();
        n->d_left = new AstNode( BraceOp );
        expr( n->d_left );
        tok = d_lex.peek();
        if( tok.d_type != Lexer::Token::RBrace )
            error( "expecting ')'" );
        tok = d_lex.next(); // in check_match enthalten!
        return;
    case Lexer::Token::Name:
        n->d_left = new AstNode( Name, tok.d_val );
        // singlevar wird gleich hier alles erledigt
        tok = d_lex.next(); // singlevar enthält str_checkname was das Token weiterschaltet!
        return;
    default:
        error("unexpected symbol");
        return;
    }
}

void ExpressionParser::primaryexp(ExpressionParser::AstNode * n)
{
    /* primaryexp -> prefixexp { '.' NAME | '[' exp ']' } */
    prefixexp( n );
    // n->d_left ist bereits gesetzt
    for(;;)
    {
        Lexer::Token tok = d_lex.peek();
        switch( tok.d_type )
        {
        case Lexer::Token::Dot:
            {
                AstNode* opNode = new AstNode( DotOp );
                opNode->d_left = n->d_left;
                n->d_left = opNode;
                tok = d_lex.next();
                if( tok.d_type != Lexer::Token::Name )
                    error("expecting name");
                opNode->d_right = new AstNode( Name, tok.d_val );
                tok = d_lex.next(); // in checkname indirekt enthalten!
            }
            break;
        case Lexer::Token::LBrack:
            {
                /* index -> '[' expr ']' */
                tok = d_lex.next();
                AstNode* opNode = new AstNode( IndexOp );
                opNode->d_left = n->d_left;
                n->d_left = opNode;
                AstNode tmp;
                expr( &tmp );
                opNode->d_right = tmp.d_left;
                tmp.d_left = 0;
                tok = d_lex.peek();
                if( tok.d_type != Lexer::Token::RBrack )
                    error( "expecting ']'");
                tok = d_lex.next(); // in checknext enthalten!
            }
            break;
        default:
            return;
        }
    }
}

void ExpressionParser::expr(ExpressionParser::AstNode * n)
{
    subexpr( n, 0, 0 );
}

// Konzept aus lparser.c
static const struct {
  quint8 left;  /* left priority for each binary operator */
  quint8 right; /* right priority */
} priority[] = {  /* ORDER OPR */
                  { 0, 0 }, // NoOp
   {6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  /* '+' '-' '/' '%' */
   {10, 9}, {5, 4},                 /* power and concat (right associative) */
   {3, 3}, {3, 3},                  /* equality and inequality */
   {3, 3}, {3, 3}, {3, 3}, {3, 3},  /* order */
   {2, 2}, {1, 1}                   /* logical (and/or) */
                  // Rest des enums hier nicht verwendet
};

#define UNARY_PRIORITY  8  /* priority for unary operators */

static Lexer::Operator _getbinopr( const Lexer::Token& t )
{
    if( t.d_type == Lexer::Token::Op )
        return (Lexer::Operator)t.d_val.toUInt();
    else
        return Lexer::NoOp;
}
static bool _isUnop( const Lexer::Token& t )
{
    if( t.d_type == Lexer::Token::Op )
    {
        switch( t.d_val.toInt() )
        {
        case Lexer::Minus:
        case Lexer::Pound:
        case Lexer::Not:
            return true;
        default:
            break;
        }
    }
    return false;
}

Lexer::Operator ExpressionParser::subexpr(ExpressionParser::AstNode * n, quint32 limit, int level)
{
    /*
    ** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
    ** where 'binop' is any binary operator with a priority higher than 'limit'
    */
    Lexer::Token tok = d_lex.peek();
    if( _isUnop(tok) )
    {
        n->d_left = new AstNode( UnOp, tok.d_val );
        tok = d_lex.next();
        subexpr( n->d_left, UNARY_PRIORITY, level + 1 );
    }else
        simpleexp( n );
    // hier hat also n->d_left einen Wert
    // qDebug() << QString(level*4,QChar(' ') ) << "subexpr: simpe|unop" << _toTypeStr( n->d_left->d_type ) << _toValStr( n->d_left->d_type, n->d_left->d_val );
    /* expand while operators have priorities higher than 'limit' */
    tok = d_lex.peek();
    Lexer::Operator op = _getbinopr( tok );
    while( op != Lexer::NoOp && priority[op].left > limit )
    {
        // qDebug() << QString(level*4,QChar(' ') ) << "subexpr: pass" << _toOpStr(op) << "prio left" << priority[op].left << "right" << priority[op].right << "level" << level;
        tok = d_lex.next();

        /* read sub-expression with higher priority */
        AstNode v2( BinOp, op );
        Lexer::Operator nextop = subexpr( &v2, priority[op].right, level + 1 );

        AstNode* opNode = new AstNode( BinOp, op );
        opNode->d_left = n->d_left;
        n->d_left = opNode;
        opNode->d_right = v2.d_left;
        v2.d_left = 0;

        op = nextop;
    }
    return op;
}

void ExpressionParser::simpleexp(ExpressionParser::AstNode * n)
{
    /* simpleexp -> NUMBER | STRING | NIL | true | false | primaryexp */
    Lexer::Token tok = d_lex.peek();
    switch( tok.d_type )
    {
    case Lexer::Token::Number:
    case Lexer::Token::String:
    case Lexer::Token::Bool:
    case Lexer::Token::Nil:
        n->d_left = new AstNode( Constant, tok.d_val );
        break;
    default:
        primaryexp( n );
        tok = d_lex.peek();
        return;
    }
    tok = d_lex.next();
}
