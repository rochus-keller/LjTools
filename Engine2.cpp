/*
* Copyright 2019, 2020 Rochus Keller <mailto:me@rochus-keller.ch>
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

#include <lua.hpp>
#include "LuaJitComposer.h"
#include "Engine2.h"
#include <QCoreApplication>
#include <math.h>
#include <QtDebug>
#include <iostream>
#include <QTime>
#include <QFileInfo>
#include <QDir>

using namespace Lua;

static Engine2* s_this = 0;

static const char* s_path = "path";
static const char* s_cpath = "cpath";
static Engine2::Breaks s_dummy;
static const int s_aliveCount = 10000;

int Engine2::_print (lua_State *L)
{
	Engine2* e = Engine2::getInst();
    Q_ASSERT( e != 0 );
    QByteArray val1;
	int n = lua_gettop(L);  /* number of arguments */
	int i;
	lua_getglobal(L, "tostring");
	for (i=1; i<=n; i++) 
	{
		const char *s;
		lua_pushvalue(L, -1);  /* function to be called */
		lua_pushvalue(L, i);   /* value to print */
		lua_call(L, 1, 1);
		s = lua_tostring(L, -1);  /* get result */
		if (s == NULL)
			return luaL_error(L, "`tostring' must return a string to `print'");
		if (i>1) 
            val1 += "\t";
        val1 += s;
		lua_pop(L, 1);  /* pop result */
	}
    if( !val1.endsWith('\n') )
        val1 += '\n';
	try
	{
		e->notify( Print, val1 );
    }catch( std::exception& e )
	{
		luaL_error( L, "Error calling host: %s", e.what() );
	}catch( ... )
	{
		luaL_error( L, "Unknown exception while calling host" );
	}

    return 0;
}

static int dbgout(lua_State* L)
{
    const int top = lua_gettop(L);

    QByteArray buf;
    QTextStream ts(&buf,QIODevice::WriteOnly);
    for( int i = 1; i <= top; i++ )
    {
        if( i != 1 )
            ts << "\t";
        switch( lua_type(L,i) )
        {
        case LUA_TNIL:
            ts << "nil";
            break;
        case LUA_TSTRING:
            {
                const char* str = lua_tostring(L,i);
                ts << str;
            }
            break;
        case LUA_TBOOLEAN:
            ts << ( lua_toboolean(L,i) ? true : false );
            break;
        case LUA_TNUMBER:
            {
                const lua_Number n = lua_tonumber(L,i);
                const int nn = n;
                if( n == lua_Number(nn) )
                    ts << nn;
                else
                    ts << n;
            }
            break;
        case LUA_TLIGHTUSERDATA:
            ts << "LUA_TLIGHTUSERDATA";
            break;
        case LUA_TTABLE:
            ts << "LUA_TTABLE";
            break;
        case LUA_TFUNCTION:
            ts << "LUA_TFUNCTION";
            break;
        case LUA_TUSERDATA:
            ts << "LUA_TUSERDATA";
            break;
        case LUA_TTHREAD:
            ts << "LUA_TTHREAD";
            break;
        default:
            ts << "<unknown>";
            break;
        }
    }
    ts.flush();

    qDebug() << buf.constData();

    return 0;
}

static int _flush(lua_State* L)
{
    return 0; // NOP
}

int Engine2::_writeStdout(lua_State* L)
{
    return _writeImp(L,false);
}

int Engine2::_writeStderr(lua_State* L)
{
    return _writeImp(L,true);
}

int Engine2::_writeImp(lua_State *L, bool err) {
    QByteArray buf;
    QTextStream out(&buf);
    int arg = 2;
    int nargs = lua_gettop(L) - 1;
    for (; nargs--; arg++) {
        if (lua_type(L, arg) == LUA_TNUMBER) {
            /* optimization: could be done exactly as for strings */
            out << lua_tonumber(L, arg);
        }
        else {
            size_t l;
            const char *s = luaL_checklstring(L, arg, &l);
            out << s;
        }
    }
    out.flush();
    Engine2* e = Engine2::getInst();
    Q_ASSERT( e != 0 );
    try
    {
        e->notify( err ? Engine2::Cerr : Engine2::Cout, buf );
    }catch( std::exception& e )
    {
        luaL_error( L, "Error calling host: %s", e.what() );
    }catch( ... )
    {
        luaL_error( L, "Unknown exception while calling host" );
    }
    return 0;
}

int Engine2::_prettyTraceLoc(lua_State* L)
{
    const QByteArray loc = lua_tostring( L, 1 );
    const QByteArray source = lua_tostring( L, 2 );
    //const int linedefined = lua_tointeger( L, 3 );
    const int colon = loc.lastIndexOf(':');
    if( colon == -1 )
        lua_pushvalue(L,1);
    else
    {
        const int line = loc.mid(colon+1).toInt();
        QByteArray res = loc.left(colon+1);
        if( res.startsWith("0x") && !source.isEmpty() )
            res = QFileInfo(source).fileName().toUtf8() + ":"; // rhs is already linedefined
        if( JitComposer::isPacked(line) )
            res +=  QByteArray::number( JitComposer::unpackRow(line) ) + ":"
                    + QByteArray::number( JitComposer::unpackCol(line) );
        else
            res += QByteArray::number(line);
        lua_pushstring( L, res.constData() );
    }
    return 1;
}

Engine2::Engine2(QObject *p):QObject(p),
    d_ctx( 0 ), d_debugging( false ), d_running(false), d_waitForCommand(false),
    d_dbgCmd(RunToBreakPoint), d_defaultDbgCmd(RunToBreakPoint), d_activeLevel(0), d_dbgShell(0),
    d_printToStdout(false), d_aliveSignal(false), d_mode(LineMode), d_aliveCount(0), d_stepCallDepth(0),
    d_stepOverSync(false)
{
    if( !restart() )
        throw Exception( "failed to create engine" );
}

Engine2::~Engine2()
{
	if( d_ctx )
		lua_close( d_ctx );
}

void Engine2::addStdLibs()
{
	addLibrary( TABLE );
	addLibrary( STRING );
    addLibrary( MATH );
}

