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

#include <lua.hpp>

#include "Engine2.h"
#include <QCoreApplication>
#include <math.h>
#include <QtDebug>

using namespace Lua;

static Engine2* s_this = 0;

static const char* s_path = "path";
static const char* s_cpath = "cpath";
static Engine2::Breaks s_dummy;

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
    if( buf.endsWith('\n') )
        buf.chop(1);
    else if( buf.endsWith("\r\n") )
        buf.chop(2);
    Engine2* e = Engine2::getInst();
    Q_ASSERT( e != 0 );
    try
    {
        e->notify( err ? Engine2::Error : Engine2::Print, buf );
    }catch( std::exception& e )
    {
        luaL_error( L, "Error calling host: %s", e.what() );
    }catch( ... )
    {
        luaL_error( L, "Unknown exception while calling host" );
    }
    return 0;
}

Engine2::Engine2(QObject *p):QObject(p),
    d_ctx( 0 ), d_debugging( false ), d_running(false), d_waitForCommand(false),
	d_dbgCmd(RunToBreakPoint), d_defaultDbgCmd(RunToBreakPoint), d_activeLevel(0), d_dbgShell(0)
{
    lua_State* ctx = lua_open();
	if( ctx == 0 )
		throw Exception( "Not enough memory to create scripting context" );

    LUAJIT_VERSION_SYM();

	d_ctx = ctx;

	addLibrary( BASE );	// Das muss hier stehen, sonst wird ev. print wieder überschrieben

    lua_pushcfunction( ctx, _print );
    lua_setglobal( ctx, "print" );
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

bool Engine2::saveBinary(const QByteArray& source, const QByteArray& path)
{
    d_lastError.clear();
    FILE* f = ::fopen( path,"wb" );
    if( f == 0 )
    {
        d_lastError = "Unable to open file for writing";
        return false;
    }
    if( !pushFunction( source ) )
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
	const int preTop = lua_gettop( d_ctx );
	if( d_waitForCommand )
    {
		d_lastError = "Cannot run another Lua function while script is waiting in debugger!";
		lua_pop( d_ctx, nargs + 1 ); // funktion + args
		if( nresults != LUA_MULTRET )
			Q_ASSERT( lua_gettop( d_ctx ) == ( preTop - nargs - 1 ) );
        return false;
    }
	d_lastError.clear();
    d_running = true;
	d_dbgCmd = d_defaultDbgCmd;
	notifyStart();
    // TODO: ev. Stacktrace mittels errfunc
	// Lua: All arguments and the function value are popped from the stack when the function is called.
    const int err = lua_pcall( d_ctx, nargs, nresults, 0 );
    d_running = false;
    switch( err )
    {
    case LUA_ERRRUN:
        d_lastError = lua_tostring( d_ctx, -1 );
        lua_pop( d_ctx, 1 );  /* remove error message */
		nresults = 0;
        break;
    case LUA_ERRMEM:
        d_lastError = "Lua memory exception";
        break;
    case LUA_ERRERR:
        // should not happen
        d_lastError = "Lua unknown error";
        break;
    }
	notifyEnd();
	const int postTop = lua_gettop( d_ctx );
	if( nresults != LUA_MULTRET )
		Q_ASSERT( postTop == ( preTop - nargs - 1 + nresults ) );
	return (err == 0);
}

void Engine2::collect()
{
#if LUA_VERSION_NUM >= 501
	if( d_ctx )
		lua_gc( d_ctx, LUA_GCCOLLECT, 0 );
#else
	if( d_ctx )
		lua_setgcthreshold( d_ctx,0 ); 
#endif
}

void Engine2::setActiveLevel(int level, const QByteArray &script, int line)
{
    if( d_activeLevel == level )
        return;
    d_activeLevel = level;
	notify( ActiveLevel, script, line );
}

void Engine2::debugHook(lua_State *L, lua_Debug *ar)
{
    if( ar->event != LUA_HOOKLINE )
        return;

	Engine2* e = Engine2::getInst();
	Q_ASSERT( e != 0 );
    lua_getinfo( L, "S", ar );
    const QByteArray source = ar->source;
    const bool lineChanged = ( e->d_curLine != ar->currentline || e->d_curScript != source );
    if( lineChanged )
    {
        e->d_breakHit = false;
        e->d_curScript = source;
        e->d_curLine = ar->currentline;
        e->d_activeLevel = 0;
		if( e->d_curScript.startsWith('#') )
			e->d_curBinary = getBinaryFromFunc( L );
		lua_pop(L, 1); // Function

        if( e->isStepping() )
        {
            e->d_waitForCommand = true;
			e->notify( LineHit, e->d_curScript, e->d_curLine );
			if( e->d_dbgShell )
				e->d_dbgShell->handleBreak( e, e->d_curScript, e->d_curLine );
        }else if( e->d_breaks.value( source ).contains( e->d_curLine ) )
        {
            e->d_waitForCommand = true;
            e->d_breakHit = true;
			e->notify( BreakHit, e->d_curScript, e->d_curLine );
			if( e->d_dbgShell )
				e->d_dbgShell->handleBreak( e, e->d_curScript, e->d_curLine );
		}
		if( e->d_dbgCmd == Abort || e->d_dbgCmd == AbortSilently )
        {
            luaL_error( L, "Execution terminated by user" );
        }
		e->notify( Continued );
    }else
		lua_pop(L, 1); // function
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
#if LUA_VERSION_NUM >= 501
	if( res != 0 )
#else
	if( res != 1 )
#endif
		ba.clear();
	return ba;
}

void Engine2::notifyStart()
{
	notify( Started );
}

void Engine2::notifyEnd()
{
	notify( (d_dbgCmd == Abort || d_dbgCmd == AbortSilently)? Aborted : Finished );
}

#ifdef __unused
static const char* toStr( int i )
{
	switch( i )
	{
	case LUA_TNIL:
		return "nil";
	case LUA_TNUMBER:
		return "number";
	case LUA_TBOOLEAN:
		return "bool";
	case LUA_TSTRING:
		return "string";
	case LUA_TTABLE:
		return "table";
	case LUA_TFUNCTION:
		return "fun";
	case LUA_TUSERDATA:
		return "ud";
	case LUA_TTHREAD:
		return "thread";
	case LUA_TLIGHTUSERDATA:
		return "lud";

	}
	return "?";
}
#endif

void Engine2::setDebug(bool on)
{
    if( d_debugging == on )
        return;
    if( on )
        lua_sethook( d_ctx, debugHook, LUA_MASKLINE, 1);
    else
        lua_sethook( d_ctx, debugHook, 0, 0);
    d_debugging = on;

}

void Engine2::runToNextLine()
{
    d_dbgCmd = RunToNextLine;
    d_waitForCommand = false;
}

void Engine2::runToBreakPoint()
{
    d_dbgCmd = RunToBreakPoint;
    d_waitForCommand = false;
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

const Engine2::Breaks& Engine2::getBreaks(const QByteArray & s) const
{
	BreaksPerScript::const_iterator i = d_breaks.find( s );
	if( i == d_breaks.end() )
        return s_dummy;
	else
		return (*i);
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

void Engine2::error(const char * str)
{
#ifdef _DEBUG
    //qDebug( "Lua Error: %s", str );
#endif
	notify( Error, str );
}

void Engine2::setPluginPath( const char* path, bool cpath )
{
    if( cpath )
        lua_pushstring( d_ctx, s_cpath ); // Hier muss der String als Name verwendet werden, nicht die Adresse
	else
        lua_pushstring( d_ctx, s_path ); // Hier muss der String als Name verwendet werden, nicht die Adresse
    lua_pushstring( d_ctx, path );
	lua_rawset( d_ctx, LUA_REGISTRYINDEX );
}

QByteArray Engine2::getPluginPath(bool cpath) const
{
    if( cpath )
        lua_pushstring( d_ctx, s_cpath ); // Hier muss der String als Name verwendet werden, nicht die Adresse
    else
        lua_pushstring( d_ctx, s_path ); // Hier muss der String als Name verwendet werden, nicht die Adresse
	lua_rawget( d_ctx, LUA_REGISTRYINDEX );
	const char * path = lua_tostring(d_ctx, -1);
	if( path == 0 )
		return "";
	else
		return path;
}

QByteArray Engine2::getTypeName(int arg) const
{
    const int t = lua_type( d_ctx, arg );
    switch( t )
    {
    case LUA_TNIL:
        return "-";
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
    return "0x" + QByteArray::number((quint32)p, 16 ); // table, thread, function, userdata
}

QByteArray Engine2::getValueString(int arg) const
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
        return _toHex( lua_topointer( d_ctx, arg) );
    case LUA_TLIGHTUSERDATA:
        return _toHex( lua_touserdata( d_ctx, arg ) );
    case LUA_TUSERDATA:
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

void Engine2::dumpStackFrom(int arg, const char* title )
{
	if( arg <= 0 )
		arg = lua_gettop( d_ctx ) + arg;
	qDebug() << "******** Engine2::dumpStackFrom: " << arg << title;
	for( int n = arg; n <= lua_gettop( d_ctx ); n++ )
		qDebug() << "Level:" << n << getTypeName(n) << getValueString(n);
}

void Engine2::notify(MessageType messageType, const QByteArray &val1, int val2)
{
	emit onNotify( messageType, val1, val2 );
}
