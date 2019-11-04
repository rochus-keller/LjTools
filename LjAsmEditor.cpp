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

#include "LjAsmEditor.h"
#include "LjasHighlighter.h"
#include "Engine2.h"
#include "Terminal2.h"
#include "BcViewer.h"
#include "LjasParser.h"
#include "LuaJitEngine.h"
#include "LjasErrors.h"
#include "LjDisasm.h"
#include "LjAssembler.h"
#include <QtDebug>
#include <QDockWidget>
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QSettings>
#include <QShortcut>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QBuffer>
#include <GuiTools/AutoMenu.h>
#include <GuiTools/CodeEditor.h>
#include <GuiTools/AutoShortcut.h>
using namespace Lua;

static AsmEditor* s_this = 0;
static void report(QtMsgType type, const QString& message )
{
    if( s_this )
    {
        switch(type)
        {
        case QtDebugMsg:
            s_this->logMessage(QLatin1String("INF: ") + message);
            break;
        case QtWarningMsg:
            s_this->logMessage(QLatin1String("WRN: ") + message);
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            s_this->logMessage(QLatin1String("ERR: ") + message, true);
            break;
        }
    }
}
static QtMessageHandler s_oldHandler = 0;
void messageHander(QtMsgType type, const QMessageLogContext& ctx, const QString& message)
{
    if( s_oldHandler )
        s_oldHandler(type, ctx, message );
    report(type,message);
}

AsmEditor::AsmEditor(QWidget *parent)
    : QMainWindow(parent),d_lock(false)
{
    s_this = this;

    d_lua = new Engine2(this);
    d_lua->addStdLibs();
    d_lua->addLibrary(Engine2::PACKAGE);
    d_lua->addLibrary(Engine2::IO);
    d_lua->addLibrary(Engine2::DBG);
    d_lua->addLibrary(Engine2::BIT);
    d_lua->addLibrary(Engine2::JIT);
    d_lua->addLibrary(Engine2::OS);
    Engine2::setInst(d_lua);

    d_eng = new JitEngine(this);

    d_edit = new CodeEditor(this);
    new Ljas::Highlighter( d_edit->document() );
    d_edit->updateTabWidth();

    setDockNestingEnabled(true);
    setCorner( Qt::BottomRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::BottomLeftCorner, Qt::LeftDockWidgetArea );
    setCorner( Qt::TopRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::TopLeftCorner, Qt::LeftDockWidgetArea );

    createTerminal();
    createMenu();
    createDumpView();

    setCentralWidget(d_edit);

    s_oldHandler = qInstallMessageHandler(messageHander);

    QSettings s;

    if( s.value("Fullscreen").toBool() )
        showFullScreen();
    else
        showMaximized();

    const QVariant state = s.value( "DockState" );
    if( !state.isNull() )
        restoreState( state.toByteArray() );


    connect(d_edit, SIGNAL(modificationChanged(bool)), this, SLOT(onCaption()) );
    connect(d_edit,SIGNAL(cursorPositionChanged()),this,SLOT(onCursor()));
    connect(d_eng,SIGNAL(sigPrint(QString,bool)), d_term, SLOT(printText(QString,bool)) );
}

AsmEditor::~AsmEditor()
{

}

void AsmEditor::loadFile(const QString& path)
{
    d_edit->loadFromFile(path);
    QDir::setCurrent(QFileInfo(path).absolutePath());
    onCaption();

    // TEST
    compile();

}

void AsmEditor::logMessage(const QString& str, bool err)
{
    d_term->printText(str,err);
}

void AsmEditor::closeEvent(QCloseEvent* event)
{
    QSettings s;
    s.setValue( "DockState", saveState() );
    event->setAccepted(checkSaved( tr("Quit Application")));
}

void AsmEditor::createTerminal()
{
    QDockWidget* dock = new QDockWidget( tr("Terminal"), this );
    dock->setObjectName("Terminal");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable );
    d_term = new Terminal2(dock, d_lua);
    dock->setWidget(d_term);
    addDockWidget( Qt::BottomDockWidgetArea, dock );
}