bool Engine2::restart()
{
    // necessary because there seems to be no other way to get rid of ffi.cdef declarations
    if( isExecuting() )
        return false;

    if( d_ctx )
    {
        lua_close( d_ctx );
        d_ctx = 0;
    }

    lua_State* ctx = lua_open();
    if( ctx == 0 )
    {
        qCritical() << "Not enough memory to create Lua context";
        return false;
    }

    LUAJIT_VERSION_SYM();

    d_ctx = ctx;

    addLibrary( BASE );	// Das muss hier stehen, sonst wird ev. print wieder überschrieben

#ifndef LUA_ENGINE_USE_DEFAULT_PRINT
    lua_pushcfunction( ctx, _print );
    lua_setglobal( ctx, "print" );
#endif
    lua_pushcfunction( ctx, dbgout );
    lua_setglobal( ctx, "dbgout" );
    lua_pushcfunction( ctx, _prettyTraceLoc );
    lua_setglobal( ctx, "_prettyTraceLoc" );

    if( d_debugging )
    {
        d_debugging = false;
        setDebug(true);
    }
    if( d_aliveSignal )
    {
        d_aliveSignal = false;
        setAliveSignal(true);
    }
    return true;
}

void Engine2::addLibrary(Lib what)
{
    if( d_running )
        return;
	switch( what )
	{
    case BIT:
        lua_pushcfunction( d_ctx, luaopen_bit );
        lua_pushstring(d_ctx, LUA_BITLIBNAME );
        lua_call(d_ctx, 1, 0);
        break;
    case JIT:
        lua_pushcfunction( d_ctx, luaopen_jit );
        lua_pushstring(d_ctx, LUA_JITLIBNAME );
        lua_call(d_ctx, 1, 0);
        break;
    case FFI:
        lua_pushcfunction( d_ctx, luaopen_ffi );
        lua_pushstring(d_ctx, LUA_FFILIBNAME );
        lua_call(d_ctx, 1, 0);
        break;

#if LUA_VERSION_NUM >= 501
	case PACKAGE:
		lua_pushcfunction( d_ctx, luaopen_package );
		lua_pushstring(d_ctx, LUA_LOADLIBNAME );
		lua_call(d_ctx, 1, 0);

        // am 28.12.11 Konzept geändert. Neu werden wieder Lua-Standard-Loader verwendet.
		break;
#endif
	case BASE:
		lua_pushcfunction( d_ctx, luaopen_base );
		lua_pushstring(d_ctx, "" );
		lua_call(d_ctx, 1, 0);
		break;
	case REMOVE_LOADS:
		lua_pushnil( d_ctx );
		lua_setglobal( d_ctx, "dofile" );
		lua_pushnil( d_ctx );
		lua_setglobal( d_ctx, "loadfile" );
		lua_pushnil( d_ctx );
		lua_setglobal( d_ctx, "load" );
		lua_pushnil( d_ctx );
		lua_setglobal( d_ctx, "loadstring" );
		break;
	case TABLE:
		lua_pushcfunction( d_ctx, luaopen_table );
		lua_pushstring(d_ctx, LUA_TABLIBNAME );
		lua_call(d_ctx, 1, 0);
		break;
	case STRING:
		lua_pushcfunction( d_ctx, luaopen_string );
		lua_pushstring(d_ctx, LUA_STRLIBNAME );
		lua_call(d_ctx, 1, 0);
		break;
	case MATH:
		lua_pushcfunction( d_ctx, luaopen_math );
		lua_pushstring(d_ctx, LUA_MATHLIBNAME );
		lua_call(d_ctx, 1, 0);
		break;
	case IO:
		lua_pushcfunction( d_ctx, luaopen_io );
		lua_pushstring(d_ctx, LUA_IOLIBNAME );
        lua_call(d_ctx, 1, 0);
#ifndef LUA_ENGINE_USE_DEFAULT_PRINT
        // redirect stdout
        lua_createtable(d_ctx,0,1); // file
        lua_pushcfunction( d_ctx, _writeStdout );
        lua_setfield(d_ctx, -2, "write" );
        lua_pushcfunction( d_ctx, _flush );
        lua_setfield(d_ctx, -2, "flush" );
        lua_getglobal(d_ctx, LUA_IOLIBNAME );
        lua_pushvalue(d_ctx,-2);
        lua_setfield(d_ctx, -2, "stdout" );
        lua_pop(d_ctx,2); // file + io
        // redirect stderr
        lua_createtable(d_ctx,0,1); // file
        lua_pushcfunction( d_ctx, _writeStderr );
        lua_setfield(d_ctx, -2, "write" );
        lua_pushcfunction( d_ctx, _flush );
        lua_setfield(d_ctx, -2, "flush" );
        lua_getglobal(d_ctx, LUA_IOLIBNAME );
        lua_pushvalue(d_ctx,-2);
        lua_setfield(d_ctx, -2, "stderr" );
        lua_pop(d_ctx,2); // file + io
#endif
        break;
#if LUA_VERSION_NUM >= 501
	case OS:
		lua_pushcfunction( d_ctx, luaopen_os );
		lua_pushstring(d_ctx, LUA_OSLIBNAME );
		lua_call(d_ctx, 1, 0);
		break;
#endif
	case DBG:
        lua_pushcfunction( d_ctx, luaopen_debug );
        lua_pushstring(d_ctx, LUA_DBLIBNAME );
        lua_call(d_ctx, 1, 0);
        break;
        break;
    default:
        break;
	}
}

bool Engine2::pushFunction(const QByteArray &source, const QByteArray& name )
{
	d_lastError = "";
    const int status = luaL_loadbuffer( d_ctx, source, source.size(), name );
    switch( status )
    {
    case 0:
        // Stack: function
        break;
    case LUA_ERRSYNTAX:
    case LUA_ERRMEM:
        d_lastError = lua_tostring( d_ctx, -1 );
        lua_pop( d_ctx, 1 );  /* remove error message */
        // Stack: -
        return false;
    }
    return true;
}

const char* Engine2::getVersion() const
{
#if LUA_VERSION_NUM >= 501
	return LUA_RELEASE;
#else
	return lua_version();
#endif
}

