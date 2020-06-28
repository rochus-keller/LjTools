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

#include "LuaIde.h"
#include "LuaHighlighter.h"
#include "LuaProject.h"
#include "LjasFileCache.h"
#include "LjasErrors.h"
#include <LjTools/Engine2.h>
#include <LjTools/Terminal2.h>
#include <LjTools/BcViewer2.h>
#include <LjTools/BcViewer.h>
#include <LjTools/LuaJitEngine.h>
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
using namespace Lua;

#ifdef Q_OS_MAC
#define OBN_BREAK_SC "SHIFT+F8"
#define OBN_ABORT_SC "CTRL+SHIFT+Y"
#define OBN_CONTINUE_SC "CTRL+Y"
#define OBN_STEPIN_SC "CTRL+SHIFT+I"
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
#define OBN_ENDBG_SC "F8"
#define OBN_TOGBP_SC "F9"
#define OBN_GOBACK_SC "ALT+Left"
#define OBN_GOFWD_SC "ALT+Right"
#define OBN_NEXTDOC_SC "CTRL+TAB"
#define OBN_PREVDOC_SC "CTRL+SHIFT+TAB"
#endif

enum { ROW_BIT_LEN = 19, COL_BIT_LEN = 32 - ROW_BIT_LEN - 1, MSB = 0x80000000 };
static quint32 packRowCol(quint32 row, quint32 col )
{
    static const quint32 maxRow = ( 1 << ROW_BIT_LEN ) - 1;
    static const quint32 maxCol = ( 1 << COL_BIT_LEN ) - 1;
    Q_ASSERT( row <= maxRow && col <= maxCol );
    return ( row << COL_BIT_LEN ) | col | MSB;
}

static inline QString relativeToAbsolutePath( QString path )
{
    QFileInfo info(path);
    if( info.isRelative() )
        path = QDir::cleanPath(QDir::current().absoluteFilePath(path));
    return path;
}

struct ScopeRef : public Module::Ref<Module::Scope>
{
    ScopeRef(Module::Scope* s = 0):Ref(s) {}
};
Q_DECLARE_METATYPE(ScopeRef)
struct ExRef : public Module::Ref<Module::Thing>
{
    ExRef(Module::Thing* n = 0):Ref(n) {}
};
Q_DECLARE_METATYPE(ExRef)

class LuaIde::Editor : public CodeEditor
{
public:
    Editor(LuaIde* p, Project* pro):CodeEditor(p),d_pro(pro),d_ide(p)
    {
        setCharPerTab(4);
        setTypingLatency(400);
        setPaintIndents(false);
        d_hl = new Highlighter( document() );
        updateTabWidth();
    }

    ~Editor()
    {
    }

    LuaIde* d_ide;
    Highlighter* d_hl;
    Project* d_pro;

    void clearBackHisto()
    {
        d_backHisto.clear();
    }


    typedef QList<Module::Thing*> ExList;

    void markNonTerms(const ExList& syms)
    {
        d_nonTerms.clear();
        QTextCharFormat format;
        format.setBackground( QColor(237,235,243) );
        foreach( Module::Thing* s, syms )
        {
            QTextCursor c( document()->findBlockByNumber( s->d_tok.d_lineNr - 1) );
            c.setPosition( c.position() + s->d_tok.d_colNr - 1 );
            int pos = c.position();
            c.setPosition( pos + s->d_tok.d_val.size(), QTextCursor::KeepAnchor );

            QTextEdit::ExtraSelection sel;
            sel.format = format;
            sel.cursor = c;

            d_nonTerms << sel;
        }
        updateExtraSelections();
    }

    void updateExtraSelections()
    {
        ESL sum;

        QTextEdit::ExtraSelection line;
        line.format.setBackground(QColor(Qt::yellow).lighter(170));
        line.format.setProperty(QTextFormat::FullWidthSelection, true);
        line.cursor = textCursor();
        line.cursor.clearSelection();
        sum << line;

        sum << d_nonTerms;

        if( !d_pro->getErrs()->getErrors().isEmpty() )
        {
            QTextCharFormat errorFormat;
            errorFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            errorFormat.setUnderlineColor(Qt::magenta);
            Ljas::Errors::EntryList l = d_pro->getErrs()->getErrors(getPath());
            for( int i = 0; i < l.size(); i++ )
            {
                QTextCursor c( document()->findBlockByNumber(l[i].d_line - 1) );

                c.setPosition( c.position() + l[i].d_col - 1 );
                c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);

                QTextEdit::ExtraSelection sel;
                sel.format = errorFormat;
                sel.cursor = c;
                sel.format.setToolTip(l[i].d_msg);

                sum << sel;
            }
        }

        sum << d_link;

