#ifndef LUALEXER_H
#define LUALEXER_H

/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
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

#include <QObject>
#include <LjTools/LuaToken.h>
#include <QHash>
#include <QRegExp>

class QIODevice;

namespace Ljas
{
class Errors;
class FileCache;
}

namespace Lua
{
    class Lexer : public QObject
    {
    public:
        explicit Lexer(QObject *parent = 0);

        void setStream( QIODevice*, const QString& sourcePath );
        bool setStream(const QString& sourcePath);
        void setErrors(Ljas::Errors* p) { d_err = p; }
        void setCache(Ljas::FileCache* p) { d_fcache = p; }
        void setIgnoreComments( bool b ) { d_ignoreComments = b; }
        void setPackComments( bool b ) { d_packComments = b; }

        Token nextToken();
        Token peekToken(quint8 lookAhead = 1);
        QList<Token> tokens( const QString& code );
        QList<Token> tokens( const QByteArray& code, const QString& path = QString() );
        static QByteArray getSymbol( const QByteArray& );
        static void clearSymbols();
        static bool isValidIdent( const QByteArray& );
    protected:
        Token nextTokenImp();
        int skipWhiteSpace();
        void nextLine();
        int lookAhead(int off = 1) const;
        Token token(TokenType tt, int len = 1, const QByteArray &val = QByteArray());
        Token ident();
        Token number();
        Token comment();
        Token string();
        Token longstring();
    private:
        QIODevice* d_in;
        Ljas::Errors* d_err;
        Ljas::FileCache* d_fcache;
        QRegExp d_real;
        quint32 d_lineNr;
        quint16 d_colNr;
        QByteArray d_sourcePath;
        QByteArray d_line;
        QList<Token> d_buffer;
        static QHash<QByteArray,QByteArray> d_symbols;
        Token d_lastToken;
        bool d_ignoreComments;  // don't deliver comment tokens
        bool d_packComments;    // Only deliver one Tok_Comment for /**/ instead of Tok_Lcmt and Tok_Rcmt
    };
}

#endif // LUALEXER_H