static int file_writer (lua_State *L, const void* b, size_t size, void* f) 
{
    Q_UNUSED(L);
    int res = ::fwrite( b, size, 1, (FILE*)f );
    if( res != 1 )
		return -1; // = error
	return 0; // = no error
}

bool Engine2::saveBinary(const QByteArray& source, const QByteArray& name, const QByteArray& path)
{
    d_lastError.clear();
    FILE* f = ::fopen( path,"wb" );
    if( f == 0 )
    {
        d_lastError = "Unable to open file for writing";
        return false;
    }
    if( !pushFunction( source, name ) )
        return false;

    // Stack: Function
    const int res = lua_dump( d_ctx, file_writer, f);
    ::fclose( f );
    lua_pop( d_ctx, 1 ); // Function
#if LUA_VERSION_NUM >= 501
    if( res != 0 )
#else
    if( res != 1 )
#endif
    {
        d_lastError = "Unable to write compiled script";
        return false;
    }else
        return true;
}

bool Engine2::executeCmd(const QByteArray &source, const QByteArray &name)
{
    if( d_running )
    {
        d_lastError = "Cannot run commands while another script is running!";
        error( d_lastError );
        return false;
    }
    d_lastError.clear();
    d_waitForCommand = false;
    d_aliveCount = false;
    if( !pushFunction( source, name ) )
    {
        error( d_lastError );
        return false;
    }
	const bool res = runFunction( 0, LUA_MULTRET );
    if( !res )
        error( d_lastError );
    return res;
}

bool Engine2::executeFile(const QByteArray &path)
{
    if( d_running )
    {
        d_lastError = "Cannot run script while another script is running!";
        error( d_lastError );
        return false;
    }
    d_lastError.clear();
    d_waitForCommand = false;
    d_aliveCount = 0;
    switch( luaL_loadfile( d_ctx, path ) )
    {
    case 0: // no Error
        break;
    case LUA_ERRFILE:
    case LUA_ERRSYNTAX:
    case LUA_ERRMEM:
        d_lastError = lua_tostring( d_ctx, -1);
        lua_pop( d_ctx, 1 );  /* remove error message */
        error( d_lastError );
        return false;
    }
	const bool res = runFunction( 0, LUA_MULTRET );
    if( !res )
        error( d_lastError );
    return res;
}

bool Engine2::runFunction(int nargs, int nresults)
{
    if( d_running )
    {
        lua_pop(d_ctx, 1 + nargs);
        return false;
    }

    int preTop = lua_gettop( d_ctx );
	if( d_waitForCommand )
    {
		d_lastError = "Cannot run another Lua function while script is waiting in debugger!";
		lua_pop( d_ctx, nargs + 1 ); // funktion + args
		if( nresults != LUA_MULTRET )
			Q_ASSERT( lua_gettop( d_ctx ) == ( preTop - nargs - 1 ) );
        return false;
    }

    lua_pushcfunction( d_ctx, ErrHandler );
    const int errf = preTop-nargs;
    lua_insert(d_ctx,errf);
    preTop = lua_gettop( d_ctx );

	d_lastError.clear();
	d_dbgCmd = d_defaultDbgCmd;
    d_returns.clear();
	notifyStart();

    // Lua: All arguments and the function value are popped from the stack when the function is called.
    int err = lua_pcall( d_ctx, nargs, nresults, errf );
    switch( err )
    {
    case LUA_ERRRUN:
        if( d_dbgCmd != AbortSilently)
        {
            if( d_dbgCmd == Abort )
                d_lastError = "Execution terminated by user";
            else
                d_lastError = lua_tostring( d_ctx, -1 );
            lua_pop( d_ctx, 1 );  /* remove error message */
            lua_pop( d_ctx, 1 ); // remove ErrHandler
            nresults = 0;
            notifyEnd();
            return false;
        }//else
        err = 0;
        break;
    case LUA_ERRMEM:
        d_lastError = "Lua memory exception";
        notifyEnd();
        return false;
    case LUA_ERRERR:
        // should not happen
        d_lastError = "Lua unknown error";
        notifyEnd();
        return false;
    }
    // func + nargs were popped by pcall; nresults were pushed
    preTop = preTop - 1 - nargs;
    int postTop = lua_gettop( d_ctx );
    for( int i = preTop + 1; i <= postTop; i++ )
        d_returns << getValueString( i );
    if( postTop - preTop )
        lua_pop(d_ctx, postTop - preTop );
    lua_pop( d_ctx, 1 ); // remove ErrHandler
    notifyEnd();
    return (err == 0);
}

bool Engine2::addSourceLib(const QByteArray& source, const QByteArray& libname)
{
    if( d_waitForCommand )
    {
        d_lastError = "Cannot run another Lua function while script is waiting in debugger!";
        return false;
    }

    const int prestack = lua_gettop(d_ctx);
    lua_pushcfunction( d_ctx, ErrHandler );
    const int errf = lua_gettop(d_ctx);

    d_lastError.clear();
    d_returns.clear();

    if( !pushFunction( source, libname ) )
        return false;

    d_dbgCmd = d_defaultDbgCmd;
    d_aliveCount = 0;
    notifyStart();

    const int err = lua_pcall( d_ctx, 0, 1, errf );
    switch( err )
    {
    case LUA_ERRRUN:
        d_lastError = lua_tostring( d_ctx, -1 );
        lua_pop( d_ctx, 1 );  /* remove error message */
        lua_pop( d_ctx, 1 ); // remove ErrHandler
        Q_ASSERT( prestack == lua_gettop(d_ctx) );
        notifyEnd();
        return false;
    case LUA_ERRMEM:
        d_lastError = "Lua memory exception";
        notifyEnd();
        return false;
    case LUA_ERRERR:
        // should not happen
        d_lastError = "Lua unknown error";
        notifyEnd();
        return false;
    }
    int stack = lua_gettop(d_ctx);
    // stack: lib
    // sets it as the value of the global variable libname, sets it as the value of package.loaded[libname]
    lua_pushvalue(d_ctx, -1 ); // stack: lib lib
    lua_setfield(d_ctx, LUA_GLOBALSINDEX, libname.constData() ); // stack: lib
    lua_getfield(d_ctx, LUA_GLOBALSINDEX, "package"); // stack: lib package
    lua_getfield(d_ctx, -1, "loaded"); // stack: lib package loaded
    lua_pushvalue(d_ctx, -3 ); // stack: lib package loaded lib
    lua_setfield(d_ctx, -2, libname.constData() ); // stack: lib package loaded
    lua_pop(d_ctx,3); // stack: -
    lua_pop( d_ctx, 1 ); // remove ErrHandler
    stack = lua_gettop(d_ctx);

    Q_ASSERT( prestack == stack );
    notifyEnd();

    return true;
}