        setExtraSelections(sum);
    }

    void mousePressEvent(QMouseEvent* e)
    {
        if( !d_link.isEmpty() )
        {
            QTextCursor cur = cursorForPosition(e->pos());
            d_ide->pushLocation( LuaIde::Location( getPath(), cur.blockNumber(), cur.positionInBlock() ) );
            QApplication::restoreOverrideCursor();
            d_link.clear();
        }
        if( QApplication::keyboardModifiers() == Qt::ControlModifier )
        {
            QTextCursor cur = cursorForPosition(e->pos());
            Module::Ref<Module::Thing> e = d_pro->findSymbolBySourcePos(
                        getPath(),cur.blockNumber() + 1,cur.positionInBlock() + 1);
            if( e && e->getTag() == Module::Thing::T_SymbolUse )
            {
                Module::Thing* sym = static_cast<Module::SymbolUse*>(e.data())->d_sym;
                d_ide->pushLocation( LuaIde::Location( getPath(), cur.blockNumber(), cur.positionInBlock() ) );
                d_ide->showEditor( sym, false, false );
                //setCursorPosition( sym->d_loc.d_row - 1, sym->d_loc.d_col - 1, true );
            }
            updateExtraSelections();
        }else
            QPlainTextEdit::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e)
    {
        QPlainTextEdit::mouseMoveEvent(e);
        if( QApplication::keyboardModifiers() == Qt::ControlModifier )
        {
            QTextCursor cur = cursorForPosition(e->pos());
            Module::Ref<Module::Thing> thing = d_pro->findSymbolBySourcePos(
                        getPath(),cur.blockNumber() + 1,cur.positionInBlock() + 1);
            const bool alreadyArrow = !d_link.isEmpty();
            d_link.clear();
            if( thing )
            {
                Module::Thing* sym = thing.data();
                const int off = cur.positionInBlock() + 1 - thing->d_tok.d_colNr;
                cur.setPosition(cur.position() - off);
                cur.setPosition( cur.position() + sym->d_tok.d_val.size(), QTextCursor::KeepAnchor );

                QTextEdit::ExtraSelection sel;
                sel.cursor = cur;
                sel.format.setFontUnderline(true);
                d_link << sel;
                /*
                d_linkLineNr = sym->d_loc.d_row - 1;
                d_linkColNr = sym->d_loc.d_col - 1;
                */
                if( !alreadyArrow )
                    QApplication::setOverrideCursor(Qt::ArrowCursor);
            }
            if( alreadyArrow && d_link.isEmpty() )
                QApplication::restoreOverrideCursor();
            updateExtraSelections();
        }else if( !d_link.isEmpty() )
        {
            QApplication::restoreOverrideCursor();
            d_link.clear();
            updateExtraSelections();
        }
    }

    void onUpdateModel()
    {
        d_ide->compile();
        if( !d_nonTerms.isEmpty() && !d_pro->getErrs()->getErrors().isEmpty() )
        {
            d_nonTerms.clear();
            updateExtraSelections();
        }
    }
};

class LuaIde::DocTab : public DocTabWidget
{
public:
    DocTab(QWidget* p):DocTabWidget(p,false) {}

    // overrides
    bool isUnsaved(int i)
    {
        LuaIde::Editor* edit = static_cast<LuaIde::Editor*>( widget(i) );
        return edit->isModified();
    }

    bool save(int i)
    {
        LuaIde::Editor* edit = static_cast<LuaIde::Editor*>( widget(i) );
        if( !edit->saveToFile( edit->getPath(), false ) )
            return false;
        return true;
    }
};

