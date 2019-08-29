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

#include "LuaHighlighter.h"
//#include <Lua.h>
#include <QtDebug>
using namespace Lua;

Highlighter::Highlighter(QTextDocument *parent) :
    QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    // Die Regeln werden in der hier gegebenen Reihenfolge abgearbeitet
    d_commentFormat.setProperty( TokenProp, Comment );
    d_commentFormat.setForeground(Qt::darkGreen);
	// Das wird neu unten gemacht mit den Multiline-Formaten
//    rule.pattern = QRegExp("--+[^\n]*");
//    rule.format = d_commentFormat;
//    rule.name = "Single Line Comment";
//    d_rules.append(rule);

    d_literalFormat.setProperty( TokenProp, LiteralString );
    d_literalFormat.setForeground(Qt::darkRed);
    // Quelle: http://stackoverflow.com/questions/481282/how-can-i-match-double-quoted-strings-with-escaped-double-quote-characters
	rule.pattern = QRegExp( "\"(?:[^\\\\\"]|\\\\.)*\"" ); // TODO: verhindern, dass '"'abc'"' als String "'abc'" interpretiert wird!
    rule.pattern.setMinimal(true);
    rule.name = "Double Quote String";
    rule.format = d_literalFormat;
    d_rules.append(rule);
	rule.pattern = QRegExp( "'(?:[^\\\\']|\\\\.)*'" );
    rule.pattern.setMinimal(true);
    rule.name = "Single Quote String";
    d_rules.append(rule);

    QTextCharFormat keywordFormat;
    keywordFormat.setProperty( TokenProp, Keyword );
    keywordFormat.setForeground(QColor(0x00,0x00,0x7f));
    keywordFormat.setFontWeight(QFont::Bold);
    QStringList keywordPatterns;
    keywordPatterns << "\\band\\b" << "\\bbreak\\b" << "\\bdo\\b"
                    << "\\belse\\b" << "\\belseif\\b" << "\\bend\\b"
                    << "\\bfalse\\b" << "\\bfor\\b" << "\\bfunction\\b"
                    << "\\bif\\b" << "\\bin\\b" << "\\blocal\\b"
                    << "\\bnil\\b" << "\\bnot\\b" << "\\bor\\b"
                    << "\\brepeat\\b" << "\\breturn\\b" << "\\bthen\\b"
                    << "\\btrue\\b" << "\\buntil\\b" << "\\bwhile\\b";
    foreach (const QString &pattern, keywordPatterns)
    {
        rule.pattern = QRegExp(pattern);
        rule.format = keywordFormat;
        rule.name = "Keyword " + pattern.toUtf8();
        d_rules.append(rule);
    }

    QTextCharFormat numbers;
    numbers.setProperty( TokenProp, Number );
    numbers.setForeground(Qt::red);
    // 0xff   0x56
    rule.pattern = QRegExp("(^[a-zA-Z_]|\\b)0x[0-9a-fA-F]+" );
    // (^[a-zA-Z_]|\\b) heisst, dass 0x nicht Teil eines Idents sein darf
    rule.format = numbers;
    rule.name = "Number";
    d_rules.append(rule);

    QTextCharFormat idents; // Ist nötig, damit Idents der Form "abc123" nicht als Zahl interpretiert werden
    idents.setProperty( TokenProp, Ident );
    idents.setForeground(Qt::black);
	rule.pattern = QRegExp("(\\b)[a-zA-Z_][a-zA-Z0-9_]*");
	// ([^0-9]|\\b) heisst, entweder ist der Ident am Anfang der Zeile oder nicht unmittelbar nach Zahl.
    // Damit verhindern wir, dass 314.16e-2 das "e" als Ident verwendet wird.
	// Issue: "[^0-9]|\\b" or "[^0-9]" also detect on idents like ".b"
    rule.format = idents;
    rule.name = "Ident";
    d_rules.append(rule);

    // 3   3.0    3.1416   314.16e-2   0.31416E1   12E2   .123 .16e-2 .31416E1
    rule.pattern = QRegExp("[0-9]*[\\.]?[0-9]+([eE][-+]?[0-9]+)?" );
    rule.format = numbers;
    rule.name = "Number";
    d_rules.append(rule);

    keywordFormat.setProperty( TokenProp, Other );
    QStringList otherTokens; // Zuerst die langen, dann die kurzen
    otherTokens <<  "\\.\\.\\." << "\\.\\." << "==" << "~=" << "<=" << ">=" <<
                    "\\*" << "/" << "%" << "\\^" << "#" << "<" << ">" << "=" <<
					"\\(" << "\\)" << "\\{" << "\\}" << "\\[" << "\\]" <<  ";" << ":" << "," <<
                    "\\+" << "-" << "\\.";
    foreach (const QString &pattern, otherTokens)
    {
        rule.pattern = QRegExp(pattern);
        rule.format = keywordFormat;
        rule.name = "Token " + pattern.toUtf8();
        d_rules.append(rule);
    }
}

