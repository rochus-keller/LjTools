/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Lua parser/compiler library.
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

#include "LjBcDebugger.h"
#include "LuaHighlighter.h"
#include "LuaProject.h"
#include "LjasFileCache.h"
#include "LjasErrors.h"
#include <LjTools/Engine2.h>
#include <LjTools/Terminal2.h>
#include <LjTools/BcViewer2.h>
#include <LjTools/BcViewer.h>
#include <LjTools/LuaJitEngine.h>
#include <LjTools/LuaJitComposer.h>
#include <lua.hpp>
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
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QDesktopWidget>
#include <QInputDialog>
#include <GuiTools/AutoMenu.h>
#include <GuiTools/CodeEditor.h>
#include <GuiTools/AutoShortcut.h>
#include <GuiTools/DocTabWidget.h>
#include <math.h>
using namespace Lua;

#ifdef Q_OS_MAC
#define OBN_BREAK_SC "SHIFT+F8"
#define OBN_ABORT_SC "CTRL+SHIFT+Y"
#define OBN_CONTINUE_SC "CTRL+Y"
#define OBN_STEPIN_SC "CTRL+SHIFT+I"
#define OBN_STEPOVER_SC "CTRL+SHIFT+O"
#define OBN_STEPOUT_SC "SHIFT+F11" // TODO
#define OBN_ENDBG_SC "F4"
#define OBN_TOGBP_SC "F8"
#define OBN_GOBACK_SC "ALT+CTRL+Left"
#define OBN_GOFWD_SC "ALT+CTRL+Right"
#define OBN_NEXTDOC_SC "ALT+TAB"
#define OBN_PREVDOC_SC "ALT+SHIFT+TAB"
#else
#define OBN_BREAK_SC "SHIFT+F9"
#define OBN_ABORT_SC "SHIFT+F5"
#define OBN_CONTINUE_SC "F5"
#define OBN_STEPIN_SC "F11"
#define OBN_STEPOVER_SC "F10"
#define OBN_STEPOUT_SC "SHIFT+F11"
#define OBN_ENDBG_SC "F8"
#define OBN_TOGBP_SC "F9"
#define OBN_GOBACK_SC "ALT+Left"
#define OBN_GOFWD_SC "ALT+Right"
#define OBN_NEXTDOC_SC "CTRL+TAB"
#define OBN_PREVDOC_SC "CTRL+SHIFT+TAB"
#endif

// derived from LuaIDE

QString BcDebugger::relativeToAbsolutePath( QString path )
{
    QFileInfo info(path);
    if( info.isRelative() )
    {
        if( !path.endsWith(".lua") )
        {
            foreach( const SourceBinaryPair& f, d_files )
            {
                if( QFileInfo(f.second).baseName() == path )
                    return f.second;
            }
        }else
            path = QDir::cleanPath(QDir::current().absoluteFilePath(path));
    }
    return path;
}

class BcDebugger::Debugger : public DbgShell
{
public:
    BcDebugger* d_ide;
    Debugger(BcDebugger* ide):d_ide(ide){}
    void handleBreak( Engine2* lua, const QByteArray& source, quint32 line )
    {
        d_ide->enableDbgMenu();
        d_ide->fillStack();
        d_ide->fillLocals();

        QByteArray msg = lua->getValueString(1).simplified();
        msg = msg.mid(1,msg.size()-2); // remove ""
        if( !lua->isBreakHit() )
        {
            if( d_ide->luaRuntimeMessage(msg,source) )
                d_ide->onErrors();
        }

        while( lua->isWaiting() )
        {
            QApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents );
            QApplication::flush();
        }
        d_ide->removePosMarkers();
        d_ide->enableDbgMenu();
        d_ide->d_stack->clear();
        d_ide->d_locals->clear();
    }
    void handleAliveSignal(Engine2* e)
    {
        QApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents );
        QApplication::flush();
    }
};