class LuaIde::Debugger : public DbgShell
{
public:
    LuaIde* d_ide;
    Debugger(LuaIde* ide):d_ide(ide){}
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

static LuaIde* s_this = 0;
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

static void loadLuaLib( Lua::Engine2* lua, const QByteArray& name )
{
    QFile lib( QString(":/scripts/%1.lua").arg(name.constData()) );
    lib.open(QIODevice::ReadOnly);
    if( !lua->addSourceLib( lib.readAll(), name ) )
        qCritical() << "compiling" << name << ":" << lua->getLastError();
}

static bool preloadLib( Project* pro, const QByteArray& name )
{
    QFile f( QString(":/oakwood/%1.Def" ).arg(name.constData() ) );
    if( !f.open(QIODevice::ReadOnly) )
    {
        qCritical() << "unknown preload" << name;
        return false;
    }
    pro->getFc()->addFile( name, f.readAll() );
    return true;
}

LuaIde::LuaIde(Engine2* lua, QWidget *parent)
    : QMainWindow(parent),d_lock(false),d_filesDirty(false),d_pushBackLock(false)
{
    s_this = this;

    d_pro = new Project(this);

    if( lua )
        d_lua = lua;
    else
    {
        d_lua = new Engine2(this);
        d_lua->addStdLibs();
        d_lua->addLibrary(Engine2::PACKAGE);
        d_lua->addLibrary(Engine2::IO);
        d_lua->addLibrary(Engine2::BIT);
        d_lua->addLibrary(Engine2::JIT);
        d_lua->addLibrary(Engine2::FFI);
        d_lua->addLibrary(Engine2::OS);
        Engine2::setInst(d_lua);
    }
    lua_pushcfunction( d_lua->getCtx(), Engine2::TRAP );
    lua_setglobal( d_lua->getCtx(), "TRAP" );
    d_pro->addBuiltIn("TRAP");

    d_dbg = new Debugger(this);
    d_lua->setDbgShell(d_dbg);
    // d_lua->setAliveSignal(true); // reduces performance by factor 2 to 5
    connect( d_lua, SIGNAL(onNotify(int,QByteArray,int)),this,SLOT(onLuaNotify(int,QByteArray,int)) );

    d_tab = new DocTab(this);
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
    connect( d_dbgStepIn, SIGNAL(triggered(bool)),this,SLOT(onSingleStep()) );

    enableDbgMenu();

    createTerminal();
    createDumpView();
    createMods();
    createErrs();
    createXref();
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


    connect( d_pro,SIGNAL(sigRenamed()),this,SLOT(onCaption()) );
    connect( d_pro,SIGNAL(sigModified(bool)),this,SLOT(onCaption()) );
}

LuaIde::~LuaIde()
{
    d_lua->setDbgShell(0);
    delete d_dbg;
}

void LuaIde::loadFile(const QString& path)
{
    QFileInfo info(path);

    if( info.isDir() && info.suffix() != ".luapro" )
    {
        d_pro->initializeFromDir( path );
    }else
    {
        d_pro->loadFrom(path);
    }

    onCaption();

    onCompile();
}

void LuaIde::logMessage(const QString& str, bool err)
{
    d_term->printText(str,err);
}

void LuaIde::closeEvent(QCloseEvent* event)
{
    QSettings s;
    s.setValue( "DockState", saveState() );
    const bool ok = checkSaved( tr("Quit Application"));
    event->setAccepted(ok);
    if( ok )
    {
        d_lua->terminate(true);
        //SysInnerLib::quit();
    }
}

void LuaIde::createTerminal()
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

void LuaIde::createDumpView()
{
    QDockWidget* dock = new QDockWidget( tr("Bytecode"), this );
    dock->setObjectName("Bytecode");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_bcv = new BcViewer2(dock);
    dock->setWidget(d_bcv);
    addDockWidget( Qt::RightDockWidgetArea, dock );
    connect(d_bcv,SIGNAL(sigGotoLine(quint32)),this,SLOT(onGotoLnr(quint32)));

    Gui::AutoMenu* pop = new Gui::AutoMenu( d_bcv, true );
    pop->addCommand( "Run on LuaJIT", this, SLOT(onRun()), tr("CTRL+R"), false );
    addDebugMenu(pop);
    pop->addSeparator();
    pop->addCommand( "Show low level bytecode", this, SLOT(onShowLlBc()) );
    //pop->addCommand( "Export binary...", this, SLOT(onExportBc()) );
    //pop->addCommand( "Export LjAsm...", this, SLOT(onExportAsm()) );
    addTopCommands(pop);
}

void LuaIde::createMods()
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

void LuaIde::createErrs()
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

void LuaIde::createXref()
{
    QDockWidget* dock = new QDockWidget( tr("Xref"), this );
    dock->setObjectName("Xref");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    QWidget* pane = new QWidget(dock);
    QVBoxLayout* vbox = new QVBoxLayout(pane);
    vbox->setMargin(0);
    vbox->setSpacing(0);
    d_xrefTitle = new QLabel(pane);
    d_xrefTitle->setMargin(2);
    d_xrefTitle->setWordWrap(true);
    vbox->addWidget(d_xrefTitle);
    d_xref = new QTreeWidget(pane);
    d_xref->setAlternatingRowColors(true);
    d_xref->setHeaderHidden(true);
    d_xref->setAllColumnsShowFocus(true);
    d_xref->setRootIsDecorated(false);
    vbox->addWidget(d_xref);
    dock->setWidget(pane);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
    connect(d_xref, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(onXrefDblClicked()) );
}

void LuaIde::createStack()
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

void LuaIde::createLocals()
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

void LuaIde::createMenu()
{
    Gui::AutoMenu* pop = new Gui::AutoMenu( d_mods, true );
    pop->addCommand( "Show File", this, SLOT(onOpenFile()) );
    pop->addAction("Expand all", d_mods, SLOT(expandAll()) );
    pop->addSeparator();
    pop->addCommand( "New Project", this, SLOT(onNewPro()), tr("CTRL+N"), false );
    pop->addCommand( "Open Project...", this, SLOT(onOpenPro()), tr("CTRL+O"), false );
    pop->addCommand( "Save Project", this, SLOT(onSavePro()), tr("CTRL+SHIFT+S"), false );
    pop->addCommand( "Save Project as...", this, SLOT(onSaveAs()) );
    pop->addSeparator();
    pop->addCommand( "Add Modules...", this, SLOT(onAddFiles()) );
    pop->addCommand( "Remove Module...", this, SLOT(onRemoveFile()) );
    pop->addSeparator();
    pop->addCommand( "Set Main Function...", this, SLOT( onSetMain() ) );
    pop->addCommand( "Set Working Directory...", this, SLOT( onWorkingDir() ) );
    pop->addSeparator();
    pop->addCommand( "Compile", this, SLOT(onCompile()), tr("CTRL+T"), false );
    pop->addCommand( "Compile && Generate", this, SLOT(onGenerate()), tr("CTRL+SHIFT+T"), false );
    pop->addCommand( "Run on LuaJIT", this, SLOT(onRun()), tr("CTRL+R"), false );
    addDebugMenu(pop);
    addTopCommands(pop);

    new Gui::AutoShortcut( tr("CTRL+O"), this, this, SLOT(onOpenPro()) );
    new Gui::AutoShortcut( tr("CTRL+N"), this, this, SLOT(onNewPro()) );
    new Gui::AutoShortcut( tr("CTRL+SHIFT+S"), this, this, SLOT(onSavePro()) );
    new Gui::AutoShortcut( tr("CTRL+S"), this, this, SLOT(onSaveFile()) );
    new Gui::AutoShortcut( tr("CTRL+R"), this, this, SLOT(onRun()) );
    new Gui::AutoShortcut( tr("CTRL+T"), this, this, SLOT(onCompile()) );
    new Gui::AutoShortcut( tr("CTRL+SHIFT+T"), this, this, SLOT(onGenerate()) );
    new Gui::AutoShortcut( tr(OBN_GOBACK_SC), this, this, SLOT(handleGoBack()) );
    new Gui::AutoShortcut( tr(OBN_GOFWD_SC), this, this, SLOT(handleGoForward()) );
    new Gui::AutoShortcut( tr(OBN_TOGBP_SC), this, this, SLOT(onToggleBreakPt()) );
    new Gui::AutoShortcut( tr(OBN_ENDBG_SC), this, this, SLOT(onEnableDebug()) );
}

void LuaIde::createMenuBar()
{
    Gui::AutoMenu* pop = new Gui::AutoMenu( tr("File"), this );
    pop->addCommand( "New Project", this, SLOT(onNewPro()), tr("CTRL+N"), false );
    pop->addCommand( "Open Project...", this, SLOT(onOpenPro()), tr("CTRL+O"), false );
    pop->addCommand( "Save Project", this, SLOT(onSavePro()), tr("CTRL+SHIFT+S"), false );
    pop->addCommand( "Save Project as...", this, SLOT(onSaveAs()) );
    pop->addSeparator();
    pop->addCommand( "Save", this, SLOT(onSaveFile()), tr("CTRL+S"), false );
    pop->addCommand( tr("Close file"), d_tab, SLOT(onCloseDoc()), tr("CTRL+W") );
    pop->addCommand( tr("Close all"), d_tab, SLOT(onCloseAll()) );
    pop->addSeparator();
    //pop->addCommand( "Export binary...", this, SLOT(onExportBc()) );
    //pop->addCommand( "Export LjAsm...", this, SLOT(onExportAsm()) );
    pop->addSeparator();
    pop->addAutoCommand( "Print...", SLOT(handlePrint()), tr("CTRL+P"), true );
    pop->addAutoCommand( "Export PDF...", SLOT(handleExportPdf()), tr("CTRL+SHIFT+P"), true );
    pop->addSeparator();
    pop->addAction(tr("Quit"),qApp,SLOT(quit()), tr("CTRL+Q") );

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

    pop = new Gui::AutoMenu( tr("Project"), this );
    pop->addCommand( "Add Modules...", this, SLOT(onAddFiles()) );
    pop->addCommand( "Remove Module...", this, SLOT(onRemoveFile()) );
    pop->addSeparator();
    pop->addCommand( "Set Main Function...", this, SLOT( onSetMain() ) );
    pop->addCommand( "Set Working Directory...", this, SLOT( onWorkingDir() ) );

    pop = new Gui::AutoMenu( tr("Build && Run"), this );
    pop->addCommand( "Compile", this, SLOT(onCompile()), tr("CTRL+T"), false );
    pop->addCommand( "Compile && Generate", this, SLOT(onGenerate()), tr("CTRL+SHIFT+T"), false );
    pop->addCommand( "Run on LuaJIT", this, SLOT(onRun()), tr("CTRL+R"), false );

    pop = new Gui::AutoMenu( tr("Debug"), this );
    pop->addCommand( "Enable Debugging", this, SLOT(onEnableDebug()),tr(OBN_ENDBG_SC), false );
    pop->addCommand( "Toggle Breakpoint", this, SLOT(onToggleBreakPt()), tr(OBN_TOGBP_SC), false);
    pop->addAction( d_dbgStepIn );
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

void LuaIde::onCompile()
{
    ENABLED_IF(true);
    compile();
}

void LuaIde::onRun()
{
    ENABLED_IF( !d_pro->getFiles().isEmpty() && !d_lua->isExecuting() );

    if( !compile(true) )
        return;

    QDir::setCurrent(d_pro->getWorkingDir(true));


    bool hasErrors = false;
    foreach( const QString& path, d_pro->getFileOrder() )
    {
        if( !d_lua->executeFile( path.toUtf8() ) )
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

    Project::ModProc main = d_pro->getMain();

    QByteArray src;
    QTextStream out(&src);

    //out << "jit.off()" << endl;
    //out << "jit.opt.start(3)" << endl;
    //out << "jit.opt.start(\"-abc\")" << endl;
    //out << "jit.opt.start(\"-fuse\")" << endl;
    //out << "jit.opt.start(\"hotloop=10\", \"hotexit=2\")" << endl;

    if( !main.first.isEmpty() )
    {
        out << "local " << main.first << " = require '" << main.first << "'" << endl;
        out << main.first << "." << main.second << "()" << endl;
    }else if( !main.second.isEmpty() )
        out << main.second << "()" << endl;
    out.flush();
    if( !src.isEmpty() )
        d_lua->executeCmd(src,"terminal");
    removePosMarkers();

}

void LuaIde::onAbort()
{
    // ENABLED_IF( d_lua->isWaiting() );
    d_lua->terminate();
}

void LuaIde::onGenerate()
{
    ENABLED_IF(true);
    compile(true);
}

void LuaIde::onNewPro()
{
    ENABLED_IF(true);

    if( !checkSaved( tr("New Project")) )
        return;

    d_pro->createNew();
    d_tab->onCloseAll();
    compile();
}

void LuaIde::onOpenPro()
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
    d_pro->loadFrom(fileName);

    compile();
}

void LuaIde::onSavePro()
{
    ENABLED_IF( d_pro->isDirty() );

    if( !d_pro->getFilePath().isEmpty() )
        d_pro->save();
    else
        onSaveAs();
}

void LuaIde::onSaveFile()
{
    Editor* edit = static_cast<Editor*>( d_tab->getCurrentTab() );
    ENABLED_IF( edit && edit->isModified() );

    edit->saveToFile( edit->getPath() );
    d_pro->getFc()->removeFile( edit->getPath() );
}

void LuaIde::onSaveAs()
{
    ENABLED_IF(true);

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                                          QFileInfo(d_pro->getFilePath()).absolutePath(),
                                                          tr("Lua Project (*.luapro)") );

    if (fileName.isEmpty())
        return;

    QDir::setCurrent(QFileInfo(fileName).absolutePath());

    if( !fileName.endsWith(".luapro",Qt::CaseInsensitive ) )
        fileName += ".luapro";

    d_pro->saveTo(fileName);
    onCaption();
}

void LuaIde::onCaption()
{
    const QString star = d_pro->isDirty() || d_filesDirty ? "*" : "";
    if( d_pro->getFilePath().isEmpty() )
    {
        setWindowTitle(tr("<unnamed>%2 - %1").arg(qApp->applicationName()).arg(star));
    }else
    {
        QFileInfo info(d_pro->getFilePath());
        setWindowTitle(tr("%1%2 - %3").arg(info.fileName()).arg(star).arg(qApp->applicationName()) );
    }
}

void LuaIde::onGotoLnr(quint32 lnr)
{
    if( d_lock )
        return;
    d_lock = true;
    Editor* edit = static_cast<Editor*>( d_tab->getCurrentTab() );
    if( edit )
    {
        edit->setCursorPosition(lnr-1,0);
        edit->setFocus();
    }
    d_lock = false;
}

void LuaIde::onFullScreen()
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

void LuaIde::onCursor()
{
    fillXref();
    if( d_lock )
        return;
    d_lock = true;
    Editor* edit = static_cast<Editor*>( d_tab->getCurrentTab() );
    if( edit )
    {
        QTextCursor cur = edit->textCursor();
        const int line = cur.blockNumber() + 1;
        d_bcv->gotoLine(line);
    }
    d_lock = false;
}

void LuaIde::onModsDblClicked(QTreeWidgetItem* item, int)
{
    ScopeRef s = item->data(0,Qt::UserRole).value<ScopeRef>();
    if( s.isNull() )
        return;

    showEditor( s.data(), false, true );
}

void LuaIde::onStackDblClicked(QTreeWidgetItem* item, int)
{
    if( item )
    {
        const QString source = item->data(3,Qt::UserRole).toString();
        if( !source.isEmpty() )
        {
            const quint32 line = item->data(2,Qt::UserRole).toUInt();
            showEditor( source, line, 1 );
        }
        const int level = item->data(0,Qt::UserRole).toInt();
        d_lua->setActiveLevel(level);
        fillLocals();
    }
}

void LuaIde::onTabChanged()
{
    const QString path = d_tab->getCurrentDoc().toString();

    onEditorChanged();

    if( !path.isEmpty() )
    {
        QByteArray bc; //  TODO
        if( !bc.isEmpty() )
        {
            QBuffer buf( &bc );
            buf.open(QIODevice::ReadOnly);
            d_bcv->loadFrom(&buf);
            onCursor();
            return;
        }
    }
    // else
    d_bcv->clear();
}

void LuaIde::onTabClosing(int i)
{
    d_pro->getFc()->removeFile( d_tab->getDoc(i).toString() );
}

void LuaIde::onEditorChanged()
{
    // only fired once when editor switches from unmodified to modified and back
    // not fired for every key press
    d_filesDirty = false;
    for( int i = 0; i < d_tab->count(); i++ )
    {
        Editor* e = static_cast<Editor*>( d_tab->widget(i) );
        if( e->isModified() )
            d_filesDirty = true;
        QFileInfo info( d_tab->getDoc(i).toString() );
        d_tab->setTabText(i, info.fileName() + ( e->isModified() ? "*" : "" ) );
    }
    onCaption();
}

void LuaIde::onErrorsDblClicked()
{
    QTreeWidgetItem* item = d_errs->currentItem();
    showEditor( item->data(0, Qt::UserRole ).toString(),
                item->data(1, Qt::UserRole ).toInt(), item->data(2, Qt::UserRole ).toInt() );
}

void LuaIde::onErrors()
{
    d_errs->clear();
    Ljas::Errors::EntryList errs = d_pro->getErrs()->getAll();

    for( int i = 0; i < errs.size(); i++ )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_errs);
        item->setText(2, errs[i].d_msg );
        item->setToolTip(2, item->text(2) );
        if( errs[i].d_isErr )
            item->setIcon(0, QPixmap(":/images/exclamation-red.png") );
        else
            item->setIcon(0, QPixmap(":/images/exclamation-circle.png") );
        item->setText(0, QFileInfo(errs[i].d_file).baseName() );
        item->setText(1, QString("%1:%2").arg(errs[i].d_line).arg(errs[i].d_col));
        item->setData(0, Qt::UserRole, errs[i].d_file );
        item->setData(1, Qt::UserRole, errs[i].d_line );
        item->setData(2, Qt::UserRole, errs[i].d_col );
    }
    if( errs.size() )
        d_errs->parentWidget()->show();

    for( int i = 0; i < d_tab->count(); i++ )
    {
        Editor* e = static_cast<Editor*>( d_tab->widget(i) );
        Q_ASSERT( e );
        e->updateExtraSelections();
    }
}