void Engine2::collect()
{
	if( d_ctx )
		lua_gc( d_ctx, LUA_GCCOLLECT, 0 );
}

void Engine2::setActiveLevel(int level)
{
    if( d_activeLevel == level )
        return;
    d_activeLevel = level;
    notify( ActiveLevel, d_curScript, lineForNotify() );
}

int Engine2::ErrHandler( lua_State* L )
{
    // This function leaves the error message just where it is

    Engine2* e = Engine2::getInst();

    //const char* msg = lua_tostring(L, 1 ); // TEST

    if( e == 0 )
    {
        qWarning() << "Engine2::ErrHandler: getInst returns 0";
        return 1;
    }

    if( e->d_dbgCmd == Abort || e->d_dbgCmd == AbortSilently )
        return 1; // don't break if user wants to abort

    StackLevel l = e->getStackLevel(0,false);

    e->d_breakHit = false;
    e->d_curScript = l.d_source;
    e->d_curRowCol = l.d_line;
    e->d_activeLevel = 0;
    e->d_waitForCommand = true;
    e->notify( ErrorHit, e->d_curScript, e->lineForNotify() );
    if( e->d_dbgShell )
        e->d_dbgShell->handleBreak( e, e->d_curScript, e->lineForBreak() );

    e->d_waitForCommand = false;
    if( e->d_dbgCmd == Abort || e->d_dbgCmd == AbortSilently )
        e->notify( Aborted );
    else
        e->notify( Continued );

    return 1;
}

void Engine2::debugHook(lua_State *L, lua_Debug *ar)
{
    Engine2* e = Engine2::getInst();
    Q_ASSERT( e != 0 );

    const StackLevel l = e->getStackLevel(0,false,ar);

#if 0
    {
        QByteArray action;
        int depth = e->d_stepCallDepth;
        switch(ar->event)
        {
        case LUA_HOOKRET:
            if( !l.d_inC )
                depth--;
            action = "RETURN";
            break;
        case LUA_HOOKCALL:
            if( !l.d_inC )
                depth++;
            if( l.d_inC )
                action = "NATIVE_CALL";
            else
                action = "CALL";
            break;
        case LUA_HOOKLINE:
            action = "LINE";
            break;
        case LUA_HOOKCOUNT:
            action = "COUNT";
            break;
        case LUA_HOOKTAILRET:
            action = "TAILRET";
            break;
        }

        qDebug() << "Engine2::debugHook" << "calldepth" << depth << action
                 << l.d_source << JitComposer::unpackRow2(l.d_line) << JitComposer::unpackCol2(l.d_line);
    }
#endif

    if( ar->event == LUA_HOOKCALL )
    {
        // HOOKRET comes when already in the function to be called
        if( e->d_dbgCmd == StepOut || e->d_dbgCmd == StepOver )
        {
            if( !l.d_inC )
                e->d_stepCallDepth++;
        }
        return;
    }
    if( ar->event == LUA_HOOKRET ) // LUA_HOOKRET isn't fired in case of CALLT!!! Use _LJTOOLS_DONT_CREATE_TAIL_CALLS to avoid.
    {
        // HOOKRET comes when still in the returning function; even the pc is still the same as the previous HOOKLINE
        if( e->d_dbgCmd == StepOut || e->d_dbgCmd == StepOver )
        {
            Q_ASSERT( !l.d_inC && !e->d_stepBreak.first.isEmpty() ); // see runToNextLine

            e->d_stepCallDepth--;
            if(  e->d_stepCallDepth < 0 && e->d_stepBreak.first == l.d_source &&
                    e->d_stepBreak.second == l.d_lineDefined )
                e->d_dbgCmd = Engine2::StepNext;
                // Change StepOut to StepNext if we leave the function in which StepOut was called (i.e. callDepth<0)
                // Change StepOver to StepNext if we're about to leave the function where StepOver was called (i.e. callDepth<0)
            else if( e->d_dbgCmd == StepOver && e->d_stepCallDepth == 0 )
            {
                // if we step over a procedure call reset curLine/Script to the original value to avoid that
                // we break twice on the line of the function call (by avoiding lineChanged to become true)
                e->d_curScript = e->d_stepBreak.first;
                e->d_curRowCol = e->d_stepCurRowCol;
                e->d_dbgCmd = Engine2::StepNext;
                e->d_stepOverSync = true;
            }
        }
        return;
    }
    Q_ASSERT( ar->event == LUA_HOOKLINE || ar->event == LUA_HOOKCOUNT );

    e->d_aliveCount++;

    bool lineChanged = false;
    switch( e->d_mode )
    {
    case LineMode:
        if( e->d_stepOverSync && e->d_stepCallDepth == 0 && JitComposer::isPacked(l.d_line) )
            e->d_dbgCmd = StepOver; // happens if arg or a call is yet another call on the same line
        else
            lineChanged = ( JitComposer::unpackRow2(e->d_curRowCol) != JitComposer::unpackRow2(l.d_line) ||
                        e->d_curScript != l.d_source );
        break;
    case RowColMode:
    case PcMode:
        lineChanged = ( unpackDeflinePc(e->d_curRowCol).second != l.d_line || e->d_curScript != l.d_source );
        break;
    }

    e->d_curScript = l.d_source;
    if( e->d_mode == PcMode )
        e->d_curRowCol = packDeflinePc( JitComposer::unpackRow2(l.d_lineDefined),l.d_line);
    else
        e->d_curRowCol = l.d_line;

    if( e->d_mode == PcMode || lineChanged )
    {
        e->d_breakHit = false;
        e->d_activeLevel = 0;

        const quint32 line = e->lineForBreak();
        if( e->d_dbgCmd == Engine2::StepNext ||
                ( e->d_dbgCmd == Engine2::StepOver && e->d_stepCallDepth == 0 &&
                                e->d_stepBreak.first == l.d_source &&
                                e->d_stepBreak.second == l.d_lineDefined ) )
                // the Engine2::StepOver case is still relevant here because we can step over non-calls too.
        {
            e->d_waitForCommand = true;
            e->d_breakHit = true;
            e->notify( LineHit, e->d_curScript, e->lineForNotify() );
            if( e->d_dbgShell )
                e->d_dbgShell->handleBreak( e, e->d_curScript, line );
            e->d_waitForCommand = false;
        }else if( e->d_breaks.value( e->d_curScript ).contains( line ) )
        {
            e->d_waitForCommand = true;
            e->d_breakHit = true;
            e->notify( BreakHit, e->d_curScript,e->lineForNotify() );
            if( e->d_dbgShell )
                e->d_dbgShell->handleBreak( e, e->d_curScript, line );
            e->d_waitForCommand = false;
        }
        if( e->d_dbgCmd == Abort || e->d_dbgCmd == AbortSilently )
        {
            lua_pushnil(L);
            lua_error(L);
        }
        e->notify( Continued );
    }else if( /* e->d_aliveSignal && */ e->d_aliveCount > s_aliveCount / 2 && e->d_dbgShell )
    {
        // even if aliveSignal is not enabled it is necessary to give calculation time to the shell
        // from time to time during long runs without breaks
        e->d_dbgShell->handleAliveSignal( e );
        e->d_aliveCount = 0;
#if 0
        // why should this be useful?
        if( e->isStepping() )
        {
            e->d_waitForCommand = true;
            e->d_breakHit = true;
            e->notify( LineHit, e->d_curScript, e->d_curLine );
            if( e->d_dbgShell )
                e->d_dbgShell->handleBreak( e, e->d_curScript, e->d_curLine );
            e->d_waitForCommand = false;
        }
#endif
    }
    e->d_stepOverSync = false;
    //else // this doesn't seem to be necessary, even counterproductive
    //    lua_pop(L, 1); // function
}