static BcDebugger* s_this = 0;
static void report(QtMsgType type, const QString& message )
{
    if( s_this )
    {
        switch(type)
        {
        case QtDebugMsg:
            // NOP s_this->logMessage(QLatin1String("INF: ") + message);
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
static void messageHander(QtMsgType type, const QMessageLogContext& ctx, const QString& message)
{
    if( s_oldHandler )
        s_oldHandler(type, ctx, message );
    report(type,message);
}

BcDebugger::BcDebugger(Engine2* lua, QWidget *parent)
    : QMainWindow(parent),d_lock(false),d_pushBackLock(false)
{
    s_this = this;

    if( lua )
    {
        d_lua = lua;
        d_lua->setBytecodeMode(true);
    }else
    {
        d_lua = new Engine2(this);
        d_lua->addStdLibs();
        d_lua->addLibrary(Engine2::PACKAGE);
        d_lua->addLibrary(Engine2::IO);
        d_lua->addLibrary(Engine2::BIT);
        d_lua->addLibrary(Engine2::JIT);
        d_lua->addLibrary(Engine2::FFI);
        d_lua->addLibrary(Engine2::OS);
        d_lua->setBytecodeMode(true);
        Engine2::setInst(d_lua);
    }
    lua_pushcfunction( d_lua->getCtx(), Engine2::TRAP );
    lua_setglobal( d_lua->getCtx(), "TRAP" );
    lua_pushcfunction( d_lua->getCtx(), Engine2::TRACE );
    lua_setglobal( d_lua->getCtx(), "TRACE" );
    lua_pushcfunction( d_lua->getCtx(), Engine2::ABORT );
    lua_setglobal( d_lua->getCtx(), "ABORT" );

    d_dbg = new Debugger(this);
    d_lua->setDbgShell(d_dbg);
    // d_lua->setAliveSignal(true); // reduces performance by factor 2 to 5
    connect( d_lua, SIGNAL(onNotify(int,QByteArray,int)),this,SLOT(onLuaNotify(int,QByteArray,int)) );

    d_tab = new DocTabWidget(this,false);
    d_tab->setCloserIcon( ":/images/close.png" );
    Gui::AutoMenu* pop = new Gui::AutoMenu( d_tab, true );
    pop->addCommand( tr("Forward Tab"), d_tab, SLOT(onDocSelect()), tr(OBN_NEXTDOC_SC) );
    pop->addCommand( tr("Backward Tab"), d_tab, SLOT(onDocSelect()), tr(OBN_PREVDOC_SC) );
    pop->addCommand( tr("Close Tab"), d_tab, SLOT(onCloseDoc()), tr("CTRL+W") );
    pop->addCommand( tr("Close All"), d_tab, SLOT(onCloseAll()) );
    pop->addCommand( tr("Close All Others"), d_tab, SLOT(onCloseAllButThis()) );
    addTopCommands( pop );

    new Gui::AutoShortcut( tr(OBN_NEXTDOC_SC), this, d_tab, SLOT(onDocSelect()) );
    new Gui::AutoShortcut( tr(OBN_PREVDOC_SC), this, d_tab, SLOT(onDocSelect()) );
    new Gui::AutoShortcut( tr("CTRL+W"), this, d_tab, SLOT(onCloseDoc()) );

    connect( d_tab, SIGNAL( currentChanged(int) ), this, SLOT(onTabChanged() ) );
    connect( d_tab, SIGNAL(closing(int)), this, SLOT(onTabClosing(int)) );

    setDockNestingEnabled(true);
    setCorner( Qt::BottomRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::BottomLeftCorner, Qt::LeftDockWidgetArea );
    setCorner( Qt::TopRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::TopLeftCorner, Qt::LeftDockWidgetArea );

    d_dbgBreak = new QAction(tr("Break"),this);
    d_dbgBreak->setShortcutContext(Qt::ApplicationShortcut);
    d_dbgBreak->setShortcut(tr(OBN_BREAK_SC));
    addAction(d_dbgBreak);
    connect( d_dbgBreak, SIGNAL(triggered(bool)),this,SLOT(onBreak()) );
    d_dbgAbort = new QAction(tr("Abort"),this);
    d_dbgAbort->setShortcutContext(Qt::ApplicationShortcut);
    d_dbgAbort->setShortcut(tr(OBN_ABORT_SC));
    addAction(d_dbgAbort);
    connect( d_dbgAbort, SIGNAL(triggered(bool)),this,SLOT(onAbort()) );
    d_dbgContinue = new QAction(tr("Continue"),this);
    d_dbgContinue->setShortcutContext(Qt::ApplicationShortcut);
    d_dbgContinue->setShortcut(tr(OBN_CONTINUE_SC));
    addAction(d_dbgContinue);
    connect( d_dbgContinue, SIGNAL(triggered(bool)),this,SLOT(onContinue()) );
    d_dbgStepIn = new QAction(tr("Step In"),this);
    d_dbgStepIn->setShortcutContext(Qt::ApplicationShortcut);
    d_dbgStepIn->setShortcut(tr(OBN_STEPIN_SC));
    addAction(d_dbgStepIn);
    connect( d_dbgStepIn, SIGNAL(triggered(bool)),this,SLOT(onStepInto()) );
    d_dbgStepOver = new QAction(tr("Step Over"),this);
    d_dbgStepOver->setShortcutContext(Qt::ApplicationShortcut);
    d_dbgStepOver->setShortcut(tr(OBN_STEPOVER_SC));
    addAction(d_dbgStepOver);
    connect( d_dbgStepOver, SIGNAL(triggered(bool)),this,SLOT(onStepOver()) );
    d_dbgStepOut = new QAction(tr("Step Out"),this);
    d_dbgStepOut->setShortcutContext(Qt::ApplicationShortcut);
    d_dbgStepOut->setShortcut(tr(OBN_STEPOUT_SC));
    addAction(d_dbgStepOut);
    connect( d_dbgStepOut, SIGNAL(triggered(bool)),this,SLOT(onStepOut()) );

    enableDbgMenu();

    createTerminal();
    createMods();
    createErrs();
    createStack();
    createLocals();
    createMenu();

    setCentralWidget(d_tab);

    createMenuBar();

    s_oldHandler = qInstallMessageHandler(messageHander);

    QSettings s;

    const QRect screen = QApplication::desktop()->screenGeometry();
    resize( screen.width() - 20, screen.height() - 30 ); // so that restoreState works
    if( s.value("Fullscreen").toBool() )
        showFullScreen();
    else
        showMaximized();

    const QVariant state = s.value( "DockState" );
    if( !state.isNull() )
        restoreState( state.toByteArray() );


    // TODO connect( d_pro,SIGNAL(sigRenamed()),this,SLOT(onCaption()) );
    // TODO connect( d_pro,SIGNAL(sigModified(bool)),this,SLOT(onCaption()) );
}

BcDebugger::~BcDebugger()
{
    d_lua->setDbgShell(0);
    delete d_dbg;
}

void BcDebugger::loadFile(const QString& path)
{
    QFileInfo info(path);

    /* TODO
    if( info.isDir() && info.suffix() != ".luapro" )
    {
        d_pro->initializeFromDir( path );
    }else
    {
        d_pro->loadFrom(path);
    }
    */

    onCaption();
}

void BcDebugger::logMessage(const QString& str, bool err)
{
    d_term->printText(str,err);
}

void BcDebugger::setSpecialInterpreter(bool on)
{
    d_term->setSpecialInterpreter(on);
}

bool BcDebugger::initializeFromFiles(const Files& files, const QString& workingDir, const QByteArray& run)
{
    d_files = files;
    d_runCmd = run;
    d_workingDir = workingDir;
    fillMods();
}

void BcDebugger::closeEvent(QCloseEvent* event)
{
    if( d_lua->isExecuting() )
    {
        QMessageBox::warning(this,tr("Closing Main Window"), tr("Cannot quit IDE when Lua is running") );
        event->setAccepted(false);
        return;
    }
    QSettings s;
    s.setValue( "DockState", saveState() );
    const bool ok = checkSaved( tr("Quit Application"));
    event->setAccepted(ok);

    qApp->quit();
}

void BcDebugger::createTerminal()
{
    QDockWidget* dock = new QDockWidget( tr("Terminal"), this );
    dock->setObjectName("Terminal");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_term = new Terminal2(dock, d_lua);
    dock->setWidget(d_term);
    addDockWidget( Qt::BottomDockWidgetArea, dock );
    new Gui::AutoShortcut( tr("CTRL+SHIFT+C"), this, d_term, SLOT(onClear()) );
}

void BcDebugger::createMods()
{
    QDockWidget* dock = new QDockWidget( tr("Modules"), this );
    dock->setObjectName("Modules");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable );
    d_mods = new QTreeWidget(dock);
    d_mods->setHeaderHidden(true);
    d_mods->setExpandsOnDoubleClick(false);
    d_mods->setAlternatingRowColors(true);
    dock->setWidget(d_mods);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
    connect( d_mods, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),this,SLOT(onModsDblClicked(QTreeWidgetItem*,int)) );
}

void BcDebugger::createErrs()
{
    QDockWidget* dock = new QDockWidget( tr("Issues"), this );
    dock->setObjectName("Issues");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_errs = new QTreeWidget(dock);
    d_errs->setSizePolicy(QSizePolicy::MinimumExpanding,QSizePolicy::Preferred);
    d_errs->setAlternatingRowColors(true);
    d_errs->setHeaderHidden(true);
    d_errs->setSortingEnabled(false);
    d_errs->setAllColumnsShowFocus(true);
    d_errs->setRootIsDecorated(false);
    d_errs->setColumnCount(3);
    d_errs->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    d_errs->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    d_errs->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    dock->setWidget(d_errs);
    addDockWidget( Qt::BottomDockWidgetArea, dock );
    connect(d_errs, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(onErrorsDblClicked()) );
    connect( new QShortcut( tr("ESC"), this ), SIGNAL(activated()), dock, SLOT(hide()) );
}

void BcDebugger::createStack()
{
    QDockWidget* dock = new QDockWidget( tr("Stack"), this );
    dock->setObjectName("Stack");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_stack = new QTreeWidget(dock);
    d_stack->setHeaderHidden(true);
    d_stack->setAlternatingRowColors(true);
    d_stack->setColumnCount(4); // Level, Name, Pos, Mod
    d_stack->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    d_stack->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    d_stack->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    d_stack->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    dock->setWidget(d_stack);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
    connect( d_stack, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),this,SLOT(onStackDblClicked(QTreeWidgetItem*,int)) );
}