void LuaIde::onOpenFile()
{
    ENABLED_IF( d_mods->currentItem() );

    onModsDblClicked( d_mods->currentItem(), 0 );
}

void LuaIde::onAddFiles()
{
    ENABLED_IF(true);

    QString filter;
    foreach( const QString& suf, d_pro->getSuffixes() )
        filter += " *" + suf;
    const QStringList files = QFileDialog::getOpenFileNames(this,tr("Add Modules"),QString(),filter );
    foreach( const QString& f, files )
    {
        if( !d_pro->addFile(f) )
            qWarning() << "cannot add module" << f;
    }
    compile();
}

void LuaIde::onRemoveFile()
{
    ENABLED_IF( d_mods->currentItem() );

    ScopeRef s = d_mods->currentItem()->data(0,Qt::UserRole).value<ScopeRef>();
    if( s.isNull() )
        return;

    QString path = s->d_tok.d_sourcePath;
    if( path.isEmpty() )
        return;

    if( QMessageBox::warning( this, tr("Remove Module"),
                              tr("Do you really want to remove module '%1' from project?").arg(QFileInfo(path).baseName()),
                           QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes ) != QMessageBox::Yes )
        return;
    if( !d_pro->removeFile( path ) )
        qWarning() << "cannot remove module" << path;
    else
        compile();
}