void AsmEditor::createMenu()
{
    Gui::AutoMenu* pop = new Gui::AutoMenu( d_edit, true );
    pop->addCommand( "New", this, SLOT(onNew()), tr("CTRL+N"), false );
    pop->addCommand( "Open...", this, SLOT(onOpen()), tr("CTRL+O"), false );
    pop->addCommand( "Import from Lua...", this, SLOT(onImport()), tr("CTRL+I"), true );
    pop->addCommand( "Save", this, SLOT(onSave()), tr("CTRL+S"), false );
    pop->addCommand( "Save as...", this, SLOT(onSaveAs()) );
    pop->addSeparator();
    pop->addCommand( "Check syntax", this, SLOT(onParse()), tr("CTRL+T"), true );
    pop->addCommand( "Execute LuaJIT", this, SLOT(onRun()), tr("CTRL+E"), false );
    pop->addCommand( "Execute test VM", this, SLOT(onRun2()), tr("CTRL+SHIFT+E"), false );
    pop->addCommand( "Dump", this, SLOT(onDump()), tr("CTRL+D"), false );
    pop->addCommand( "Export binary...", this, SLOT(onExportBc()) );
    pop->addSeparator();
    pop->addCommand( "Undo", d_edit, SLOT(handleEditUndo()), tr("CTRL+Z"), true );
    pop->addCommand( "Redo", d_edit, SLOT(handleEditRedo()), tr("CTRL+Y"), true );
    pop->addSeparator();
    pop->addCommand( "Cut", d_edit, SLOT(handleEditCut()), tr("CTRL+X"), true );
    pop->addCommand( "Copy", d_edit, SLOT(handleEditCopy()), tr("CTRL+C"), true );
    pop->addCommand( "Paste", d_edit, SLOT(handleEditPaste()), tr("CTRL+V"), true );
    pop->addSeparator();
    pop->addCommand( "Find...", d_edit, SLOT(handleFind()), tr("CTRL+F"), true );
    pop->addCommand( "Find again", d_edit, SLOT(handleFindAgain()), tr("F3"), true );
    pop->addCommand( "Replace...", d_edit, SLOT(handleReplace()), tr("CTRL+R"), true );
    pop->addSeparator();
    pop->addCommand( "&Goto...", d_edit, SLOT(handleGoto()), tr("CTRL+G"), true );
    pop->addCommand( "Go Back", d_edit, SLOT(handleGoBack()), tr("ALT+Left"), true );
    pop->addCommand( "Go Forward", d_edit, SLOT(handleGoForward()), tr("ALT+Right"), true );
    pop->addSeparator();
    pop->addCommand( "Indent", d_edit, SLOT(handleIndent()) );
    pop->addCommand( "Unindent", d_edit, SLOT(handleUnindent()) );
    pop->addCommand( "Fix Indents", d_edit, SLOT(handleFixIndent()) );
    pop->addCommand( "Set Indentation Level...", d_edit, SLOT(handleSetIndent()) );
    pop->addSeparator();
    pop->addCommand( "Print...", d_edit, SLOT(handlePrint()), tr("CTRL+P"), true );
    pop->addCommand( "Export PDF...", d_edit, SLOT(handleExportPdf()), tr("CTRL+SHIFT+P"), true );
    pop->addSeparator();
    pop->addCommand( "Set &Font...", d_edit, SLOT(handleSetFont()) );
    pop->addCommand( "Show &Linenumbers", d_edit, SLOT(handleShowLinenumbers()) );
    pop->addCommand( "Show Fullscreen", this, SLOT(onFullScreen()) );
    pop->addSeparator();
    pop->addAction(tr("Quit"),qApp,SLOT(quit()), tr("CTRL+Q") );

    new QShortcut(tr("CTRL+Q"),this,SLOT(close()));
    new Gui::AutoShortcut( tr("CTRL+O"), this, this, SLOT(onOpen()) );
    new Gui::AutoShortcut( tr("CTRL+N"), this, this, SLOT(onNew()) );
    new Gui::AutoShortcut( tr("CTRL+O"), this, this, SLOT(onOpen()) );
    new Gui::AutoShortcut( tr("CTRL+S"), this, this, SLOT(onSave()) );
    new Gui::AutoShortcut( tr("CTRL+E"), this, this, SLOT(onRun()) );
    new Gui::AutoShortcut( tr("CTRL+SHIFT+E"), this, this, SLOT(onRun2()) );
    new Gui::AutoShortcut( tr("CTRL+D"), this, this, SLOT(onDump()) );
}