void BcDebugger::createLocals()
{
    QDockWidget* dock = new QDockWidget( tr("Locals"), this );
    dock->setObjectName("Locals");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_locals = new QTreeWidget(dock);
    d_locals->setHeaderHidden(true);
    d_locals->setAlternatingRowColors(true);
    d_locals->setColumnCount(2); // Name, Value
    d_locals->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    d_locals->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    dock->setWidget(d_locals);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
}

void BcDebugger::createMenu()
{
    Gui::AutoMenu* pop = new Gui::AutoMenu( d_mods, true );
    pop->addCommand( "Show File", this, SLOT(onOpenFile()) );
    //pop->addAction("Expand all", d_mods, SLOT(expandAll()) );
    //pop->addSeparator();
    /*
    pop->addCommand( "New Project", this, SLOT(onNewPro()), tr("CTRL+N"), false );
    pop->addCommand( "Open Project...", this, SLOT(onOpenPro()), tr("CTRL+O"), false );
    pop->addCommand( "Save Project", this, SLOT(onSavePro()), tr("CTRL+SHIFT+S"), false );
    pop->addCommand( "Save Project as...", this, SLOT(onSaveAs()) );
    pop->addSeparator();
    pop->addCommand( "Add Modules...", this, SLOT(onAddFiles()) );
    pop->addCommand( "Remove Module...", this, SLOT(onRemoveFile()) );
    pop->addSeparator();
    */
    pop->addCommand( "Set Main Function...", this, SLOT( onSetMain() ) );
    pop->addCommand( "Set Working Directory...", this, SLOT( onWorkingDir() ) );
    pop->addSeparator();
    pop->addCommand( "Run on LuaJIT", this, SLOT(onRun()), tr("CTRL+R"), false );
    addDebugMenu(pop);
    addTopCommands(pop);

    //new Gui::AutoShortcut( tr("CTRL+O"), this, this, SLOT(onOpenPro()) );
    //new Gui::AutoShortcut( tr("CTRL+N"), this, this, SLOT(onNewPro()) );
    //new Gui::AutoShortcut( tr("CTRL+SHIFT+S"), this, this, SLOT(onSavePro()) );
    //new Gui::AutoShortcut( tr("CTRL+S"), this, this, SLOT(onSaveFile()) );
    new Gui::AutoShortcut( tr("CTRL+R"), this, this, SLOT(onRun()) );
    new Gui::AutoShortcut( tr(OBN_GOBACK_SC), this, this, SLOT(handleGoBack()) );
    new Gui::AutoShortcut( tr(OBN_GOFWD_SC), this, this, SLOT(handleGoForward()) );
    new Gui::AutoShortcut( tr(OBN_TOGBP_SC), this, this, SLOT(onToggleBreakPt()) );
    new Gui::AutoShortcut( tr(OBN_ENDBG_SC), this, this, SLOT(onEnableDebug()) );
}