void LuaIde::onEnableDebug()
{
    CHECKED_IF( true, d_lua->isDebug() );

    d_lua->setDebug( !d_lua->isDebug() );
    enableDbgMenu();
}

void LuaIde::onBreak()
{
    // normal call because called during processEvent which doesn't seem to enable
    // the functions: ENABLED_IF( d_lua->isExecuting() );
    d_lua->runToNextLine();
}

bool LuaIde::checkSaved(const QString& title)
{
    if( d_filesDirty )
    {
        switch( QMessageBox::critical( this, title, tr("There are modified files; do you want to save them?"),
                               QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes ) )
        {
        case QMessageBox::Yes:
            // TODO
            break;
        case QMessageBox::No:
            break;
        default:
            return false;
        }
    }
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
    return true;
}

bool LuaIde::compile(bool generate )
{
    for( int i = 0; i < d_tab->count(); i++ )
    {
        Editor* e = static_cast<Editor*>( d_tab->widget(i) );
        if( e->isModified() )
            d_pro->getFc()->addFile( e->getPath(), e->toPlainText().toUtf8() );
        else
            d_pro->getFc()->removeFile( e->getPath() );
    }
    d_pro->recompile();
    onErrors();
    fillMods();
    onTabChanged();
    return d_pro->getErrs()->getErrCount() == 0;
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

static void fillScope( QTreeWidgetItem* p, Module* m, Module::Scope* s )
{
    QList<Module::Thing*> sort;
    for( int j = 0; j < s->d_locals.size(); j++ )
        sort << s->d_locals[j].data();
    if( m )
    {
        foreach( const Module::Ref<Module::Function>& f, m->getNonLocals() )
            sort << f.data();
    }
    std::sort( sort.begin(), sort.end(), sortNamed );
    foreach( Module::Thing* n, sort )
    {
        if( n->getTag() == Module::Thing::T_Function )
        {
            QTreeWidgetItem* item = new QTreeWidgetItem( p );
            if( n->d_tok.d_val.isEmpty() )
                item->setText(0, QString("<function %1>").arg(n->d_tok.d_lineNr) );
            else
                item->setText(0, n->d_tok.d_val );
            Module::Scope* s = static_cast<Module::Scope*>(n);
            item->setData(0,Qt::UserRole, QVariant::fromValue( ScopeRef(s)) );
            fillScope(item, 0, s );
        }
    }
}

void LuaIde::fillMods()
{
    d_mods->clear();
    foreach( const QString& path, d_pro->getFileOrder() )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_mods);
        item->setText(0, QFileInfo(path).baseName());
        item->setToolTip(0,path);
        Module* m = d_pro->getFiles().value(path);
        Q_ASSERT( m != 0 );
        ScopeRef s(m->getTopChunk());
        item->setData(0,Qt::UserRole,QVariant::fromValue(s) );
        fillScope( item, m, s.data() );
    }
}

