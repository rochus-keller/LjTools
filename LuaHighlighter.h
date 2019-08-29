/*
 * Copyright 2000-2019 Rochus Keller <mailto:rkeller@nmr.ch>
 *
 * This file is part of the CARA (Computer Aided Resonance Assignment,
 * see <http://cara.nmr.ch/>) NMR Application Framework (NAF) library.
 *
 * The following is the license that applies to this copy of the
 * library. For a license to use the library under conditions
 * other than those described here, please email to rkeller@nmr.ch.
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

#ifndef LUASYNTAXHIGHLIGHTER_H
#define LUASYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>

namespace Lua
{
    class Highlighter : public QSyntaxHighlighter
    {
        Q_OBJECT
    public:
        enum { TokenProp = QTextFormat::UserProperty };
        enum TokenType { UnknownToken = 0, Ident, Keyword, Number, LiteralString, Comment, Other };
        static QString format( int tokenType );
        explicit Highlighter(QTextDocument *parent = 0);
    protected:
        void highlightBlock(const QString &text);
        void stamp( QString& text, int start, int len, const QTextCharFormat& f );
    private:
        enum { Idle, InComment, InLiteral };
        struct HighlightingRule
        {
            QRegExp pattern;
            QTextCharFormat format;
            QByteArray name;
        };
        QVector<HighlightingRule> d_rules;

        QTextCharFormat d_commentFormat;
        QTextCharFormat d_literalFormat;
   };
}

#endif // LUASYNTAXHIGHLIGHTER_H