void BcDebugger::createMenuBar()
{
    Gui::AutoMenu* pop = new Gui::AutoMenu( tr("File"), this );
    /*
    pop->addCommand( "New Project", this, SLOT(onNewPro()), tr("CTRL+N"), false );
    pop->addCommand( "Open Project...", this, SLOT(onOpenPro()), tr("CTRL+O"), false );
    pop->addCommand( "Save Project", this, SLOT(onSavePro()), tr("CTRL+SHIFT+S"), false );
    pop->addCommand( "Save Project as...", this, SLOT(onSaveAs()) );
    pop->addSeparator();
    pop->addCommand( "Save", this, SLOT(onSaveFile()), tr("CTRL+S"), false );
    */
    pop->addCommand( tr("Close file"), d_tab, SLOT(onCloseDoc()), tr("CTRL+W") );
    pop->addCommand( tr("Close all"), d_tab, SLOT(onCloseAll()) );
    pop->addSeparator();
    pop->addCommand(tr("Quit"),this,SLOT(onQuit()), tr("CTRL+Q"), true );

    /*
    pop = new Gui::AutoMenu( tr("Edit"), this );
    pop->addAutoCommand( "Undo", SLOT(handleEditUndo()), tr("CTRL+Z"), true );
    pop->addAutoCommand( "Redo", SLOT(handleEditRedo()), tr("CTRL+Y"), true );
    pop->addSeparator();
    pop->addAutoCommand( "Cut", SLOT(handleEditCut()), tr("CTRL+X"), true );
    pop->addAutoCommand( "Copy", SLOT(handleEditCopy()), tr("CTRL+C"), true );
    pop->addAutoCommand( "Paste", SLOT(handleEditPaste()), tr("CTRL+V"), true );
    pop->addSeparator();
    pop->addAutoCommand( "Find...", SLOT(handleFind()), tr("CTRL+F"), true );
    pop->addAutoCommand( "Find again", SLOT(handleFindAgain()), tr("F3"), true );
    pop->addAutoCommand( "Replace...", SLOT(handleReplace()) );
    pop->addSeparator();
    pop->addAutoCommand( "&Go to line...", SLOT(handleGoto()), tr("CTRL+G"), true );
    pop->addSeparator();
    pop->addAutoCommand( "Indent", SLOT(handleIndent()) );
    pop->addAutoCommand( "Unindent", SLOT(handleUnindent()) );
    pop->addAutoCommand( "Fix Indents", SLOT(handleFixIndent()) );
    pop->addAutoCommand( "Set Indentation Level...", SLOT(handleSetIndent()) );
    */

    pop = new Gui::AutoMenu( tr("Project"), this );
    /*
    pop->addCommand( "Add Modules...", this, SLOT(onAddFiles()) );
    pop->addCommand( "Remove Module...", this, SLOT(onRemoveFile()) );
    pop->addSeparator();
    */
    pop->addCommand( "Set Main Function...", this, SLOT( onSetMain() ) );
    pop->addCommand( "Set Working Directory...", this, SLOT( onWorkingDir() ) );

    pop = new Gui::AutoMenu( tr("Debug"), this );
    pop->addCommand( "Run on LuaJIT", this, SLOT(onRun()), tr("CTRL+R"), false );
    pop->addCommand( "Enable Debugging", this, SLOT(onEnableDebug()),tr(OBN_ENDBG_SC), false );
    pop->addCommand( "Toggle Breakpoint", this, SLOT(onToggleBreakPt()), tr(OBN_TOGBP_SC), false);
    pop->addAction( d_dbgStepIn );
    pop->addAction( d_dbgStepOver );
    pop->addAction( d_dbgStepOut );
    pop->addAction( d_dbgBreak );
    pop->addAction( d_dbgContinue );
    pop->addAction( d_dbgAbort );

    pop = new Gui::AutoMenu( tr("Window"), this );
    pop->addCommand( tr("Next Tab"), d_tab, SLOT(onDocSelect()), tr(OBN_NEXTDOC_SC) );
    pop->addCommand( tr("Previous Tab"), d_tab, SLOT(onDocSelect()), tr(OBN_PREVDOC_SC) );
    pop->addSeparator();
    pop->addCommand( "Go Back", this, SLOT(handleGoBack()), tr(OBN_GOBACK_SC), false );
    pop->addCommand( "Go Forward", this, SLOT(handleGoForward()), tr(OBN_GOFWD_SC), false );
    pop->addSeparator();
    pop->addAutoCommand( "Set &Font...", SLOT(handleSetFont()) );
    pop->addAutoCommand( "Show &Linenumbers", SLOT(handleShowLinenumbers()) );
    pop->addCommand( "Show Fullscreen", this, SLOT(onFullScreen()) );
    pop->addSeparator();
    QMenu* sub2 = createPopupMenu();
    sub2->setTitle( tr("Show Window") );
    pop->addMenu( sub2 );

    Gui::AutoMenu* help = new Gui::AutoMenu( tr("Help"), this, true );
    help->addCommand( "&About this application...", this, SLOT(onAbout()) );
    help->addCommand( "&About Qt...", this, SLOT(onQt()) );
}

void BcDebugger::onRun()
{
    ENABLED_IF( !d_files.isEmpty() && !d_lua->isExecuting() );


    QDir::setCurrent(d_workingDir);

    bool hasErrors = false;
    foreach( const SourceBinaryPair& path, d_files )
    {
        const QString module = QFileInfo(path.second).baseName();
        d_lua->executeCmd( QString("package.loaded[\"%1\"]=nil").arg(module).toUtf8(), "terminal" );
        if( !d_lua->executeFile(path.second.toUtf8()) )
        {
            hasErrors = true;
        }

        if( d_lua->isAborted() )
        {
            removePosMarkers();
            return;
        }
    }

    if( hasErrors )
    {
        removePosMarkers();
        onErrors();
        return;
    }

    //out << "jit.off()" << endl;
    //out << "jit.opt.start(3)" << endl;
    //out << "jit.opt.start(\"-abc\")" << endl;
    //out << "jit.opt.start(\"-fuse\")" << endl;
    //out << "jit.opt.start(\"hotloop=10\", \"hotexit=2\")" << endl;

    if( !d_runCmd.isEmpty() )
        d_lua->executeCmd(d_runCmd);
    removePosMarkers();

}

void BcDebugger::onAbort()
{
    // ENABLED_IF( d_lua->isWaiting() );
    d_lua->terminate();
}

void BcDebugger::onNewPro()
{
    ENABLED_IF(true);

    if( !checkSaved( tr("New Project")) )
        return;

    // TODO d_pro->createNew();
    d_tab->onCloseAll();
    fillMods();
    onTabChanged();
}

void BcDebugger::onOpenPro()
{
    ENABLED_IF( true );

    if( !checkSaved( tr("New Project")) )
        return;

    const QString fileName = QFileDialog::getOpenFileName(this, tr("Open Project"),QString(),
                                                          tr("Lua Project (*.luapro)") );
    if (fileName.isEmpty())
        return;

    QDir::setCurrent(QFileInfo(fileName).absolutePath());

    d_tab->onCloseAll();
    // TODO d_pro->loadFrom(fileName);

    fillMods();
    onTabChanged();
}