void LuaIde::addTopCommands(Gui::AutoMenu* pop)
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
    pop->addAction(tr("Quit"),qApp,SLOT(quit()) );
}

void LuaIde::showEditor(const QString& path, int row, int col, bool setMarker , bool center)
{
    if( !d_pro->getFiles().contains(path) )
        return;

    const int i = d_tab->findDoc(path);
    Editor* edit = 0;
    if( i != -1 )
    {
        d_tab->setCurrentIndex(i);
        edit = static_cast<Editor*>( d_tab->widget(i) );
    }else
    {
        edit = new Editor(this,d_pro);
        createMenu(edit);

        connect(edit, SIGNAL(modificationChanged(bool)), this, SLOT(onEditorChanged()) );
        connect(edit,SIGNAL(cursorPositionChanged()),this,SLOT(onCursor()));
        connect(edit,SIGNAL(sigUpdateLocation(int,int)),this,SLOT(onUpdateLocation(int,int)));

        edit->loadFromFile(path);

        const Engine2::Breaks& br = d_lua->getBreaks( path.toUtf8() );
        Engine2::Breaks::const_iterator j;
        for( j = br.begin(); j != br.end(); ++j )
            edit->addBreakPoint((*j) - 1);

        d_tab->addDoc(edit,path);
        onEditorChanged();
    }
    if( row > 0 && col > 0 )
    {
        edit->setCursorPosition( row-1, col-1, center );
        if( setMarker )
            edit->setPositionMarker(row-1);
    }
    edit->setFocus();
}

void LuaIde::showEditor(Module::Thing* n, bool setMarker, bool center)
{
    showEditor( n->d_tok.d_sourcePath, n->d_tok.d_lineNr, n->d_tok.d_colNr, setMarker, center );
}

void LuaIde::createMenu(LuaIde::Editor* edit)
{
    Gui::AutoMenu* pop = new Gui::AutoMenu( edit, true );
    pop->addCommand( "Save", this, SLOT(onSaveFile()), tr("CTRL+S"), false );
    pop->addSeparator();
    pop->addCommand( "Compile", this, SLOT(onCompile()), tr("CTRL+T"), false );
    pop->addCommand( "Run on LuaJIT", this, SLOT(onRun()), tr("CTRL+R"), false );
    addDebugMenu(pop);
    pop->addSeparator();
    pop->addCommand( "Undo", edit, SLOT(handleEditUndo()), tr("CTRL+Z"), true );
    pop->addCommand( "Redo", edit, SLOT(handleEditRedo()), tr("CTRL+Y"), true );
    pop->addSeparator();
    pop->addCommand( "Cut", edit, SLOT(handleEditCut()), tr("CTRL+X"), true );
    pop->addCommand( "Copy", edit, SLOT(handleEditCopy()), tr("CTRL+C"), true );
    pop->addCommand( "Paste", edit, SLOT(handleEditPaste()), tr("CTRL+V"), true );
    pop->addSeparator();
    pop->addCommand( "Find...", edit, SLOT(handleFind()), tr("CTRL+F"), true );
    pop->addCommand( "Find again", edit, SLOT(handleFindAgain()), tr("F3"), true );
    pop->addCommand( "Replace...", edit, SLOT(handleReplace()) );
    pop->addSeparator();
    pop->addCommand( "&Goto...", edit, SLOT(handleGoto()), tr("CTRL+G"), true );
    pop->addSeparator();
    pop->addCommand( "Indent", edit, SLOT(handleIndent()) );
    pop->addCommand( "Unindent", edit, SLOT(handleUnindent()) );
    pop->addCommand( "Fix Indents", edit, SLOT(handleFixIndent()) );
    pop->addCommand( "Set Indentation Level...", edit, SLOT(handleSetIndent()) );
    pop->addSeparator();
    pop->addCommand( "Print...", edit, SLOT(handlePrint()), tr("CTRL+P"), true );
    pop->addCommand( "Export PDF...", edit, SLOT(handleExportPdf()), tr("CTRL+SHIFT+P"), true );
    addTopCommands(pop);
}

void LuaIde::addDebugMenu(Gui::AutoMenu* pop)
{
    Gui::AutoMenu* sub = new Gui::AutoMenu(tr("Debugger"), this, false );
    pop->addMenu(sub);
    sub->addCommand( "Enable Debugging", this, SLOT(onEnableDebug()),tr(OBN_ENDBG_SC), false );
    sub->addCommand( "Toggle Breakpoint", this, SLOT(onToggleBreakPt()), tr(OBN_TOGBP_SC), false);
    sub->addAction( d_dbgStepIn );
    sub->addAction( d_dbgBreak );
    sub->addAction( d_dbgContinue );
    sub->addAction( d_dbgAbort );

}

