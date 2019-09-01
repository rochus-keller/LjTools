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

#include "Terminal2.h"
#include "ExpressionParser.h"
#include <QStatusBar>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QPrinter>
#include <QFileDialog>
#include <QFile>
#include <QShortcut>
#include <QtDebug>
#include <QMessageBox>
#include <QFontDialog>
#include <QSettings>
#include <GuiTools/AutoMenu.h>
#include <QProcess>
#include <lua.hpp>
using namespace Lua;

static QTextCharFormat s_pf;
static QTextCharFormat s_luaf;
static QTextCharFormat s_errf;
static QTextCharFormat s_outf;
static const char* s_prompt = "Lua> ";

Terminal2::Terminal2(QWidget* parent, Lua::Engine2* l):
	QTextEdit( parent ), d_lua( l )
{
	if( d_lua == 0 )
		d_lua = Lua::Engine2::getInst();
	Q_ASSERT( d_lua != 0 );

	QFont font;
	font.setPointSize(9);
#ifdef Q_WS_X11
	font.setFamily("Courier");
#else
	font.setStyleHint( QFont::TypeWriter );
#endif
	QSettings set;
	updateFont(set.value( "Terminal/Font", QVariant::fromValue( font ) ).value<QFont>());

	QTextCursor cur = textCursor();
    cur.insertText( prompt(), s_pf );
	d_out = QTextCursor( document() );
	
	setFocusPolicy( Qt::StrongFocus );
	setReadOnly( true );
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    Gui::AutoMenu* pop = new Gui::AutoMenu( this, true );
    pop->addCommand( "Copy", this, SLOT(handleCopy()) );
    pop->addCommand( "Paste", this, SLOT(handlePaste()) );
	pop->addSeparator();
    pop->addCommand( "Select All", this, SLOT(handleSelectAll()) );
	pop->addCommand( "Clear", this, SLOT(handleDelete()) );
	pop->addSeparator();
    pop->addCommand( "Export PDF...", this, SLOT(handleExportPdf()) );
    pop->addCommand( "Save Log...", this, SLOT(handleSaveAs()) );
	pop->addSeparator();
	pop->addCommand( "Set Font...", this, SLOT(handleSetFont()) );

	new QShortcut( tr("F12"), this, SLOT(handlePrintStack()) );

    connect( d_lua, SIGNAL(onNotify(int,QByteArray,int)), this, SLOT(onNotify(int,QByteArray,int)) );
	printText( QString("%1 %2").arg(LUA_RELEASE).arg(LUA_COPYRIGHT) );
    printText( QString("%1 -- %2. %3").arg(LUAJIT_VERSION).arg(LUAJIT_COPYRIGHT).arg(LUAJIT_URL) );
    printJitInfo();
    printText( tr("%3 %1 (c) 2019 %2").arg(qApp->applicationVersion()).
               arg(qApp->organizationName()).arg(qApp->applicationName()));
}

void Terminal2::printText(const QString & str, bool err )
{
    d_out.insertText( str, err ? s_errf : s_outf );
	d_out.insertText( QString( QChar::ParagraphSeparator ), s_pf );
	moveCursor( QTextCursor::End );
}

Terminal2::~Terminal2()
{
}

