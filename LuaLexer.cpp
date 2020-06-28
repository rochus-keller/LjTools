/*
* Copyright 2019 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Lua parser library.
*
* The following is the license that applies to this copy of the
* library. For a license to use the library under conditions
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

#include "LuaLexer.h"
#include "LjasErrors.h"
#include "LjasFileCache.h"
#include <QBuffer>
#include <QFile>
#include <QIODevice>
#include <QtDebug>
using namespace Lua;
using namespace Ljas;

QHash<QByteArray,QByteArray> Lexer::d_symbols;

Lexer::Lexer(QObject *parent) : QObject(parent),
    d_lastToken(Tok_Invalid),d_lineNr(0),d_colNr(0),d_in(0),d_err(0),d_fcache(0),
    d_ignoreComments(true), d_packComments(true),d_real("\\d*\\.?\\d+(e[-+]?\\d+)?")
{

}

void Lexer::setStream(QIODevice* in, const QString& sourcePath)
{
    if( in == 0 )
        setStream( sourcePath );
    else
    {
        d_in = in;
        d_lineNr = 0;
        d_colNr = 0;
        d_sourcePath = getSymbol(sourcePath.toUtf8());
        d_lastToken = Tok_Invalid;
    }
}

bool Lexer::setStream(const QString& sourcePath)
{
    QIODevice* in = 0;

    if( d_fcache )
    {
        bool found;
        QByteArray content = d_fcache->getFile(sourcePath, &found );
        if( found )
        {
            QBuffer* buf = new QBuffer(this);
            buf->setData( content );
            buf->open(QIODevice::ReadOnly);
            in = buf;
        }
    }

    if( in == 0 )
    {
        QFile* file = new QFile(sourcePath, this);
        if( !file->open(QIODevice::ReadOnly) )
        {
            if( d_err )
            {
                d_err->error(Errors::Lexer, sourcePath, 0, 0,
                                 tr("cannot open file from path %1").arg(sourcePath) );
            }
            delete file;
            return false;
        }
        in = file;
    }
    // else
    setStream( in, sourcePath );
    return true;
}

Token Lexer::nextToken()
{
    Token t;
    if( !d_buffer.isEmpty() )
    {
        t = d_buffer.first();
        d_buffer.pop_front();
    }else
        t = nextTokenImp();
    if( t.d_type == Tok_Comment && d_ignoreComments )
        t = nextToken();
    return t;
}

Token Lexer::peekToken(quint8 lookAhead)
{
    Q_ASSERT( lookAhead > 0 );
    while( d_buffer.size() < lookAhead )
        d_buffer.push_back( nextTokenImp() );
    return d_buffer[ lookAhead - 1 ];
}

QList<Token> Lexer::tokens(const QString& code)
{
    return tokens( code.toLatin1() );
}

QList<Token> Lexer::tokens(const QByteArray& code, const QString& path)
{
    QBuffer in;
    in.setData( code );
    in.open(QIODevice::ReadOnly);
    setStream( &in, path );

    QList<Token> res;
    Token t = nextToken();
    while( t.isValid() )
    {
        res << t;
        t = nextToken();
    }
    return res;
}

QByteArray Lexer::getSymbol(const QByteArray& str)
{
    if( str.isEmpty() )
        return str;
    QByteArray& sym = d_symbols[str];
    if( sym.isEmpty() )
        sym = str;
    return sym;
}

void Lexer::clearSymbols()
{
    d_symbols.clear();
}

bool Lexer::isValidIdent(const QByteArray& id)
{
    if( id.isEmpty() )
        return false;
    if( !::isalpha(id[0]) && id[0] != '_' )
        return false;
    for( int i = 1; i < id.size(); i++ )
    {
        if( !::isalnum(id[i]) && id[i] != '_' )
            return false;
    }
    return true;
}

static inline bool isCharacter(char ch, bool withDigit = true )
{
    return ( withDigit ? ::isalnum(ch) : ::isalpha(ch) ) || ch == '_' || ch == '$' || ch == '\\';
}

Token Lexer::nextTokenImp()
{
    if( d_in == 0 )
        return token(Tok_Eof);
    skipWhiteSpace();

    while( d_colNr >= d_line.size() )
    {
        if( d_in->atEnd() )
        {
            Token t = token( Tok_Eof, 0 );
            if( d_in->parent() == this )
                d_in->deleteLater();
            return t;
        }
        nextLine();
        skipWhiteSpace();
    }
    Q_ASSERT( d_colNr < d_line.size() );
    while( d_colNr < d_line.size() )
    {
        const char ch = quint8(d_line[d_colNr]);

        if( ch == '"' || ch == '\'' )
            return string();
        else if( ::isalpha(ch) || ch == '_' )
            return ident();
        else if( ::isdigit(ch) )
            return number();
        else if( ch == '.' && ::isdigit(lookAhead(1) ) )
            return number();
        else if( ch == '-' && lookAhead(1) == '-' )
            return comment();
        else if( ch == '[' && ( lookAhead(1) == '[' || lookAhead(1) == '=' ) )
            return longstring();

        // else
        int pos = d_colNr;
        TokenType tt = tokenTypeFromString(d_line,&pos);

        if( tt == Tok_Invalid || pos == d_colNr )
            return token( Tok_Invalid, 1, QString("unexpected character '%1' %2").arg(char(ch)).arg(int(ch)).toUtf8() );
        else {
            const int len = pos - d_colNr;
            return token( tt, len, d_line.mid(d_colNr,len) );
        }
    }
    Q_ASSERT(false);
    return token(Tok_Invalid);
}

int Lexer::skipWhiteSpace()
{
    const int colNr = d_colNr;
    while( d_colNr < d_line.size() && ::isspace( d_line[d_colNr] ) )
        d_colNr++;
    return d_colNr - colNr;
}

void Lexer::nextLine()
{
    d_colNr = 0;
    d_lineNr++;
    d_line = d_in->readLine();

    if( d_line.endsWith("\r\n") )
        d_line.chop(2);
    else if( d_line.endsWith('\n') || d_line.endsWith('\r') || d_line.endsWith('\025') )
        d_line.chop(1);
}

int Lexer::lookAhead(int off) const
{
    if( int( d_colNr + off ) < d_line.size() )
    {
        return d_line[ d_colNr + off ];
    }else
        return 0;
}

Token Lexer::token(TokenType tt, int len, const QByteArray& val)
{
    QByteArray v = val;
    if( tt == Tok_Name )
        v = getSymbol(v);
    Token t( tt, d_lineNr, d_colNr + 1, len, v );
    d_lastToken = t;
    d_colNr += len;
    t.d_sourcePath = d_sourcePath; // sourcePath is symbol too
    if( tt == Tok_Invalid && d_err != 0 )
        d_err->error(Errors::Syntax, t.d_sourcePath, t.d_lineNr, t.d_colNr, t.d_val );
    return t;
}

static inline bool isOnlyDecimalDigit( const QByteArray& str )
{
    for( int i = 0; i < str.size(); i++ )
    {
        if( !::isdigit(str[i]) )
            return false;
    }
    return true;
}

Token Lexer::ident()
{
    int off = 1;
    while( true )
    {
        const char c = lookAhead(off);
        if( !::isalnum(c) && c != '_' )
            break;
        else
            off++;
    }
    const QByteArray str = d_line.mid(d_colNr, off );
    Q_ASSERT( !str.isEmpty() );

    int pos = 0;
    TokenType t = tokenTypeFromString( str, &pos );
    if( t != Tok_Invalid && pos != str.size() )
        t = Tok_Invalid;

    if( t != Tok_Invalid )
        return token( t, off );
    else
        return token( Tok_Name, off, str );
}

static inline bool isHexDigit( char c )
{
    return ::isdigit(c) || c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'E' || c == 'F'
            || c == 'a' || c == 'b' || c == 'c' || c == 'd' || c == 'e' || c == 'f';
}

Token Lexer::number()
{
    // integer ::= // digit {digit}
    // hex ::= '0x' hexDigit {hexDigit}
    // real ::= // digit {digit} '.' {digit} [ScaleFactor]
    // ScaleFactor- ::= // 'e' ['+' | '-'] digit {digit}
    int off = 1;
    while( true )
    {
        const char c = lookAhead(off);
        if( !::isdigit(c) )
            break;
        else
            off++;
    }
    bool isReal = false;
    const char o1 = lookAhead(off);
    if( o1 == '.' || o1 == 'e' || o1 == 'E' )
    {
        isReal = true;
        if( !( o1 == 'e' || o1 == 'E' ) )
            off++;
        while( true )
        {
            const char c = lookAhead(off);
            if( !::isdigit(c) )
                break;
            else
                off++;
        }
        if( lookAhead(off) == 'e' || lookAhead(off) == 'E' )
        {
            off++;
            char o = lookAhead(off);
            if( o == '+' || o == '-' )
            {
                off++;
                o = lookAhead(off);
            }
            if( !::isdigit(o) )
                return token( Tok_Invalid, off, "invalid real" );
            while( true )
            {
                const char c = lookAhead(off);
                if( !::isdigit(c) )
                    break;
                else
                    off++;
            }
        }
    }else if( o1 == 'x' )
    {
        if( d_line[d_colNr+off-1] != '0' )
            return token( Tok_Invalid, off, "invalid hex number" );
        off++;
        while( true )
        {
            const char c = lookAhead(off);
            if( !isHexDigit(c) )
                break;
            else
                off++;
        }
    }
    QByteArray str = d_line.mid(d_colNr, off );
    Q_ASSERT( !str.isEmpty() );

    return token( Tok_Number, off, str );
}

Token Lexer::comment()
{
    if( lookAhead(2) != '[')
        return token( Tok_Comment, d_line.size() - d_colNr, d_line.mid(d_colNr + 2).trimmed() ); // line comment

    QByteArray endToken; // --[===[
    if( lookAhead(3) == '[' )
        endToken = "]]";
    else
    {
        const int firstEq = 3;
        int off = firstEq;
        if( lookAhead(off) != '=' )
            return token( Tok_Comment, d_line.size() - d_colNr, d_line.mid(d_colNr + 2).trimmed() ); // still a line comment
        while( lookAhead(off) == '=' )
            off++;
        if( lookAhead(off) != '[' )
            return token( Tok_Invalid, off, "invalid block comment" );
        endToken = "]" + QByteArray(off-firstEq,'=') + "]";
    }

    const int startLine = d_lineNr;
    const int startCol = d_colNr;


    int pos = -1;
    QByteArray str;
    while( pos == -1 && !d_in->atEnd() )
    {
        pos = d_line.indexOf(endToken,d_colNr);
        if( pos != -1 )
        {
            pos += endToken.size();
            if( !str.isEmpty() )
                str += '\n';
            str += d_line.mid(d_colNr,pos-d_colNr);
            break;
        }else
        {
            if( !str.isEmpty() )
                str += '\n';
            str += d_line.mid(d_colNr);
        }
        nextLine();
    }
    if( d_packComments && pos == -1 && d_in->atEnd() )
    {
        d_colNr = d_line.size();
        Token t( Tok_Invalid, startLine, startCol + 1, str.size(), tr("non-terminated comment").toLatin1() );
        if( d_err )
            d_err->error(Errors::Syntax, t.d_sourcePath, t.d_lineNr, t.d_colNr, t.d_val );
        return t;
    }
    // Col + 1 weil wir immer bei Spalte 1 beginnen, nicht bei Spalte 0
    Token t( ( d_packComments ? Tok_Comment : Tok_2MinusLbrack ), startLine, startCol + 1, str.size(), str );
    t.d_sourcePath = d_sourcePath;
    d_lastToken = t;
    d_colNr = pos;
    if( !d_packComments )
    {
        Token t(Tok_Rbrack2Minus,d_lineNr, pos - 3 + 1, 3 );
        t.d_sourcePath = d_sourcePath;
        d_lastToken = t;
        d_buffer.append( t );
    }
    return t;
}

Token Lexer::string()
{
    const int startLine = d_lineNr;
    const int startCol = d_colNr;
    const char other = lookAhead(0);
    int off = 1;
    QByteArray str;
    while( true )
    {
        const char c = lookAhead(off);
        off++;
        if( c == '\\' )
        {
            if( lookAhead(off) == 0 ) // \ + end of line
            {
                str += d_line.mid(d_colNr, off );
                str += "\n";
                nextLine();
                off = 0;
            }else
                off++; // normal escape
        }else if( c == other )
        {
            str += d_line.mid(d_colNr, off );
            break;
        }else if( c == 0 )
            return token( Tok_Invalid, off, "non-terminated string" );
    }
    Token t( Tok_String, startLine, startCol, str.size(), str );
    t.d_sourcePath = d_sourcePath;
    d_lastToken = t;
    t.d_len += 1;
    d_colNr += off;
    return t;
}

Token Lexer::longstring()
{
    const int startLine = d_lineNr;
    const int startCol = d_colNr;

    QByteArray endToken;
    if( lookAhead(1) == '[' )
        endToken = "]]";
    else
    {
        const int firstEq = 1;
        int off = firstEq;
        Q_ASSERT( lookAhead(off) == '=' );
        while( lookAhead(off) == '=' )
            off++;
        if( lookAhead(off) != '[' )
            return token( Tok_Invalid, off, "invalid long string" );
        endToken = "]" + QByteArray(off-firstEq,'=') + "]";
    }

    int pos = d_line.indexOf( endToken, d_colNr + 1 );
    QByteArray str;
    while( pos == -1 && !d_in->atEnd() )
    {
        if( !str.isEmpty() )
            str += '\n';
        str += d_line.mid( d_colNr );
        nextLine();
        pos = d_line.indexOf( endToken );
    }
    if( pos == -1 )
    {
        d_colNr = d_line.size();
        Token t( Tok_Invalid, startLine, startCol + 1, str.size(), tr("non-terminated hexadecimal string").toLatin1() );
        return t;
    }
    // else
    pos += endToken.size(); // konsumiere endToken
    if( !str.isEmpty() )
        str += '\n';
    str += d_line.mid( d_colNr, pos - d_colNr );

    Token t( Tok_String, startLine, startCol, str.size(), str );
    t.d_sourcePath = d_sourcePath;
    d_lastToken = t;
    t.d_len += 1;
    d_colNr = pos;
    return t;
}