static inline void empty( QString& str, int start, int len )
{
    // Kostet fast nichts, da kein dynamischer Speicher angelegt oder freigegeben wird
    for( int i = start; i < ( start + len ); i++ )
        str[i] = QChar(' ');
}

struct Mark
{
	enum Kind {
		LineCmt,      // --
		StartMlCmt,   // --[[ or --[=[ mit d_num Anz. =
		StartMlStr,   // [[ or [=[ mit d_num Anz. =
		EndMlStrOrCmt,// ]] or ]=] mit d_num Anz. =
		Done
	};
	int d_pos;     // Position in text
	quint8 d_num; // Anzahl Gleichheitszeichen
	quint8 d_kind; // Kind
	int len() const
	{
		switch( d_kind )
		{
		case LineCmt:
			return 2;
		case StartMlCmt:
			return 2 + d_num + 2;
		case StartMlStr:
		case EndMlStrOrCmt:
			return d_num + 2;
		}
		return 0;
	}
	Mark():d_pos(-1),d_num(0),d_kind(Done){}
};
typedef QList<Mark> Marks;

static Mark _nextMark2( const QString& text, int from = 0 )
{
	// Suche nach "--", "--[[", "--[=[", "[[", "[=[", "]]", "]=]"
    Mark res;
    for( int i = from; i < text.size(); i++ )
    {
        const QChar c = text[i];
		if( c == QChar('-') && i + 1 < text.size() && text[i+1] == QChar('-') )
        {
			// "--" found
			res.d_kind = Mark::LineCmt;
			res.d_pos = i;
			if( i + 3 < text.size() && text[i+2] == QChar('[') )
			{
				// "--[" found
				if( text[i+3] == QChar('=') )
				{
					int j = i+4;
					while( j < text.size() && text[j] == QChar('=') )
						j++;
					if( j < text.size() && text[j] == QChar('[') )
					{
						// "--[=[" found
						res.d_kind = Mark::StartMlCmt;
						res.d_num = j - ( i + 2 ) - 1;
						return res;
					}
				}else if( text[i+3] == QChar('[') )
				{
					// "--[[" found
					res.d_kind = Mark::StartMlCmt;
					return res;
				}
			}
			return res; // es ist in jedem Fall ein Single Line Comment, wenn man hier ankommt
		}else if( c == QChar(']') && i + 1 < text.size() )
        {
			if( text[i+1] == QChar('=') )
            {
                int j = i+2;
                while( j < text.size() && text[j] == QChar('=') )
                    j++;
                if( j < text.size() && text[j] == QChar(']') )
                {
					// "]=]" found
					res.d_kind = Mark::EndMlStrOrCmt;
                    res.d_pos = i;
					res.d_num = j - i - 1;
                    return res;
                }
			}else if( text[i+1] == QChar(']') )
            {
				// "]]" found
				res.d_kind = Mark::EndMlStrOrCmt;
				res.d_pos = i;
                return res;
            }
		}else if( c == QChar('[') && i + 1 < text.size() )
        {
			if( text[i+1] == QChar('=') ) // "[=" found
            {
                int j = i+2;
                while( j < text.size() && text[j] == QChar('=') )
                    j++;
                if( j < text.size() && text[j] == QChar('[') )
                {
					// "[=[" found
					res.d_kind = Mark::StartMlStr;
					res.d_pos = i;
					res.d_num = j - i - 1;
                    return res;
                }
			}else if( text[i+1] == QChar('[') ) // "[[" found
            {
				res.d_kind = Mark::StartMlStr;
				res.d_pos = i;
                return res;
			}
        }
    }
    return res;
}

static Marks _findMarks( const QString& text )
{
    Marks marks; // alle Marks der Zeile
    Mark pos = _nextMark2( text );
	while( pos.d_kind != Mark::Done )
    {
        marks.append(pos);
        pos = _nextMark2( text, pos.d_pos + pos.len() );
    }
    return marks;
}

union BlockState
{
    int d_int; // Initialwert ist -1, was 0xffffffff entspricht
    struct Data
    {
        unsigned int startOfComment:1; // Auf der Zeile beginnt ein Kommentar, der dort nicht endet
		unsigned int endOfStrOrCmnt:1; // Auf der Zeile endet ein Kommentar oder String, der dort nicht beginnt
        unsigned int allLineComment:1; // Die ganze Zeile gehört zu einem Kommentar, der darüber beginnt und darunter endet
        unsigned int startOfString:1;
        unsigned int allLineString:1;
		unsigned int level:8;    // Anz. "="
		unsigned int dummy:18;
        unsigned int unitialized:1;
    } d_state;
};