void Engine2::aliveSignal(lua_State* L, lua_Debug* ar)
{
    Engine2* e = Engine2::getInst();
    Q_ASSERT( e != 0 );
    if( e->d_dbgShell )
    {
        e->d_dbgShell->handleAliveSignal(e);
        if( e->isStepping() )
        {
            e->d_waitForCommand = true;
            e->notify( LineHit, e->d_curScript, e->lineForNotify() );
            e->d_dbgShell->handleBreak( e, e->d_curScript, e->lineForBreak() );
            e->d_waitForCommand = false; // TODO
        }
    }
}

static int array_writer (lua_State *L, const void* b, size_t size, void* f)
{
	Q_UNUSED(L);
	QByteArray* ba = (QByteArray*)f;
	ba->append( QByteArray( (const char *) b, size ) );
	return 0; // = no error
}

QByteArray Engine2::getBinaryFromFunc(lua_State *L)
{
	if( lua_type(L, -1) != LUA_TFUNCTION )
		return QByteArray();

	QByteArray ba;
	const int res = lua_dump( L, array_writer, &ba );
	if( res != 0 )
		ba.clear();
	return ba;
}

void Engine2::notifyStart()
{
    d_running = true;
    notify( Started );
}

void Engine2::notifyEnd()
{
    d_running = false;
	notify( (d_dbgCmd == Abort || d_dbgCmd == AbortSilently)? Aborted : Finished );
}

void Engine2::setDebug(bool on)
{
    if( d_debugging == on )
        return;
    if( on )
    {
        if( d_mode == PcMode )
            lua_sethook( d_ctx, debugHook, LUA_MASKCOUNT | LUA_MASKRET | LUA_MASKCALL, 1);
        else
            lua_sethook( d_ctx, debugHook, LUA_MASKLINE | LUA_MASKRET | LUA_MASKCALL, 1);
    }else if( d_aliveSignal )
        lua_sethook( d_ctx, aliveSignal, LUA_MASKCOUNT, s_aliveCount); // get's a hook call with each bytecode op when 1
    else
        lua_sethook( d_ctx, 0, 0, 0);
    d_debugging = on;

}

void Engine2::setJit(bool on)
{
    int flags = LUAJIT_MODE_ENGINE;
    if( on )
        flags |= LUAJIT_MODE_ON;
    else
        flags |= LUAJIT_MODE_OFF;
    luaJIT_setmode( d_ctx, 0, flags );
}

void Engine2::setAliveSignal(bool on)
{
    if( d_aliveSignal == on )
        return;
    d_aliveSignal = on;
    if( d_debugging )
        return;
    d_aliveCount = 0;
    if( on )
        lua_sethook( d_ctx, aliveSignal, LUA_MASKCOUNT, s_aliveCount);
    else
        lua_sethook( d_ctx, aliveSignal, 0, 0);
}

void Engine2::setDebugMode(Engine2::Mode m)
{
    d_mode = m;
    if( d_debugging )
    {
        setDebug(false);
        setDebug(true);
    }
}

void Engine2::setBytecodeMode(bool on)
{
    d_mode = on ? PcMode : LineMode;
    if( d_debugging )
    {
        setDebug(false);
        setDebug(true);
    }
}

void Engine2::runToNextLine(DebugCommand where)
{
    Q_ASSERT( where == StepNext || where == StepOver || where == StepOut );
    d_stepBreak = Break();
    if( where == StepOver || where == StepOut )
    {
        StackLevel l = getStackLevel(0,false);
        if( l.d_inC )
        {
            // an FFI C call apparently never issues a LUA_HOOKLINE or LUA_HOOKRET (but a LUA_HOOKCALL)
            where = StepNext;
        }else
        {
            // remember in which function StepOver or StepOut were called
            d_stepBreak.first = l.d_source;
            d_stepBreak.second = l.d_lineDefined;
            d_stepCurRowCol = d_curRowCol;
        }
    }
    d_dbgCmd = where;
    d_waitForCommand = false;
    d_stepCallDepth = 0;
}

void Engine2::runToBreakPoint()
{
    d_dbgCmd = RunToBreakPoint;
    d_waitForCommand = false;
}