void AsmEditor::createDumpView()
{
    QDockWidget* dock = new QDockWidget( tr("Bytecode"), this );
    dock->setObjectName("Bytecode");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable );
    d_bcv = new BcViewer(dock);
    dock->setWidget(d_bcv);
    addDockWidget( Qt::RightDockWidgetArea, dock );
}

void AsmEditor::onDump()
{
    ENABLED_IF(true);

    if( compile() )
    {
        QDir dir( QStandardPaths::writableLocation(QStandardPaths::TempLocation) );
        const QString path = dir.absoluteFilePath(QDateTime::currentDateTime().toString("yyMMddhhmmsszzz")+".bc");
        QFile out(path);
        out.open(QIODevice::WriteOnly);
        out.write(d_bc);
        d_bcv->loadFrom(path);
        dir.remove(path);
    }
}

void AsmEditor::onRun()
{
    ENABLED_IF(true);
    if( compile() )
        d_lua->executeCmd( d_bc, d_edit->getPath().toUtf8() );
}

void AsmEditor::onRun2()
{
    ENABLED_IF(true);
    if( !compile() )
        return;
    JitBytecode bc;
    QBuffer buf(&d_bc);
    buf.open(QIODevice::ReadOnly);
    if( bc.parse(&buf, d_edit->getPath()) )
        d_eng->run( &bc );
}

void AsmEditor::onNew()
{
    ENABLED_IF(true);

    if( !checkSaved( tr("New File")) )
        return;

    d_edit->newFile();
    onCaption();
}

void AsmEditor::onOpen()
{
    ENABLED_IF( true );

    if( !checkSaved( tr("New File")) )
        return;

    const QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"),QString(),
                                                          tr("*.ljasm") );
    if (fileName.isEmpty())
        return;

    QDir::setCurrent(QFileInfo(fileName).absolutePath());

    d_edit->loadFromFile(fileName);
    onCaption();
}

void AsmEditor::onSave()
{
    ENABLED_IF( d_edit->isModified() );

    if( !d_edit->getPath().isEmpty() )
        d_edit->saveToFile( d_edit->getPath() );
    else
        onSaveAs();
}

void AsmEditor::onSaveAs()
{
    ENABLED_IF(true);

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                                                          QFileInfo(d_edit->getPath()).absolutePath(),
                                                          tr("*.ljasm") );

    if (fileName.isEmpty())
        return;

    QDir::setCurrent(QFileInfo(fileName).absolutePath());

    if( !fileName.endsWith(".ljasm",Qt::CaseInsensitive ) )
        fileName += ".ljasm";

    d_edit->saveToFile(fileName);
    onCaption();
}

void AsmEditor::onCaption()
{
    if( d_edit->getPath().isEmpty() )
    {
        setWindowTitle(tr("<unnamed> - %1").arg(qApp->applicationName()));
    }else
    {
        QFileInfo info(d_edit->getPath());
        QString star = d_edit->isModified() ? "*" : "";
        setWindowTitle(tr("%1%2 - %3").arg(info.fileName()).arg(star).arg(qApp->applicationName()) );
    }
}

void AsmEditor::onGotoLnr(int lnr)
{
    if( d_lock )
        return;
    d_lock = true;
    d_edit->setCursorPosition(lnr-1,0);
    d_lock = false;
}

void AsmEditor::onFullScreen()
{
    CHECKED_IF(true,isFullScreen());
    QSettings s;
    if( isFullScreen() )
    {
        showMaximized();
        s.setValue("Fullscreen", false );
    }else
    {
        showFullScreen();
        s.setValue("Fullscreen", true );
    }
}