bool LuaIde::luaRuntimeMessage(const QByteArray& msg, const QString& file )
{
    const int rbrack = msg.indexOf(']'); // cannot directly search for ':' because Windows "C:/"
    if( rbrack != -1 )
    {
        if( msg.startsWith("[string") )
        {
            d_pro->getErrs()->error(Ljas::Errors::Runtime, "[string]", 0, 0, msg );
            return true;
        }

        const int firstColon = msg.indexOf(':', rbrack);
        if( firstColon != -1 )
        {
            const int secondColon = msg.indexOf(':',firstColon + 1);
            if( secondColon != -1 )
            {
                QString path = msg.left(firstColon);
                const int firstTick = path.indexOf('"');
                if( firstTick != -1 )
                {
                    const int secondTick = path.indexOf('"',firstTick+1);
                    path = path.mid(firstTick+1,secondTick-firstTick-1);
                    path = relativeToAbsolutePath(path);
                }else
                    path.clear();
                const qint32 line = msg.mid(firstColon+1, secondColon - firstColon - 1 ).toInt(); // lua deliveres negative numbers

                d_pro->getErrs()->error(Ljas::Errors::Runtime, path.isEmpty() ? file : path, line, 0,
                                        msg.mid(secondColon+1) );
                return true;
            }
        }
    }
    // /home/me/Smalltalk/Interpreter.lua:37: module 'ObjectMemory' not found: no field
    QRegExp reg(":[0-9]+:");
    const int lineNr = reg.indexIn(msg);
    if( lineNr != -1 )
    {
        const QString path = relativeToAbsolutePath(msg.left(lineNr));
        const QString cap = reg.cap();
        d_pro->getErrs()->error(Ljas::Errors::Runtime, path.isEmpty() ? file : path,
                                cap.mid(1,cap.size()-2).toInt(), 1,
                                msg.mid(lineNr+reg.matchedLength()).trimmed() );
        return true;
    }
    d_pro->getErrs()->error(Ljas::Errors::Runtime, file, 0, 0, msg );
    return true;
    // qWarning() << "Unknown Lua error message format:" << msg;
}

static bool sortExList( const Module::Thing* lhs, Module::Thing* rhs )
{
    const QString ln = lhs->d_tok.d_sourcePath;
    const QString rn = rhs->d_tok.d_sourcePath;
    const quint32 ll = packRowCol( lhs->d_tok.d_lineNr, lhs->d_tok.d_colNr );
    const quint32 rl = packRowCol( rhs->d_tok.d_lineNr, rhs->d_tok.d_colNr );

    return ln < rn || (!(rn < ln) && ll < rl);
}

void LuaIde::fillXref()
{
    Editor* edit = static_cast<Editor*>( d_tab->getCurrentTab() );
    if( edit == 0 )
    {
        d_xref->clear();
        d_xrefTitle->clear();
        return;
    }
    int line, col;
    edit->getCursorPosition( &line, &col );
    line += 1;
    col += 1;
    Module::Thing* hitSym = d_pro->findSymbolBySourcePos(edit->getPath(), line, col);
    if( hitSym )
    {
        Module::Thing* refSym = hitSym;
        if( refSym->getTag() == Module::Thing::T_SymbolUse )
            refSym = static_cast<Module::SymbolUse*>(refSym)->d_sym;
        Editor::ExList l1, l2;
        l1 << refSym;
        l2 << refSym;
        foreach( const Module::Ref<Module::SymbolUse>& e, refSym->d_uses )
        {
            l2 << e.data();
            if( e->d_tok.d_sourcePath == edit->getPath() )
                l1 << e.data();
        }

        edit->markNonTerms(l1);

        std::sort( l2.begin(), l2.end(), sortExList );

        QFont f = d_xref->font();
        f.setBold(true);

        QString type;
        switch( refSym->getTag() )
        {
        case Module::Thing::T_Variable:
            type = "Local Var";
            break;
        case Module::Thing::T_GlobalSym:
            {
                Module::GlobalSym* s = static_cast<Module::GlobalSym*>(refSym);
                if( s->d_builtIn )
                    type = "BuiltIn";
                else
                    type = "Global";
            }
            break;
        case Module::Thing::T_Function:
            {
                Module::Function* f = static_cast<Module::Function*>(refSym);
                switch( f->d_kind )
                {
                case Module::Function::Local:
                    type = "Local Func";
                    break;
                case Module::Function::NonLocal:
                    type = "Function";
                    break;
                case Module::Function::Global:
                    type = "Global Func";
                    break;
                }
            }
            break;
        }

        d_xrefTitle->setText(QString("%1 '%2'").arg(type).arg(refSym->d_tok.d_val.constData()));

        d_xref->clear();
        foreach( Module::Thing* e, l2 )
        {
            QTreeWidgetItem* i = new QTreeWidgetItem(d_xref);
            i->setText( 0, QString("%1 (%2:%3%4)")
                        .arg(QFileInfo(e->d_tok.d_sourcePath).baseName())
                        .arg(e->d_tok.d_lineNr).arg(e->d_tok.d_colNr)
                        .arg( e->isImplicitDecl() ? " idecl" : e == refSym ? " decl" : e->isLhsUse() ? " lhs" : "" ));
            if( e == hitSym )
                i->setFont(0,f);
            i->setToolTip( 0, i->text(0) );
            i->setData( 0, Qt::UserRole, QVariant::fromValue( ExRef(e) ) );
            if( e->d_tok.d_sourcePath != edit->getPath() )
                i->setForeground( 0, Qt::gray );
        }
    }
}

void LuaIde::fillStack()
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
            item->setText(2,QString("%1").arg(l.d_line));
            item->setData(2, Qt::UserRole, l.d_line );
            QString path = l.d_source;
            if( path.startsWith('@') )
                path = path.mid(1);
            path = relativeToAbsolutePath(path);
            item->setText(3, QFileInfo(path).baseName() );
            item->setData(3, Qt::UserRole, path );
            item->setToolTip(3, path );
            if( !opened )
            {
                showEditor(l.d_source, l.d_line, 0, true );
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
            item->setToolTip(1, QString("address 0x%1").arg(ptrdiff_t(addr.d_addr),8,16,QChar('0')));
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
        }
    }else if( val.type() == QMetaType::QVariantMap)
    {
        QVariantMap map = val.toMap();
        typeAddr( item, map.value(QString()) );
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
            item->setText(1,i.value().toString());
    }
}

void LuaIde::fillLocals()
{
    d_locals->clear();
    Engine2::LocalVars vs = d_lua->getLocalVars(true,2,50);
    foreach( const Engine2::LocalVar& v, vs )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_locals);
        QString name = v.d_name;
        if( v.d_isUv )
            name = "(" + name + ")";
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
            item->setText(1, "\"" + v.d_value.toString().simplified() + "\"");
            item->setToolTip(1, v.d_value.toString() );
        }else if( !v.d_value.isNull() )
            item->setText(1,v.d_value.toString());
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
            case Engine2::LocalVar::STRING:
                item->setText(1, "\"" + v.d_value.toString().simplified() + "\"");
                break;
            default:
                break;
           }
        }
    }
}