void BcDebugger::onSavePro()
{
    /* TODO
    ENABLED_IF( d_pro->isDirty() );

    if( !d_pro->getFilePath().isEmpty() )
        d_pro->save();
    else
        onSaveAs();
        */
}

void BcDebugger::onSaveFile()
{
    // TODO
}

void BcDebugger::onSaveAs()
{
    ENABLED_IF(true);

    /* TODO
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                                          QFileInfo(d_pro->getFilePath()).absolutePath(),
                                                          tr("Lua Project (*.luapro)") );

    if (fileName.isEmpty())
        return;

    QDir::setCurrent(QFileInfo(fileName).absolutePath());

    if( !fileName.endsWith(".luapro",Qt::CaseInsensitive ) )
        fileName += ".luapro";

    d_pro->saveTo(fileName);
    */
    onCaption();
}

void BcDebugger::onCaption()
{
    /* TODO
    const QString star = d_pro->isDirty() || d_filesDirty ? "*" : "";
    if( d_pro->getFilePath().isEmpty() )
    {
        setWindowTitle(tr("<unnamed>%2 - %1").arg(qApp->applicationName()).arg(star));
    }else
    {
        QFileInfo info(d_pro->getFilePath());
        setWindowTitle(tr("%1%2 - %3").arg(info.fileName()).arg(star).arg(qApp->applicationName()) );
    }
    */
}

void BcDebugger::onGotoLnr(quint32 lnr)
{
    if( d_lock )
        return;
    d_lock = true;
    /* TODO
    Editor* edit = static_cast<Editor*>( d_tab->getCurrentTab() );
    if( edit )
    {
        edit->setCursorPosition(lnr-1,0);
        edit->setFocus();
    }
    */
    d_lock = false;
}

void BcDebugger::onFullScreen()
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

void BcDebugger::onCursor()
{
    if( d_lock )
        return;
    d_lock = true;
    /* TODO
    Editor* edit = static_cast<Editor*>( d_tab->getCurrentTab() );
    if( edit )
    {
        QTextCursor cur = edit->textCursor();
        const int line = cur.blockNumber() + 1;
        d_bcv->gotoLine(line);
    }
    */
    d_lock = false;
}

void BcDebugger::onModsDblClicked(QTreeWidgetItem* item, int)
{
    showEditor( item->data(0,Qt::UserRole).toString() );
}

void BcDebugger::onStackDblClicked(QTreeWidgetItem* item, int)
{
    if( item )
    {
        const QString source = item->data(3,Qt::UserRole).toString();
        if( !source.isEmpty() )
        {
            const quint32 line = item->data(2,Qt::UserRole).toUInt();
            const quint32 func = item->data(1,Qt::UserRole).toUInt();
            showEditor( source, func, line );
        }
        const int level = item->data(0,Qt::UserRole).toInt();
        d_lua->setActiveLevel(level);
        fillLocals();
    }
}

void BcDebugger::onTabChanged()
{
    onEditorChanged();
}

void BcDebugger::onTabClosing(int i)
{
    // TODO d_pro->getFc()->removeFile( d_tab->getDoc(i).toString() );
}

void BcDebugger::onEditorChanged()
{
    for( int i = 0; i < d_tab->count(); i++ )
    {
        BcViewer2* e = static_cast<BcViewer2*>( d_tab->widget(i) );
        QFileInfo info( d_tab->getDoc(i).toString() );
        d_tab->setTabText(i, info.baseName() );
    }
    onCaption();
}

void BcDebugger::onErrorsDblClicked()
{
    QTreeWidgetItem* item = d_errs->currentItem();
    showEditor( item->data(0, Qt::UserRole ).toString(),
                item->data(1, Qt::UserRole ).toInt(), item->data(2, Qt::UserRole ).toInt() );
}

void BcDebugger::onErrors()
{
    d_errs->clear();
}

void BcDebugger::onOpenFile()
{
    ENABLED_IF( d_mods->currentItem() );

    onModsDblClicked( d_mods->currentItem(), 0 );
}

void BcDebugger::onAddFiles()
{
    ENABLED_IF(true);

    QString filter;
    /* TODO
    foreach( const QString& suf, d_pro->getSuffixes() )
        filter += " *" + suf;
    const QStringList files = QFileDialog::getOpenFileNames(this,tr("Add Modules"),QString(),filter );
    foreach( const QString& f, files )
    {
        if( !d_pro->addFile(f) )
            qWarning() << "cannot add module" << f;
    }
    */
    fillMods();
    onTabChanged();
}

void BcDebugger::onRemoveFile()
{
    ENABLED_IF( d_mods->currentItem() );

    QString path = d_mods->currentItem()->data(0,Qt::UserRole).toString();
    if( path.isEmpty() )
        return;

    if( QMessageBox::warning( this, tr("Remove Module"),
                              tr("Do you really want to remove module '%1' from project?").arg(QFileInfo(path).baseName()),
                           QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes ) != QMessageBox::Yes )
        return;
    for(int i = 0; i < d_files.size(); i++ )
    {
        if( d_files[i].second == path )
        {
            d_files.removeAt(i);
            break;
        }
    }
}

void BcDebugger::onEnableDebug()
{
    CHECKED_IF( true, d_lua->isDebug() );

    d_lua->setDebug( !d_lua->isDebug() );
    enableDbgMenu();
}

void BcDebugger::onBreak()
{
    if( !d_lua->isDebug() )
        d_lua->setDebug(true);
    // normal call because called during processEvent which doesn't seem to enable
    // the functions: ENABLED_IF( d_lua->isExecuting() );
    d_lua->runToNextLine();
}

bool BcDebugger::checkSaved(const QString& title)
{
    /* TODO
    if( d_pro->isDirty() )
    {
        switch( QMessageBox::critical( this, title, tr("The the project has not been saved; do you want to save it?"),
                               QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes ) )
        {
        case QMessageBox::Yes:
            if( !d_pro->getFilePath().isEmpty() )
                return d_pro->save();
            else
            {
                const QString path = QFileDialog::getSaveFileName( this, title, QString(), "Lua Project (*.luapro)" );
                if( path.isEmpty() )
                    return false;
                QDir::setCurrent(QFileInfo(path).absolutePath());
                return d_pro->saveTo(path);
            }
            break;
        case QMessageBox::No:
            return true;
        default:
            return false;
        }
    }
    */
    return true;
}