int Engine2::TRAP(lua_State* L)
{
    Engine2* e = Engine2::getInst();

    if( e->d_dbgShell == 0 )
        return 0; // ignored if there is no debugger

    if( e->d_dbgCmd == Abort || e->d_dbgCmd == AbortSilently )
        return 0; // don't break if user wants to abort

    bool doIt = true;
    if( lua_gettop(L) >= 1 )
        doIt = lua_toboolean(L,-1);
    if( !doIt )
        return 0;
    StackLevel l = e->getStackLevel(0,false);

    e->d_curScript = l.d_source;
    e->d_curRowCol = l.d_line;
    e->d_activeLevel = 0;
    e->d_waitForCommand = true;
    e->d_breakHit = true;
    e->setDebug(true);
    e->notify( BreakHit, e->d_curScript, e->lineForNotify() );
    if( e->d_dbgShell )
        e->d_dbgShell->handleBreak( e, e->d_curScript, e->lineForBreak() );
    e->d_waitForCommand = false;
    if( e->d_dbgCmd == Abort || e->d_dbgCmd == AbortSilently )
        e->notify( Aborted );
    else
        e->notify( Continued );

    return 0;
}

int Engine2::TRACE(lua_State* L)
{
    QFile out("trace.log");
    if( !out.open(QIODevice::Append) )
        qCritical() << "ERR: cannot open log for writing";
    else
    {
        for( int i = 1; i <= lua_gettop(L); i++ )
        {
            if( i != 1 )
                out.write("\t");
            out.write(lua_tostring(L,i));
        }
        out.write("\n");
    }
    return 0;
}

int Engine2::ABORT(lua_State* L)
{
    Engine2* e = Engine2::getInst();
    e->terminate(true);
    if( lua_gettop(L) == 0 )
        lua_pushnil(L);
    lua_error(L);
    return 0;
}

void Engine2::terminate(bool silent)
{
	d_dbgCmd = (silent)?AbortSilently:Abort;
    d_waitForCommand = false;
}

Engine2* Engine2::getInst()
{
	return s_this;
}

void Engine2::setInst(Engine2 * e)
{
	if( s_this != 0 )
		delete s_this;
	s_this = e;
}

void Engine2::addBreak(const QByteArray &s, quint32 l)
{
	d_breaks[s].insert( l );
	notify( BreakPoints, s );
}

void Engine2::removeBreak(const QByteArray & s, quint32 l)
{
	d_breaks[s].remove( l );
    notify( BreakPoints, s );
}

quint32 Engine2::packDeflinePc(quint32 defline, quint16 pc)
{
    static const quint32 maxDefline = ( 1 << DEFLINE_BIT_LEN ) - 1;
    static const quint32 maxPc = ( 1 << PC_BIT_LEN ) - 1;
    Q_ASSERT( defline <= maxDefline && pc <= maxPc );
    return ( defline << PC_BIT_LEN ) | pc;
}

QPair<quint32, quint16> Engine2::unpackDeflinePc(quint32 packed)
{
    const quint32 defline = packed >> PC_BIT_LEN;
    const quint16 pc = packed & ( ( 1 << PC_BIT_LEN ) - 1 );
    return qMakePair(defline,pc);
}

const Engine2::Breaks& Engine2::getBreaks(const QByteArray & s) const
{
	BreaksPerScript::const_iterator i = d_breaks.find( s );
	if( i == d_breaks.end() )
        return s_dummy;
	else
        return (*i);
}

Engine2::StackLevels Engine2::getStackTrace() const
{
    StackLevels ls;
    int level =  0;
    while( true )
    {
#if 0
        lua_Debug dbg;
        int res = lua_getstack(d_ctx, level, &dbg );
        if( res != 1 )
            break;
        res = lua_getinfo(d_ctx,"nlS", &dbg );

        StackLevel l;
        l.d_level = level;
        l.d_line = dbg.currentline;
        l.d_what = dbg.namewhat;
        l.d_name = ( dbg.name ? dbg.name : "" );
        l.d_source = dbg.source;
        l.d_inC = *dbg.what == 'C';
        ls << l;
#endif
        StackLevel l = getStackLevel(level,false);
        if( !l.d_valid )
            break;
        ls << l;
        level++;
    }
    return ls;
}

Engine2::StackLevel Engine2::getStackLevel(quint16 level, bool withValidLines, lua_Debug* ar) const
{
    return getStackLevel( d_ctx, level, withValidLines, d_mode == PcMode, ar );
}

Engine2::StackLevel Engine2::getStackLevel(lua_State *L, quint16 level, bool withValidLines, bool bytecodeMode, lua_Debug* ar )
{
    StackLevel l;
    lua_Debug dbg;
    if( ar == 0 )
    {
        int res = lua_getstack(L, level, &dbg );
        if( res != 1 )
        {
            l.d_valid = false;
            return l;
        }
        ar = &dbg;
    }
    QByteArray query;
    if( bytecodeMode )
        query = "nSp";
    else
        query = withValidLines ? "nlSL" : "nlS";
    if( lua_getinfo(L, query.constData(), ar ) == 0 )
    {
        qCritical() << "Engine2::getStackLevel lua_getinfo error";
        l.d_valid = false;
        return l;
    }

    const char * what = ar->what;
    int curline = ar->currentline;
    if( curline == -1 )
        curline = 0;
    const bool inLua = *(what) == 'L' || *(what) == 'm';
    l.d_level = level;
    if( !inLua )
        l.d_line = 0;
    else if( bytecodeMode )
        l.d_line = curline; // in this case curline is the pc
    else
        l.d_line = curline; // no, we want row and col; before JitComposer::unpackRow2(curline);
    l.d_lineDefined = ar->linedefined;
    l.d_lastLine = ar->lastlinedefined;
    l.d_what = ar->namewhat;
    l.d_name = ( ar->name ? ar->name : "" );
    l.d_source = *ar->source == '@' ? ar->source + 1 : ar->source;
    l.d_inC = *(what) == 'C';

    if( withValidLines )
    {
        // pushes onto the stack a table whose indices are the numbers of the lines that are valid on the function
        const int t = lua_gettop(L);
        if( inLua )
        {
            lua_pushnil(L);
            while( lua_next(L, t) != 0 ) // not ordered!
            {
                lua_pop(L, 1); // remove unused value
                const quint32 line = bytecodeMode ?
                            lua_tointeger(L, -1 ) : JitComposer::unpackRow2(lua_tointeger(L, -1 ));
                l.d_lines.insert(line);
            }
            lua_pop(L, 2); // key and t
        }else
            lua_pop(L,1); // t
    }

    return l;
}