void Terminal2::keyPressEvent(QKeyEvent *e)
{
	switch( e->key() )
	{
	case Qt::Key_Return:
	case Qt::Key_Enter:
		{
            moveCursor( QTextCursor::End );
			QTextCursor cur = textCursor();
			cur.insertText( QString( QChar::ParagraphSeparator ), s_pf );
			d_out.setPosition( cur.position() );
			const int old = cur.position();
			cur.insertText( prompt(), s_pf );
			d_out.setPosition( old );
			if( !d_line.isEmpty() )
				d_histo.append( d_line );
			d_next.clear();
			if( d_lua->isWaiting() )
            {
                ExpressionParser p;
                if( p.parseAndPrint( d_line.toLatin1(), d_lua, false ) )
                    p.executeAndPrint( d_lua );
            }else
                d_lua->executeCmd( d_line.toLatin1(), "Terminal" );
			d_line.clear();
		}
		return;
	case Qt::Key_Backspace:
		if( !d_line.isEmpty() )
		{
			moveCursor( QTextCursor::End );
			QTextCursor cur = textCursor();
			cur.deletePreviousChar();
			d_line.truncate( d_line.length() - 1 );
		}
		return;
	case Qt::Key_Up:
		if( !d_histo.isEmpty() )
		{
			moveCursor( QTextCursor::End );
			QTextCursor cur = textCursor();
			cur.setPosition( cur.position() - d_line.size(), QTextCursor::KeepAnchor );
			d_next.append( d_line );
			d_line = d_histo.back();
			d_histo.pop_back();
			cur.insertText( d_line, s_luaf );
		}
		break;
	case Qt::Key_Down:
		if( !d_next.isEmpty() )
		{
			moveCursor( QTextCursor::End );
			QTextCursor cur = textCursor();
			cur.setPosition( cur.position() - d_line.size(), QTextCursor::KeepAnchor );
			d_histo.append( d_line );
			d_line = d_next.back();
			d_next.pop_back();
			cur.insertText( d_line, s_luaf );
		}
		break;
	}
	bool ctrl = ( e->modifiers() == Qt::ControlModifier );
	if( ctrl )
	{
		switch( e->key() )
		{
		case Qt::Key_V:
			paste();
			return;
		case Qt::Key_C:
			copy();
			return;
		}
	}else if( !e->text().isEmpty() )
	{
		moveCursor( QTextCursor::End );
		QTextCursor cur = textCursor();
		cur.insertText( e->text(), s_luaf );
		d_line += e->text();
	}else
        e->ignore();
}

void Terminal2::inputMethodEvent(QInputMethodEvent * e)
{
    // Auf Linux ist diese Methode nötig für Ü, Ö, etc.
    // Das ist eine sehr vereinfachte Methode. Für eine vollständige Behandlung siehe
    // QTextControlPrivate::inputMethodEvent in Qt/src/qui/text/qtextcontrol.cpp
    QString text = e->commitString();

    if (!text.isEmpty() &&
        (text.at(0).isPrint() || text.at(0) == QLatin1Char('\t')))
    {
        moveCursor( QTextCursor::End );
        QTextCursor cur = textCursor();
        cur.insertText( text, s_luaf );
        d_line += text;
        e->accept();
    }else
        e->ignore();
}

QString Terminal2::prompt() const
{
	if( d_lua->isWaiting() )
        return "Exp>";
    else
		return s_prompt;
}

void Terminal2::updateFont(const QFont & f)
{
	s_pf.setFontFamily( f.family() );
	s_pf.setFontPointSize( f.pointSizeF() );
	s_pf.setFontWeight( QFont::Bold );
	s_luaf.setFontFamily( f.family() );
	s_luaf.setFontPointSize( f.pointSizeF() );
	s_luaf.setFontWeight( QFont::Normal );
	s_errf.setFontFamily( f.family() );
	s_errf.setFontPointSize( f.pointSizeF() );
	s_errf.setFontWeight( QFont::Normal );
	s_errf.setForeground( Qt::red );
	s_outf.setFontFamily( f.family() );
	s_outf.setFontPointSize( f.pointSizeF() );
	s_outf.setFontWeight( QFont::Normal );
    s_outf.setForeground( Qt::blue );
}

void Terminal2::printJitInfo()
{
    int n;
    const char *s;
    lua_State* L = d_lua->getCtx();
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, -1, "jit");  /* Get jit.* module table. */
    lua_remove(L, -2);
    lua_getfield(L, -1, "status");
    lua_remove(L, -2);
    n = lua_gettop(L);
    lua_call(L, 0, LUA_MULTRET);
    QString str;
    QTextStream out(&str);
    out << (lua_toboolean(L, n) ? "JIT: ON" : "JIT: OFF");
    for (n++; (s = lua_tostring(L, n)); n++) {
        out << ' ' << s;
    }
    printText( str );
}