static bool sortNamed( Module::Thing* lhs, Module::Thing* rhs )
{
    QByteArray l = lhs->d_tok.d_val.toLower();
    QByteArray r = rhs->d_tok.d_val.toLower();
    char buf[10];
    if( l.isEmpty() )
    {
        qsnprintf(buf,10,"%06d", lhs->d_tok.d_lineNr);
        l = buf;
    }
    if( r.isEmpty() )
    {
        qsnprintf(buf,10,"%06d", rhs->d_tok.d_lineNr);
        r = buf;
    }
    return l < r;
}

void BcDebugger::fillMods()
{
    d_mods->clear();
    foreach( const SourceBinaryPair& path, d_files )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_mods);
        item->setText(0, QFileInfo(path.first).baseName());
        item->setToolTip(0,path.first);
        item->setData(0,Qt::UserRole,path.first );
    }
    d_mods->sortByColumn(0,Qt::AscendingOrder);
}

void BcDebugger::addTopCommands(Gui::AutoMenu* pop)
{
    Q_ASSERT( pop != 0 );
    pop->addSeparator();
    pop->addCommand( "Go Back", this, SLOT(handleGoBack()), tr(OBN_GOBACK_SC), false );
    pop->addCommand( "Go Forward", this, SLOT(handleGoForward()), tr(OBN_GOFWD_SC), false );
    pop->addSeparator();
    pop->addAutoCommand( "Set &Font...", SLOT(handleSetFont()) );
    pop->addAutoCommand( "Show &Linenumbers", SLOT(handleShowLinenumbers()) );
    pop->addCommand( "Show Fullscreen", this, SLOT(onFullScreen()) );
    pop->addSeparator();
    pop->addCommand(tr("Quit"),this,SLOT(onQuit()) );
}

void BcDebugger::showEditor(const QString& path, quint32 func, quint32 pc, bool setMarker , bool center)
{
    int found = -1;
    for(int i = 0; i < d_files.size(); i++ )
    {
        if( d_files[i].first == path || d_files[i].second == path )
        {
            found = i;
            break;
        }
    }
    if( found == -1 )
        return;

    const int i = d_tab->findDoc(path);
    BcViewer2* edit = 0;
    if( i != -1 )
    {
        d_tab->setCurrentIndex(i);
        edit = static_cast<BcViewer2*>( d_tab->widget(i) );
    }else
    {
        edit = new BcViewer2(this);
        createMenu(edit);

        //connect(edit, SIGNAL(modificationChanged(bool)), this, SLOT(onEditorChanged()) );
        //connect(edit,SIGNAL(cursorPositionChanged()),this,SLOT(onCursor()));
        //connect(edit,SIGNAL(sigUpdateLocation(int,int)),this,SLOT(onUpdateLocation(int,int)));

        edit->setLastWidth(200);
        edit->loadFrom(d_files[found].second,d_files[found].first);

        const Engine2::Breaks& br = d_lua->getBreaks( path.toUtf8() );
        Engine2::Breaks::const_iterator j;
        for( j = br.begin(); j != br.end(); ++j )
            edit->addBreakPoint((*j));

        d_tab->addDoc(edit,path);
        onEditorChanged();
    }

    if( func != 0 && pc != 0 )
        edit->gotoFuncPc( func, pc, center, setMarker );
    edit->setFocus();
}

void BcDebugger::createMenu(QWidget* edit)
{
    Gui::AutoMenu* pop = new Gui::AutoMenu( edit, true );
    pop->addCommand( "Run on LuaJIT", this, SLOT(onRun()), tr("CTRL+R"), false );
    addDebugMenu(pop);
    pop->addSeparator();
    addTopCommands(pop);
}

void BcDebugger::addDebugMenu(Gui::AutoMenu* pop)
{
    Gui::AutoMenu* sub = new Gui::AutoMenu(tr("Debugger"), this, false );
    pop->addMenu(sub);
    sub->addCommand( "Enable Debugging", this, SLOT(onEnableDebug()),tr(OBN_ENDBG_SC), false );
    sub->addCommand( "Toggle Breakpoint", this, SLOT(onToggleBreakPt()), tr(OBN_TOGBP_SC), false);
    sub->addAction( d_dbgStepIn );
    sub->addAction( d_dbgStepOver );
    sub->addAction( d_dbgStepOut );
    sub->addAction( d_dbgBreak );
    sub->addAction( d_dbgContinue );
    sub->addAction( d_dbgAbort );

}

bool BcDebugger::luaRuntimeMessage(const QByteArray& msg, const QString& file )
{
    Engine2::ErrorMsg em = Engine2::decodeRuntimeMessage(msg);
    if( em.d_line != 0 )
    {
        if( em.d_source.isEmpty() )
            em.d_source = file.toUtf8();
        // TODO d_pro->getErrs()->error(Ljas::Errors::Runtime, em.d_source, em.d_line, 1, em.d_message );
        return true;
    }

    // /home/me/Smalltalk/Interpreter.lua:37: module 'ObjectMemory' not found: no field
    QRegExp reg(":[0-9]+:");
    const int pos1 = reg.indexIn(msg);
    if( pos1 != -1 )
    {
        const QString path = relativeToAbsolutePath(msg.left(pos1));
        const QString cap = reg.cap();
        const int pos2 = reg.indexIn(msg, pos1 + 1 );
        /* TODO
        if( pos2 != -1 )
        {
            // resent error from pcall return
            const QString path2 = relativeToAbsolutePath(msg.mid(pos1+cap.size(),pos2-pos1-cap.size()).trimmed());
            const QString cap2 = reg.cap();
            d_pro->getErrs()->error(Ljas::Errors::Runtime, path2.isEmpty() ? file : path2,
                                cap2.mid(1,cap2.size()-2).toInt(), 1,
                                msg.mid(pos2+reg.matchedLength()).trimmed() );
            d_pro->getErrs()->error(Ljas::Errors::Runtime, path.isEmpty() ? file : path,
                                cap.mid(1,cap.size()-2).toInt(), 1, "rethrown error" );
        }else
            d_pro->getErrs()->error(Ljas::Errors::Runtime, path.isEmpty() ? file : path,
                                cap.mid(1,cap.size()-2).toInt(), 1,
                                msg.mid(pos1+reg.matchedLength()).trimmed() );
                                */
        return true;
    }
    // TODO d_pro->getErrs()->error(Ljas::Errors::Runtime, file, 0, 0, msg );
    return false;
    // qWarning() << "Unknown Lua error message format:" << msg;
}