void LuaIde::removePosMarkers()
{
    for( int i = 0; i < d_tab->count(); i++ )
    {
        Editor* e = static_cast<Editor*>( d_tab->widget(i) );
        e->setPositionMarker(-1);
    }
}

void LuaIde::enableDbgMenu()
{
    d_dbgBreak->setEnabled(!d_lua->isWaiting() && d_lua->isExecuting() && d_lua->isDebug() );
    d_dbgAbort->setEnabled(d_lua->isWaiting());
    d_dbgContinue->setEnabled(d_lua->isWaiting());
    d_dbgStepIn->setEnabled(d_lua->isWaiting() && d_lua->isDebug() );
}

void LuaIde::handleGoBack()
{
    ENABLED_IF( d_backHisto.size() > 1 );

    d_pushBackLock = true;
    d_forwardHisto.push_back( d_backHisto.last() );
    d_backHisto.pop_back();
    showEditor( d_backHisto.last().d_file, d_backHisto.last().d_line+1, d_backHisto.last().d_col+1 );
    d_pushBackLock = false;
}

void LuaIde::handleGoForward()
{
    ENABLED_IF( !d_forwardHisto.isEmpty() );

    Location cur = d_forwardHisto.last();
    d_forwardHisto.pop_back();
    showEditor( cur.d_file, cur.d_line+1, cur.d_col+1 );
}

void LuaIde::onUpdateLocation(int line, int col)
{
    Editor* e = static_cast<Editor*>( sender() );
    e->clearBackHisto();
    pushLocation(Location(e->getPath(), line,col));
}

void LuaIde::onXrefDblClicked()
{
    QTreeWidgetItem* item = d_xref->currentItem();
    if( item )
    {
        ExRef e = item->data(0,Qt::UserRole).value<ExRef>();
        Q_ASSERT( !e.isNull() );
        showEditor( e->d_tok.d_sourcePath, e->d_tok.d_lineNr, e->d_tok.d_colNr );
    }
}

void LuaIde::onToggleBreakPt()
{
    Editor* edit = static_cast<Editor*>( d_tab->getCurrentTab() );
    ENABLED_IF( edit );

    quint32 line;
    const bool on = edit->toggleBreakPoint(&line);
    if( on )
        d_lua->addBreak( edit->getPath().toUtf8(), line + 1 );
    else
        d_lua->removeBreak( edit->getPath().toUtf8(), line + 1 );
}

void LuaIde::onSingleStep()
{
    // ENABLED_IF( d_lua->isWaiting() );

    d_lua->runToNextLine();
}

void LuaIde::onContinue()
{
    // ENABLED_IF( d_lua->isWaiting() );

    d_lua->runToBreakPoint();
}

void LuaIde::onShowLlBc()
{
    ENABLED_IF( d_bcv->topLevelItemCount() );

    BcViewer* bc = new BcViewer();
    QBuffer buf; // TODO
    buf.open(QIODevice::ReadOnly);
    bc->loadFrom( &buf );
    bc->show();
    bc->setAttribute(Qt::WA_DeleteOnClose);
}

void LuaIde::onWorkingDir()
{
    ENABLED_IF(true);

    bool ok;
    const QString res = QInputDialog::getText(this,tr("Set Working Directory"), QString(), QLineEdit::Normal,
                                              d_pro->getWorkingDir(), &ok );
    if( !ok )
        return;
    d_pro->setWorkingDir(res);
}

void LuaIde::onLuaNotify(int messageType, QByteArray val1, int val2)
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

void LuaIde::pushLocation(const LuaIde::Location& loc)
{
    if( d_pushBackLock )
        return;
    if( !d_backHisto.isEmpty() && d_backHisto.last() == loc )
        return; // o ist bereits oberstes Element auf dem Stack.
    d_backHisto.removeAll( loc );
    d_backHisto.push_back( loc );
}

void LuaIde::onAbout()
{
    ENABLED_IF(true);

    QMessageBox::about( this, qApp->applicationName(),
      tr("<html>Release: %1   Date: %2<br><br>"

      "Welcome to the Lua IDE.<br>"
      "See <a href=\"https://github.com/rochus-keller/LjTools\">"
         "here</a> for more information.<br><br>"

      "Author: Rochus Keller, me@rochus-keller.ch<br><br>"

      "Licese: <a href=\"https://www.gnu.org/licenses/license-list.html#GNUGPL\">GNU GPL v2 or v3</a>"
      "</html>" ).arg( qApp->applicationVersion() ).arg( QDateTime::currentDateTime().toString("yyyy-MM-dd") ));
}

void LuaIde::onQt()
{
    ENABLED_IF(true);
    QMessageBox::aboutQt(this,tr("About the Qt Framework") );
}

void LuaIde::onSetMain()
{
    ENABLED_IF(true);

    bool ok;
    QString main = QInputDialog::getText( this, tr("Set Main Function"), tr("[module.]function:"),QLineEdit::Normal,
                           d_pro->formatMain(), &ok );
    if( ok )
    {
        QStringList parts = main.split('.');
        if( parts.size() > 2 )
        {
            QMessageBox::critical(this,tr("Set Main Function"), tr("invalid main function format") );
            return;
        }
        Project::ModProc modProc;
        if( parts.size() == 2 )
        {
            modProc.first = parts.first().toUtf8();
            modProc.second = parts.last().toUtf8();
        }else
            modProc.second = parts.first().toUtf8();
        d_pro->setMain(modProc);
    }
}

#ifndef LUAIDE_EMBEDDED
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/LjTools");
    a.setApplicationName("Lua IDE");
    a.setApplicationVersion("0.1");
    a.setStyle("Fusion");

    LuaIde w;

    if( a.arguments().size() > 1 )
        w.loadFile(a.arguments()[1] );

    return a.exec();
}
#endif // LUAIDE_EMBEDDED