void AsmEditor::onCursor()
{
    if( d_lock )
        return;
    d_lock = true;
    QTextCursor cur = d_edit->textCursor();
    const int line = cur.blockNumber() + 1;
    d_lock = false;
}

void AsmEditor::onExportBc()
{
    ENABLED_IF(true);
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Binary"),
                                                          d_edit->getPath(),
                                                          tr("*.bc") );

    if (fileName.isEmpty())
        return;

    QDir::setCurrent(QFileInfo(fileName).absolutePath());

    if( !fileName.endsWith(".bc",Qt::CaseInsensitive ) )
        fileName += ".bc";
    d_lua->saveBinary(d_edit->toPlainText().toUtf8(), d_edit->getPath().toUtf8(),fileName.toUtf8());
}

void AsmEditor::onImport()
{
    ENABLED_IF(true);

    if( !checkSaved( tr("New File")) )
        return;

    d_edit->newFile();
    onCaption();

    const QString fileName = QFileDialog::getOpenFileName(this, tr("Import Lua"), d_edit->getPath(), tr("*.lua") );
    if (fileName.isEmpty())
        return;

    QDir::setCurrent(QFileInfo(fileName).absolutePath());

    QDir dir( QStandardPaths::writableLocation(QStandardPaths::TempLocation) );
    const QString path = dir.absoluteFilePath(QDateTime::currentDateTime().toString("yyMMddhhmmsszzz")+".bc");
    QFile in(fileName);
    if( !in.open(QIODevice::ReadOnly) )
    {
        QMessageBox::critical(this,tr("Import from Lua"), tr("cannot open file for reading") );
        return;
    }
    if( !d_lua->saveBinary( in.readAll(), fileName.toUtf8(),path.toUtf8() ) )
    {
        QMessageBox::critical(this,tr("Import from Lua"), tr("selected file has errors") );
        return;
    }
    Lua::JitBytecode bc;
    bc.parse(path);
    dir.remove(path);

    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    Ljas::Disasm::disassemble( bc, &buf, fileName );
    buf.close();
    d_edit->setPlainText(buf.buffer());
}

void AsmEditor::onParse()
{
    ENABLED_IF(true);

    QString name = d_edit->getPath();
    if( name.isEmpty() )
        name = tr("<unnamed>");
    qDebug() << "Checking syntax of" << name;
    if( compile() )
        qDebug() << "No errors found";
}

bool AsmEditor::checkSaved(const QString& title)
{
    if( d_edit->isModified() )
    {
        switch( QMessageBox::critical( this, title, tr("The file has not been saved; do you want to save it?"),
                               QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes ) )
        {
        case QMessageBox::Yes:
            if( !d_edit->getPath().isEmpty() )
                return d_edit->saveToFile(d_edit->getPath());
            else
            {
                const QString path = QFileDialog::getSaveFileName( this, title, QString(), "*.ljasm" );
                if( path.isEmpty() )
                    return false;
                QDir::setCurrent(QFileInfo(path).absolutePath());
                return d_edit->saveToFile(path);
            }
            break;
        case QMessageBox::No:
            return true;
        default:
            return false;
        }
    }
    return true;
}

bool AsmEditor::compile()
{
    Ljas::Errors err;
    err.setReportToConsole(true);
    Ljas::Lexer lex;
    lex.setErrors(&err);
    QByteArray code = d_edit->toPlainText().toUtf8();
    QBuffer buf(&code);
    buf.open(QIODevice::ReadOnly);
    lex.setStream(&buf,d_edit->getPath());
    Ljas::Parser p(&lex,&err);
    p.Parse();
    if( err.getErrCount() != 0 )
        return false;

    Ljas::Assembler ass(&err);
    if( ass.process( p.d_root.d_children.first() ) )
    {
        d_bc = ass.getBc();
        return true;
    }else
        return false;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/LjTools");
    a.setApplicationName("LjAsmViewer");
    a.setApplicationVersion("0.1");
    a.setStyle("Fusion");

    Lua::AsmEditor w;

    if( a.arguments().size() > 1 )
        w.loadFile(a.arguments()[1] );

    return a.exec();
}