void BcDebugger::fillStack()
{
    d_stack->clear();
    Engine2::StackLevels ls = d_lua->getStackTrace();

    bool opened = false;
    for( int level = 0; level < ls.size(); level++ )
    {
        const Engine2::StackLevel& l = ls[level];
        // Level, Name, Pos, Mod
        QTreeWidgetItem* item = new QTreeWidgetItem(d_stack);
        item->setText(0,QString::number(l.d_level));
        item->setData(0,Qt::UserRole,l.d_level);
        item->setText(1,l.d_name);
        if( l.d_inC )
        {
            item->setText(3,"(native)");
        }else
        {
            item->setData(1,Qt::UserRole,l.d_lineDefined );
            item->setText(2,QString("%1").arg( l.d_line - 1 ));
            item->setData(2, Qt::UserRole, l.d_line );
            const QString path = relativeToAbsolutePath(l.d_source);
            item->setText(3, QFileInfo(path).baseName() );
            item->setData(3, Qt::UserRole, path );
            item->setToolTip(3, path );
            if( !opened )
            {
                showEditor(path, l.d_lineDefined, l.d_line, true );
                d_lua->setActiveLevel(level);
                opened = true;
            }
        }
    }

    d_stack->parentWidget()->show();
}

static void typeAddr( QTreeWidgetItem* item, const QVariant& val )
{
    if( val.canConvert<Lua::Engine2::VarAddress>() )
    {
        Lua::Engine2::VarAddress addr = val.value<Lua::Engine2::VarAddress>();
        if( addr.d_addr )
            item->setToolTip(1, QString("address 0x%1").arg(quint32(addr.d_addr),8,16,QChar('0'))); // RISK
        switch( addr.d_type )
        {
        case Engine2::LocalVar::NIL:
            item->setText(1, "nil");
            break;
        case Engine2::LocalVar::FUNC:
            item->setText(1, "func");
            break;
        case Engine2::LocalVar::TABLE:
            item->setText(1, "table");
            break;
        case Engine2::LocalVar::STRUCT:
            item->setText(1, "struct");
            break;
        case Engine2::LocalVar::CDATA:
            item->setText(1, "cdata");
            break;
        case Engine2::LocalVar::UNKNOWN:
            item->setText(1, "<unknown>");
            break;
        }
    }else if( val.type() == QMetaType::QVariantMap)
    {
        QVariantMap map = val.toMap();
        typeAddr( item, map.value(QString()) );
    }
}

static void setLocalText( QTreeWidgetItem* local, const QVariant& val )
{
    switch( val.type() )
    {
    case QVariant::Double:
        {
            const double d = val.toDouble();
            const int i = d;
            if( d == NAN )
            {
                local->setText(1, "#NaN" );
            }else if( d - double(i) == 0.0 )
            {
                local->setToolTip(1, QString("%1 0x%2").arg(i).arg(i,0,16));
                local->setText(1, QString::number( quint32(d) ) );
            }else
            {
                local->setText(1, val.toString() );
                local->setToolTip(1,QString::number(d, 'f', 8 ));
            }
        }
        break;
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::LongLong:
    case QVariant::ULongLong:
        local->setText(1, val.toString() );
        local->setToolTip(1, QString("%1 0x%2").arg(val.toString()).arg(val.toLongLong(),0,16));
        break;
    default:
        local->setText(1, val.toString() );
        break;
    }
}

static void fillLocalSubs( QTreeWidgetItem* super, const QVariantMap& vals )
{
    QVariantMap::const_iterator i;
    for( i = vals.begin(); i != vals.end(); i++ )
    {
        if( i.key().isEmpty() )
            continue;
        QTreeWidgetItem* item = new QTreeWidgetItem(super);
        item->setText(0, i.key() );

        if( i.value().canConvert<Lua::Engine2::VarAddress>() )
        {
            typeAddr(item,i.value());
        }else if( i.value().type() == QMetaType::QVariantMap)
        {
            typeAddr(item,i.value());
            fillLocalSubs(item,i.value().toMap() );
        }else if( i.value().type() == QMetaType::QByteArray )
        {
            item->setText(1, "\"" + i.value().toString().simplified() + "\"");
            item->setToolTip(1, i.value().toString());
        }else
            setLocalText(item,i.value());
    }
}

void BcDebugger::fillLocals()
{
    d_locals->clear();
    Engine2::LocalVars vs = d_lua->getLocalVars(true,4,50,true); // TODO: adjustable
    foreach( const Engine2::LocalVar& v, vs )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_locals);
        QString name = v.d_name;
        if( v.d_isUv )
            name = name + "'";
        item->setText(0,name);
        if( v.d_value.canConvert<Lua::Engine2::VarAddress>() )
        {
            typeAddr(item,v.d_value);
        }else if( v.d_value.type() == QMetaType::QVariantMap )
        {
            typeAddr(item,v.d_value);
            fillLocalSubs(item,v.d_value.toMap() );
        }else if( JitBytecode::isString(v.d_value) )
        {
            if( v.d_type == Engine2::LocalVar::CDATA || v.d_type == Engine2::LocalVar::UNKNOWN )
                item->setText(1, v.d_value.toString().simplified());
            else
                item->setText(1, "\"" + v.d_value.toString().simplified() + "\"");
            item->setToolTip(1, v.d_value.toString() );
        }else if( !v.d_value.isNull() )
            setLocalText(item,v.d_value);
        else
        {
            switch( v.d_type )
            {
            case Engine2::LocalVar::NIL:
                item->setText(1, "nil");
                break;
            case Engine2::LocalVar::FUNC:
                item->setText(1, "func");
                break;
            case Engine2::LocalVar::TABLE:
                item->setText(1, "table");
                break;
            case Engine2::LocalVar::STRUCT:
                item->setText(1, "struct");
                break;
            case Engine2::LocalVar::CDATA:
                item->setText(1, "cdata");
                break;
            case Engine2::LocalVar::UNKNOWN:
                item->setText(1, "<unknown>");
                break;
            case Engine2::LocalVar::STRING:
                item->setText(1, "\"" + v.d_value.toString().simplified() + "\"");
                break;
            default:
                break;
           }
        }
    }
}