static bool sortLocals( const Lua::Engine2::LocalVar& lhs, const Lua::Engine2::LocalVar& rhs )
{
    return lhs.d_name.toLower() < rhs.d_name.toLower();
}

static inline Engine2::LocalVar::Type luaToValType( int t )
{
    switch( t )
    {
    case LUA_TNIL:
        return Engine2::LocalVar::NIL;
    case LUA_TFUNCTION:
        return Engine2::LocalVar::FUNC;
    case LUA_TTABLE:
        return Engine2::LocalVar::TABLE;
    case LUA_TLIGHTUSERDATA:
    case LUA_TUSERDATA:
        return Engine2::LocalVar::STRUCT;
    case LUA_TBOOLEAN:
        return Engine2::LocalVar::BOOL;
    case LUA_TNUMBER:
        return Engine2::LocalVar::NUMBER;
    case LUA_TSTRING:
        return Engine2::LocalVar::STRING;
    case 10:
        return Engine2::LocalVar::CDATA;

    default:
        return Engine2::LocalVar::UNKNOWN;
    }
}

Engine2::LocalVars Engine2::getLocalVars(bool includeUpvals, quint8 resolveTableToLevel,
                                         int maxArrayIndex , bool includeTemps) const
{
    LocalVars ls;

    lua_Debug ar;
    if( !lua_getstack( d_ctx, d_activeLevel, &ar ) )
        return LocalVars();
    int n = 1;
    while( const char* name = lua_getlocal( d_ctx, &ar, n) )
    {
        const int top = lua_gettop(d_ctx);
        LocalVar v;
        v.d_name = name;
        if( !v.d_name.startsWith('(') && !v.d_name.isEmpty() )
        {
            v.d_type = luaToValType( lua_type( d_ctx, top ) );
            v.d_value = getValue( top, resolveTableToLevel, maxArrayIndex );
            ls << v;
        }else if( includeTemps || v.d_name.isEmpty() )
        {
            v.d_type = luaToValType( lua_type( d_ctx, top ) );
            v.d_value = getValue( top, resolveTableToLevel, maxArrayIndex );
            v.d_name = "[" + QByteArray::number(n-1) + "]";
            ls << v;
        }
        lua_pop( d_ctx, 1 );
        n++;
    }

    if( includeUpvals && lua_getinfo( d_ctx, "f", &ar ) != 0 )
    {
        const int f = lua_gettop(d_ctx);

        int n = 1;
        while( const char* name = lua_getupvalue( d_ctx, f, n) )
        {
            const int top = lua_gettop(d_ctx);
            LocalVar v;
            v.d_name = name;
            v.d_isUv = true;
            if( !v.d_name.startsWith('(') && !v.d_name.isEmpty() )
            {
                v.d_type = luaToValType( lua_type( d_ctx, top ) );
                v.d_value = getValue( top, resolveTableToLevel, maxArrayIndex );
                ls << v;
            }else if( includeTemps || v.d_name.isEmpty() )
            {
                v.d_type = luaToValType( lua_type( d_ctx, top ) );
                v.d_value = getValue( top, resolveTableToLevel, maxArrayIndex );
                v.d_name = "(" + QByteArray::number(n-1) + ")";
                ls << v;
            }
            lua_pop( d_ctx, 1 );
            n++;
        }
        lua_pop( d_ctx, 1 ); // fuction
    }

    std::sort( ls.begin(), ls.end(), sortLocals );

    return ls;
}

void Engine2::removeAllBreaks(const QByteArray &s)
{
	if( s.isNull() )
	{
		if( d_breaks.empty() )
			return;
		d_breaks.clear();
		notify( BreakPoints );
	}else
	{
		if( d_breaks[s].empty() )
			return;
		d_breaks[s].clear();
		notify( BreakPoints, s );
	}
}

void Engine2::print(const char * str)
{
    notify( Print, str );
}

Engine2::ErrorMsg Engine2::decodeRuntimeMessage(const QByteArray& msg)
{
    ErrorMsg res;
    const int rbrack = msg.indexOf(']'); // cannot directly search for ':' because Windows "C:/"
    if( rbrack != -1 )
    {
        QByteArray path = msg.left(rbrack);
        const int firstTick = path.indexOf('"');
        if( firstTick != -1 )
        {
            const int secondTick = path.indexOf('"',firstTick+1);
            path = path.mid(firstTick+1,secondTick-firstTick-1);
            if( path == "string" )
                path.clear();
        }else
            path.clear();
        res.d_source = path;

        const int firstColon = msg.indexOf(':', rbrack);
        if( firstColon != -1 )
        {
            const int secondColon = msg.indexOf(':',firstColon + 1);
            if( secondColon != -1 )
            {
                res.d_line = msg.mid(firstColon+1, secondColon - firstColon - 1 ).toInt(); // lua deliveres negative numbers
                res.d_message = msg.mid(secondColon+1);
            }
        }else
            res.d_message = msg.mid(rbrack+1);
    }else
        res.d_message = msg;
    return res;
}

void Engine2::error(const char * str)
{
	notify( Error, str );
}

QByteArray Engine2::getTypeName(int arg) const
{
    const int t = lua_type( d_ctx, arg );
    switch( t )
    {
    case LUA_TNIL:
        return "";
    case LUA_TFUNCTION:
        if( lua_iscfunction( d_ctx, arg ) )
            return "C function";
        else
            return "Lua function";
    case LUA_TUSERDATA:
        {
            const QByteArray name; //  = ValueBindingBase::getTypeName( d_ctx, arg );
			if( !name.isEmpty() )
				return name;
			break;
		}
    default:
        return lua_typename( d_ctx, t );
    }
    return "<unknown>";
}