void Terminal2::onNotify(int messageType, QByteArray val1, int val2)
{
    switch( messageType )
    {
	case Engine2::Print:
		printText( val1 );
		break;
	case Engine2::Error:
        d_out.insertText( val1, s_errf );
        d_out.insertText( QString( QChar::ParagraphSeparator ), s_pf );
		moveCursor( QTextCursor::End );
        break;
	case Engine2::LineHit:
	case Engine2::BreakHit:
	case Engine2::Continued:
	case Engine2::Aborted:
		{
			moveCursor( QTextCursor::End );
			moveCursor( QTextCursor::StartOfLine, QTextCursor::KeepAnchor );
			QTextCursor cur = textCursor();
			cur.insertText( prompt(), s_pf );
			cur.movePosition(QTextCursor::StartOfLine);
			d_out.setPosition( cur.position() );
			d_line.clear();
        }
        break;
    case Engine2::Finished:
        foreach(const QByteArray& res, d_lua->getReturns() )
            printText(res);
        break;
    default:
        break;
    }
    //msg.consume();
	ensureCursorVisible();
}

void Terminal2::clear()
{
	setPlainText( "" );
	QTextCursor cur = textCursor();
    cur.insertText( prompt(), s_pf );
}

void Terminal2::paste()
{
	QString s = QApplication::clipboard()->text();
	moveCursor( QTextCursor::End );
	QTextCursor cur = textCursor();
	cur.insertText( s, s_luaf );
	d_line += s;
}

void Terminal2::handlePaste()
{
	QClipboard* cb = QApplication::clipboard();
    ENABLED_IF( !cb->text().isNull() );
	paste();
}
void Terminal2::handleCopy()
{ 
    ENABLED_IF( textCursor().hasSelection() );
	copy(); 
}
void Terminal2::handleSelectAll()
{
    ENABLED_IF( true );
	selectAll();
}
void Terminal2::handleDelete()
{
    ENABLED_IF( true );
	clear();
}

void Terminal2::handleExportPdf()
{
    ENABLED_IF( true );
	QPrinter p( QPrinter::HighResolution );
	p.setOutputFormat( QPrinter::PdfFormat );
	p.setCreator( tr("CARA") );
	p.setPrintProgram( tr("CARA") );
    p.setDocName( tr("CARA Lua Terminal2 Log") );
	
	QString path = QFileDialog::getSaveFileName( this, tr("Export to PDF" ), 
                                                 QString(), "*.pdf" );
	if( path.isEmpty() )
		return;
	p.setOutputFileName( path );
	print( &p );
}

void Terminal2::handleSaveAs()
{
    ENABLED_IF( true );

	QString path = QFileDialog::getSaveFileName( this, tr("Export to Text" ), 
                                                 QString(), "*.txt" );
	if( path.isEmpty() )
		return;
	QFile out(path);
	if( !out.open( QIODevice::WriteOnly ) )
	{
		QMessageBox::critical( this, tr("Export to Text"), tr("Cannot open file for writing") );
		return;
	}
	out.write( toPlainText().toLatin1() );
}

void Terminal2::handlePrintStack()
{
	// ENABLED_IF( true );
	QByteArray str;
	QTextStream out( &str, QIODevice::WriteOnly );
	const int top = lua_gettop( d_lua->getCtx() );
	out << "*** Lua Internal Stack:" << endl;
	if( top == 0 )
		out << "empty" << endl;
	else
	{
		for( int n = 1; n <= top; n++ )
		{
			out << "* Level " << n << ": (" << d_lua->getTypeName( n ) << ") " << d_lua->getValueString( n ) << endl;
		}
	}
	qDebug() << str;
	d_lua->print( str );
}

void Terminal2::handleSetFont()
{
	ENABLED_IF( true );

	bool ok;
	QFont res = QFontDialog::getFont( &ok, s_luaf.font(), this );
	if( !ok )
		return;
	QSettings set;
	set.setValue( "Terminal/Font", QVariant::fromValue(res) );
	set.sync();
	updateFont( res );
}