void BcDebugger::removePosMarkers()
{
    for( int i = 0; i < d_tab->count(); i++ )
    {
        BcViewer2* e = static_cast<BcViewer2*>( d_tab->widget(i) );
        e->clearMarker();
    }
}

void BcDebugger::enableDbgMenu()
{
    d_dbgBreak->setEnabled(d_lua->isExecuting());
    d_dbgAbort->setEnabled(d_lua->isExecuting());
    d_dbgContinue->setEnabled(d_lua->isWaiting());
    d_dbgStepIn->setEnabled(true);
    d_dbgStepOver->setEnabled(d_lua->isWaiting() && d_lua->isDebug() );
    d_dbgStepOut->setEnabled(d_lua->isWaiting() && d_lua->isDebug() );
}

void BcDebugger::handleGoBack()
{
    ENABLED_IF( d_backHisto.size() > 1 );

    d_pushBackLock = true;
    d_forwardHisto.push_back( d_backHisto.last() );
    d_backHisto.pop_back();
    showEditor( d_backHisto.last().d_file, d_backHisto.last().d_line+1, d_backHisto.last().d_col+1 );
    d_pushBackLock = false;
}

void BcDebugger::handleGoForward()
{
    ENABLED_IF( !d_forwardHisto.isEmpty() );

    Location cur = d_forwardHisto.last();
    d_forwardHisto.pop_back();
    showEditor( cur.d_file, cur.d_line+1, cur.d_col+1 );
}

void BcDebugger::onUpdateLocation(int line, int col)
{
    /* TODO
    Editor* e = static_cast<Editor*>( sender() );
    e->clearBackHisto();
    pushLocation(Location(e->getPath(), line,col));
    */
}

void BcDebugger::onToggleBreakPt()
{
    BcViewer2* edit = static_cast<BcViewer2*>( d_tab->getCurrentTab() );
    ENABLED_IF( edit );

    BcViewer2::Breakpoint bp;
    if( !edit->toggleBreakPoint(&bp) )
        return;
    if( bp.d_on )
        d_lua->addBreak( edit->getPath().toUtf8(), bp.d_linePc );
    else
        d_lua->removeBreak( edit->getPath().toUtf8(), bp.d_linePc );
}

void BcDebugger::onStepInto()
{
    // ENABLED_IF( d_lua->isWaiting() );

    if( !d_lua->isWaiting() || !d_lua->isDebug() )
    {
        d_lua->setDebug(true);
        enableDbgMenu();
        d_lua->setDefaultCmd(Engine2::StepInto);
        onRun();
    }else
        d_lua->runToNextLine();
}

void BcDebugger::onStepOver()
{
    d_lua->runToNextLine(Engine2::StepOver);
}

void BcDebugger::onStepOut()
{
    d_lua->runToNextLine(Engine2::StepOut);
}

void BcDebugger::onContinue()
{
    d_lua->runToBreakPoint();
}

void BcDebugger::onWorkingDir()
{
    ENABLED_IF(true);

    bool ok;
    const QString res = QInputDialog::getText(this,tr("Set Working Directory"), QString(), QLineEdit::Normal,
                                              d_workingDir, &ok );
    if( !ok )
        return;
    d_workingDir = res;
}

void BcDebugger::onLuaNotify(int messageType, QByteArray val1, int val2)
{
    switch( messageType )
    {
    case Engine2::Started:
    case Engine2::Continued:
    case Engine2::LineHit:
    case Engine2::BreakHit:
    case Engine2::ErrorHit:
    case Engine2::Finished:
    case Engine2::Aborted:
        enableDbgMenu();
        break;
    }
}

void BcDebugger::pushLocation(const BcDebugger::Location& loc)
{
    if( d_pushBackLock )
        return;
    if( !d_backHisto.isEmpty() && d_backHisto.last() == loc )
        return; // o ist bereits oberstes Element auf dem Stack.
    d_backHisto.removeAll( loc );
    d_backHisto.push_back( loc );
}

void BcDebugger::onAbout()
{
    ENABLED_IF(true);

    QMessageBox::about( this, qApp->applicationName(),
      tr("<html>Release: %1   Date: %2<br><br>"

      "Welcome to the LuaJIT bytecode debugger.<br>"
      "See <a href=\"https://github.com/rochus-keller/LjTools\">"
         "here</a> for more information.<br><br>"

      "Author: Rochus Keller, me@rochus-keller.ch<br><br>"

      "Licese: <a href=\"https://www.gnu.org/licenses/license-list.html#GNUGPL\">GNU GPL v2 or v3</a>"
      "</html>" ).arg( qApp->applicationVersion() ).arg( QDateTime::currentDateTime().toString("yyyy-MM-dd") ));
}

void BcDebugger::onQt()
{
    ENABLED_IF(true);
    QMessageBox::aboutQt(this,tr("About the Qt Framework") );
}

void BcDebugger::onSetMain()
{
    ENABLED_IF(true);

    bool ok;
    QString main = QInputDialog::getText( this, tr("Set Main Function"), tr("A Lua statement:"),QLineEdit::Normal,
                           d_runCmd, &ok );
    if( ok )
        d_runCmd = main.toUtf8();
}

void BcDebugger::onQuit()
{
    ENABLED_IF(!d_lua->isExecuting());

    qApp->quit();
}

#ifndef LUAIDE_EMBEDDED
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/LjTools");
    a.setApplicationName("LuaJIT Bytecode Debugger");
    a.setApplicationVersion("0.1");
    a.setStyle("Fusion");

    BcDebugger w;

    if( a.arguments().size() > 1 )
        w.loadFile(a.arguments()[1] );

    return a.exec();
}
#endif // LUAIDE_EMBEDDED