void Highlighter::highlightBlock(const QString & block)
{
    QString text = block; // wir machen Kopie, damit wir die geparsten Stellen rauslöschen können

    BlockState prev;
    prev.d_int = previousBlockState();
    if( prev.d_state.unitialized )
        prev.d_int = 0;
    BlockState newCur;
    newCur.d_int = 0;

    Marks marks = _findMarks( text );
    int marksDone = 0;
    if( prev.d_state.startOfComment || prev.d_state.allLineComment )
    {   // wir sind in einem Kommentar drin
        // prüfe, ob er hier endet; suche das erste End; alle davor liegenden Starts und anderen Marks werden ignoriert
        for( int i = 0; i < marks.size(); i++ )
        {
			if( marks[i].d_kind == Mark::EndMlStrOrCmt && marks[i].d_num == prev.d_state.level )
            {   // wir sind auf ein End gestossen
                marksDone = i + 1;
				newCur.d_state.endOfStrOrCmnt = true;
				newCur.d_state.level = prev.d_state.level;
				stamp( text, 0, marks[i].d_pos + marks[i].len(), d_commentFormat );
                break;
            }
        }
        if( marksDone == 0 )
        {   // keine wirksamen Comment Marks gefunden; die ganze Zeile ist auch ein Kommentar
            newCur.d_state.allLineComment = true;
			newCur.d_state.level = prev.d_state.level;
			stamp( text, 0, text.size(), d_commentFormat );
            marksDone = marks.size();
        }
    }else if( prev.d_state.startOfString || prev.d_state.allLineString )
    {
        // wir sind in einem String drin
        // prüfe, ob er hier endet; suche das erste End; alle davor liegenden Starts und anderen Marks werden ignoriert
        for( int i = 0; i < marks.size(); i++ )
        {
			if( marks[i].d_kind == Mark::EndMlStrOrCmt && marks[i].d_num == prev.d_state.level )
            {   // wir sind auf ein End gestossen
                marksDone = i + 1;
				newCur.d_state.endOfStrOrCmnt = true;
				newCur.d_state.level = prev.d_state.level;
                stamp( text, 0, marks[i].d_pos + marks[i].len(), d_literalFormat );
                break;
            }
        }
        if( marksDone == 0 )
        {   // keine wirksamen String Marks gefunden; die ganze Zeile ist auch ein String
            newCur.d_state.allLineString = true;
			newCur.d_state.level = prev.d_state.level;
            stamp( text, 0, text.size(), d_literalFormat );
            marksDone = marks.size();
        }
    }
	// Suche ganze LineCmt oder Ml-Paare
    for( int i = marksDone; i < marks.size(); i++ )
    {
		if( marks[i].d_kind == Mark::LineCmt )
		{
			stamp( text, marks[i].d_pos, text.size() - marks[i].d_pos, d_commentFormat );
			marksDone = marks.size();
			break;
		}else if( marks[i].d_kind == Mark::StartMlCmt || marks[i].d_kind == Mark::StartMlStr )
        {
			for( int j = i + 1; j < marks.size(); j++ )
            {
				if( marks[j].d_num == marks[i].d_num )
                {
                    stamp( text, marks[i].d_pos, marks[j].d_pos - marks[i].d_pos + marks[j].len(),
						   (marks[i].d_kind == Mark::StartMlCmt)?d_commentFormat:d_literalFormat );
					marks[i].d_kind = Mark::Done;
					marks[j].d_kind = Mark::Done; // als gesehen markieren
                }
            }
        }
    }
    // Suche offene Enden
    for( int i = marksDone; i < marks.size(); i++ )
    {
		if( marks[i].d_kind == Mark::StartMlCmt || marks[i].d_kind == Mark::StartMlStr )
        {
			if( marks[i].d_kind == Mark::StartMlCmt )
                newCur.d_state.startOfComment = true;
            else
                newCur.d_state.startOfString = true;
			newCur.d_state.level = marks[i].d_num;
			stamp( text, marks[i].d_pos, text.size() - marks[i].d_pos,
				   (marks[i].d_kind == Mark::StartMlCmt)?d_commentFormat:d_literalFormat );
        }
    }
    setCurrentBlockState( newCur.d_int );

	//qDebug() << "**********";
    foreach( const HighlightingRule &rule, d_rules )
    {
        QRegExp expression(rule.pattern);
        int index = expression.indexIn(text);
        while( index >= 0 )
        {
            const int length = expression.matchedLength();
			//qDebug() << "hit" << rule.name << ":" << text.mid( index, length );
            stamp( text, index, length, rule.format );
            index = expression.indexIn(text, index + length);
        }
    }
}

void Highlighter::stamp(QString &text, int start, int len, const QTextCharFormat &f)
{
    setFormat(start, len, f );
    // Vermeide, dass mehrere Regeln auf denselben Text angewendet werden
    empty( text, start, len );
	// qDebug() << "empty:" << text;
}

QString Highlighter::format(int tokenType)
{
    switch( tokenType )
    {
    case Ident:
        return tr("Ident");
    case Keyword:
        return tr("Keyword");
    case Number:
        return tr("Number");
    case LiteralString:
        return tr("String");
    case Comment:
        return tr("Comment");
    case Other:
        return tr("Other");
    default:
        return QString();
    }
}