static QByteArray _toHex(const void *p)
{
    return "0x" + QByteArray::number((quintptr)p, 16 ); // table, thread, function, userdata
}

QByteArray Engine2::getValueString(int arg, bool showAddress ) const
{
    switch( lua_type( d_ctx, arg ) )
    {
    case LUA_TNIL:
        return "nil";
    case LUA_TNUMBER:
        return QByteArray::number( lua_tonumber( d_ctx, arg ) );
    case LUA_TBOOLEAN:
        return ( lua_toboolean( d_ctx, arg ) ) ? "true" : "false";
    case LUA_TSTRING:
        return QByteArray("\"") + lua_tostring( d_ctx, arg ) + "\"";
    case LUA_TTABLE:
    case LUA_TTHREAD:
    case LUA_TFUNCTION:
        if( showAddress )
            return _toHex( lua_topointer( d_ctx, arg) );
        break;
    case LUA_TLIGHTUSERDATA:
        if( showAddress )
            return _toHex( lua_touserdata( d_ctx, arg ) );
        break;
    case LUA_TUSERDATA:
        if( false ) // TODO
        {
            const QByteArray name; //  = ValueBindingBase::getBindingName( d_ctx, arg );
			if( !name.isEmpty() )
				return __tostring( arg ); // Alle haben eine gültige tostring-Konvertierung
			else
				return "<unknown>";
        }
        break;
    }
    return QByteArray();
}

static inline const void* metapointer( lua_State *L, int idx )
{
    if( lua_getmetatable( L, idx ) )
    {
        const void* res = lua_topointer(L,-1);
        lua_pop(L,1);
        return res;
    }else
        return 0;
}

QVariant Engine2::getValue(int arg, quint8 resolveTableToLevel, int maxArrayIndex ) const
{
    const int t = lua_type( d_ctx, arg );
    switch( t )
    {
    case LUA_TNUMBER:
        return lua_tonumber( d_ctx, arg );
    case LUA_TBOOLEAN:
        return lua_toboolean( d_ctx, arg ) ? true : false;
    case LUA_TSTRING:
        return lua_tostring( d_ctx, arg );
    case LUA_TTABLE:
        if( resolveTableToLevel > 0 )
        {
            QVariantMap vals;
            Q_ASSERT( arg >= 0 );
            vals.insert(QString(),QVariant::fromValue(
                            VarAddress(LocalVar::TABLE, lua_topointer(d_ctx,arg), metapointer(d_ctx,arg) )));
            const int w = ::log10(maxArrayIndex)+1;
            lua_pushnil(d_ctx);  /* first key */
            while( lua_next(d_ctx, arg) != 0 )
            {
                /* uses 'key' (at index -2) and 'value' (at index -1) */
                const int top = lua_gettop(d_ctx);
                // "While traversing a table, do not call lua_tolstring directly on a key, unless you know that the
                // key is actually a string"; tostring calls tolstring!
                const QVariant key = getValue( top - 1, 0, 0 );
                const bool numKey = JitBytecode::isNumber(key);
                if( !numKey || key.toUInt() <= maxArrayIndex )
                {
                    // if key is a string or - if a number - is less than max
                    const QVariant value = getValue( top, resolveTableToLevel - 1, maxArrayIndex ) ;
                    QString keyStr;
                    if( numKey )
                        keyStr = QString("%1").arg(key.toUInt(),w,10,QChar(' '));
                    else
                        keyStr = key.toString();
                    vals.insert( keyStr, value );
                }
                /* removes 'value'; keeps 'key' for next iteration */
                lua_pop(d_ctx, 1);
            }
            return vals;
        }else
            return QVariant::fromValue(VarAddress(LocalVar::TABLE, lua_topointer(d_ctx,arg), metapointer(d_ctx,arg) ) );
        break;
    case LUA_TUSERDATA:
    default:
        if( luaL_callmeta(d_ctx,arg,"__tostring") )
        {
            QByteArray str = lua_tostring(d_ctx, -1 );
            lua_pop(d_ctx,1);
            return str;
        }// else
        return QVariant::fromValue(VarAddress(luaToValType(t), lua_topointer(d_ctx,arg), metapointer(d_ctx,arg) ));
    }
    return QVariant();
}

int Engine2::pushLocalOrGlobal(const QByteArray &name)
{
    if( d_waitForCommand )
    {
        // Wir können auf den Stack zugreifen
        lua_Debug ar;
        if( lua_getstack( d_ctx, d_activeLevel, &ar ) )
        {
            int n = 1;
            while( const char* localName = lua_getlocal( d_ctx, &ar, n) )
            {
                Q_ASSERT( localName != 0 );
                if( localName == name )
                {
                    return 1;
                }else
                    lua_pop( d_ctx, 1 );
                n++;
            }
        }
    }
    lua_pushstring( d_ctx, name );
    lua_rawget( d_ctx, (d_waitForCommand)?LUA_ENVIRONINDEX:LUA_GLOBALSINDEX );
    return 1;
}

QByteArray Engine2::__tostring(int arg) const
{
    if( luaL_callmeta( d_ctx, arg, "__tostring") )
    {
        QByteArray value = lua_tostring( d_ctx, -1 );
        lua_pop( d_ctx, 1 );
        return value;
    }else
        return "<bytes>";
}

void Engine2::pop(int count)
{
	lua_pop( d_ctx, count );
}

void Engine2::notify(MessageType messageType, const QByteArray &val1, int val2)
{
    if( d_printToStdout )
    {
        switch( messageType )
        {
        case Print:
            std::cout << val1.constData() << std::flush;
            break;
        case Error:
            std::cerr << val1.constData() << std::flush;
            break;
        }
    }
    emit onNotify( messageType, val1, val2 );
}

quint32 Engine2::lineForBreak() const
{
    if( d_mode == LineMode )
    {
        return JitComposer::unpackRow2(d_curRowCol);
    }else
        return d_curRowCol;
}

int Engine2::lineForNotify() const
{
    switch( d_mode )
    {
    case LineMode:
    case RowColMode:
        return JitComposer::unpackRow2(d_curRowCol);
    case PcMode:
        return unpackDeflinePc(d_curRowCol).second;
    }
}
