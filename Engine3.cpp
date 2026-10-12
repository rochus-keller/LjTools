/*
* Copyright 2026 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the LjTools library.
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

#include "Engine3.h"
#include <lua.hpp>
#include <QtDebug>
#include <iostream>
using namespace Lua;

// No global instance; the engine is found from any lua_State via the registry, seee getEngine
// static Engine2* s_this = 0;

static char s_key = 0;
static const int s_aliveCount = 10000;
static const Engine3::Breaks s_noBreaks;
static const char* s_terminated = "Execution terminated by user";

static inline QByteArray sourceName( const char* s )
{
    if( s == 0 )
        return QByteArray();
    return *s == '@' ? QByteArray(s + 1) : QByteArray(s);
}

static int threadDepth( lua_State* L )
{
    lua_Debug ar;
    int n = 0;
    while( lua_getstack(L, n, &ar) )
        n++;
    return n;
}

static inline QByteArray toHex( const void* p )
{
    return "0x" + QByteArray::number(quintptr(p), 16);
}

static inline Engine3::LocalVar::Type toValType( int t )
{
    switch( t )
    {
    case LUA_TNIL:
        return Engine3::LocalVar::NIL;
    case LUA_TBOOLEAN:
        return Engine3::LocalVar::BOOL;
    case LUA_TNUMBER:
        return Engine3::LocalVar::NUMBER;
    case LUA_TSTRING:
        return Engine3::LocalVar::STRING;
    case LUA_TFUNCTION:
        return Engine3::LocalVar::FUNC;
    case LUA_TTABLE:
        return Engine3::LocalVar::TABLE;
    case 10:
        return Engine3::LocalVar::CDATA;
    default:
        return Engine3::LocalVar::UNKNOWN;
    }
}

static bool sortLocals( const Engine3::LocalVar& lhs, const Engine3::LocalVar& rhs )
{
    return lhs.d_name.toLower() < rhs.d_name.toLower();
}

Engine3::Engine3(QObject* p):QObject(p),d_ctx(0),d_breakL(0),d_curL(0),d_stepL(0),d_shell(0),
    d_curLine(0),d_stepDepth(0),d_activeLevel(0),d_abort(0),d_dbgCmd(RunToBreakPoint),
    d_debugging(false),d_aliveSignal(false),d_interruptible(false),d_jit(true),d_waiting(false),
    d_breakHit(false),d_printToStdout(false)
{
    d_ctx = luaL_newstate();
    if( d_ctx == 0 )
    {
        qCritical() << "Engine3: not enough memory to create Lua context";
        return;
    }
    LUAJIT_VERSION_SYM();
    lua_pushlightuserdata(d_ctx, &s_key);
    lua_pushlightuserdata(d_ctx, this);
    lua_rawset(d_ctx, LUA_REGISTRYINDEX);
    addLibrary(BASE);
    lua_pushcfunction(d_ctx, print);
    lua_setglobal(d_ctx, "print");
}

Engine3::~Engine3()
{
    if( d_ctx )
        lua_close(d_ctx);
}

Engine3* Engine3::getEngine(lua_State* L)
{
    lua_pushlightuserdata(L, &s_key);
    lua_rawget(L, LUA_REGISTRYINDEX);
    Engine3* e = (Engine3*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return e;
}

void Engine3::addLibrary(Lib what)
{
    lua_CFunction f = 0;
    const char* name = "";
    switch( what )
    {
    case BASE:
        f = luaopen_base;
        break;
    case PACKAGE:
        f = luaopen_package;
        name = LUA_LOADLIBNAME;
        break;
    case TABLE:
        f = luaopen_table;
        name = LUA_TABLIBNAME;
        break;
    case STRING:
        f = luaopen_string;
        name = LUA_STRLIBNAME;
        break;
    case MATH:
        f = luaopen_math;
        name = LUA_MATHLIBNAME;
        break;
    case OS:
        f = luaopen_os;
        name = LUA_OSLIBNAME;
        break;
    case IO:
        f = luaopen_io;
        name = LUA_IOLIBNAME;
        break;
    case BIT:
        f = luaopen_bit;
        name = LUA_BITLIBNAME;
        break;
    case JIT:
        f = luaopen_jit;
        name = LUA_JITLIBNAME;
        break;
    case FFI:
        f = luaopen_ffi;
        name = LUA_FFILIBNAME;
        break;
    case DBG:
        f = luaopen_debug;
        name = LUA_DBLIBNAME;
        break;
    }
    if( f == 0 )
        return;
    lua_pushcfunction(d_ctx, f);
    lua_pushstring(d_ctx, name);
    lua_call(d_ctx, 1, 0);
    if( what == IO )
    {
        // io.write and io.stderr:write go through notify like print
        lua_getglobal(d_ctx, LUA_IOLIBNAME);
        lua_pushcfunction(d_ctx, writeOut);
        lua_setfield(d_ctx, -2, "write");
        lua_createtable(d_ctx, 0, 1);
        lua_pushcfunction(d_ctx, writeErr);
        lua_setfield(d_ctx, -2, "write");
        lua_setfield(d_ctx, -2, "stderr");
        lua_pop(d_ctx, 1);
    }
}

void Engine3::addStdLibs()
{
    addLibrary(TABLE);
    addLibrary(STRING);
    addLibrary(MATH);
}

bool Engine3::addPreloadLib(const QByteArray& code, const QByteArray& name)
{
    if( d_waiting )
    {
        d_lastError = "cannot load a module while waiting in the debugger";
        return false;
    }
    if( luaL_loadbuffer(d_ctx, code.constData(), code.size(), name.constData()) != 0 )
    {
        d_lastError = lua_tostring(d_ctx, -1);
        lua_pop(d_ctx, 1);
        return false;
    }
    lua_getglobal(d_ctx, "package");
    lua_getfield(d_ctx, -1, "preload");
    lua_pushvalue(d_ctx, -3);
    lua_setfield(d_ctx, -2, name.constData());
    lua_pop(d_ctx, 1);
    lua_getfield(d_ctx, -1, "loaded");
    lua_pushnil(d_ctx);
    lua_setfield(d_ctx, -2, name.constData());
    lua_pop(d_ctx, 3); // loaded, package, chunk
    return true;
}

bool Engine3::executeCmd(const QByteArray& source, const QByteArray& name)
{
    d_lastError.clear();
    const QByteArray n = name.isEmpty() ? source : name;
    if( luaL_loadbuffer(d_ctx, source.constData(), source.size(), n.constData()) != 0 )
    {
        d_lastError = lua_tostring(d_ctx, -1);
        lua_pop(d_ctx, 1);
        notify(Error, d_lastError);
        return false;
    }
    if( !pcall(0, 0) )
    {
        if( !d_lastError.isEmpty() )
            notify(Error, d_lastError);
        return false;
    }
    return true;
}

bool Engine3::pcall(int nargs, int nresults)
{
    const int func = lua_gettop(d_ctx) - nargs;
    if( d_waiting || isAborted() )
    {
        if( d_abort.loadAcquire() == AbortSilently )
            d_lastError.clear();
        else
            d_lastError = d_waiting ? "cannot run while waiting in the debugger" : s_terminated;
        lua_settop(d_ctx, func - 1);
        return false;
    }
    lua_pushcfunction(d_ctx, errHandler);
    lua_insert(d_ctx, func);
    if( lua_pcall(d_ctx, nargs, nresults, func) != 0 )
    {
        if( d_abort.loadAcquire() == AbortSilently )
            d_lastError.clear();
        else if( isAborted() )
            d_lastError = s_terminated;
        else if( lua_isstring(d_ctx, -1) )
            d_lastError = lua_tostring(d_ctx, -1);
        else
            d_lastError = "unknown Lua error";
        lua_settop(d_ctx, func - 1);
        return false;
    }
    lua_remove(d_ctx, func);
    return true;
}

int Engine3::errHandler(lua_State* L)
{
    Engine3* e = getEngine(L);
    if( e == 0 || e->isAborted() )
        return 1;
    const char* msg = lua_tostring(L, 1);
    if( e->d_debugging && e->d_shell )
    {
        lua_Debug ar;
        QByteArray source;
        quint32 line = 0;
        int level = 1;
        while( lua_getstack(L, level, &ar) )
        {
            lua_getinfo(L, "Sl", &ar);
            if( *ar.what != 'C' )
            {
                source = sourceName(ar.source);
                line = ar.currentline < 0 ? 0 : ar.currentline;
                break;
            }
            level++;
        }
        e->d_breakHit = false;
        e->waitForCommand(L, source, line, ErrorHit);
        if( e->isAborted() )
            return 1;
    }
    luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
    return 1;
}

void Engine3::terminate(bool silent)
{
    d_abort.storeRelease(silent ? AbortSilently : Abort);
    d_waiting = false;
}

bool Engine3::isAborted() const
{
    return d_abort.loadAcquire() != 0;
}

void Engine3::setJit(bool on)
{
    d_jit = on;
    if( on && !d_debugging )
        luaJIT_setmode(d_ctx, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
    else
    {
        luaJIT_setmode(d_ctx, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_FLUSH);
        luaJIT_setmode(d_ctx, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    }
}

void Engine3::setInterruptible(bool on)
{
    d_interruptible = on;
    updateHook();
}

void Engine3::setDebug(bool on)
{
    d_debugging = on;
    setJit(d_jit);
    updateHook();
}

void Engine3::setAliveSignal(bool on)
{
    d_aliveSignal = on;
    updateHook();
}

void Engine3::updateHook()
{
    if( d_debugging )
        lua_sethook(d_ctx, debugHook, LUA_MASKLINE | LUA_MASKCOUNT, s_aliveCount);
    else if( d_interruptible || d_aliveSignal )
        lua_sethook(d_ctx, countHook, LUA_MASKCOUNT, s_aliveCount);
    else
        lua_sethook(d_ctx, 0, 0, 0);
}

void Engine3::countHook(lua_State* L, lua_Debug*)
{
    Engine3* e = getEngine(L);
    if( e == 0 )
        return;
    if( e->d_aliveSignal && e->d_shell )
        e->d_shell->handleAliveSignal(e);
    if( e->isAborted() )
    {
        lua_pushstring(L, s_terminated);
        lua_error(L);
    }
}

void Engine3::debugHook(lua_State* L, lua_Debug* ar)
{
    Engine3* e = getEngine(L);
    if( e == 0 )
        return;
    if( ar->event == LUA_HOOKCOUNT )
    {
        if( e->d_shell )
            e->d_shell->handleAliveSignal(e);
    }else if( ar->event == LUA_HOOKLINE )
    {
        lua_getinfo(L, "S", ar);
        const QByteArray source = sourceName(ar->source);
        const quint32 line = ar->currentline < 0 ? 0 : ar->currentline;
        const bool changed = L != e->d_curL || line != e->d_curLine || source != e->d_curScript;
        e->d_curL = L;
        e->d_curLine = line;
        e->d_curScript = source;
        if( changed )
        {
            BreaksPerScript::const_iterator i = e->d_breaks.find(source);
            if( i != e->d_breaks.end() && i.value().contains(line) )
            {
                e->d_breakHit = true;
                e->waitForCommand(L, source, line, BreakHit);
            }else if( e->isStepping() && !e->d_hidden.contains(source) && e->stepTarget(L) )
            {
                e->d_breakHit = true;
                e->waitForCommand(L, source, line, LineHit);
            }
        }
    }
    if( e->isAborted() )
    {
        lua_pushstring(L, s_terminated);
        lua_error(L);
    }
}

bool Engine3::stepTarget(lua_State* L) const
{
    switch( d_dbgCmd )
    {
    case StepNext:
        return true;
    case StepOver:
        return L == d_stepL && threadDepth(L) <= d_stepDepth;
    case StepOut:
        return L == d_stepL && threadDepth(L) < d_stepDepth;
    }
    return false;
}

void Engine3::waitForCommand(lua_State* L, const QByteArray& source, quint32 line, MessageType kind)
{
    d_breakL = L;
    d_activeLevel = 0;
    d_dbgCmd = RunToBreakPoint;
    d_waiting = true;
    notify(kind, source, line);
    if( d_shell )
        d_shell->handleBreak(this, source, line);
    d_waiting = false;
    d_breakL = 0;
    d_breakHit = false;
    if( !isAborted() )
        notify(Continued);
}

void Engine3::runToNextLine(DebugCommand where)
{
    d_stepL = d_breakL;
    d_stepDepth = d_breakL ? threadDepth(d_breakL) : 0;
    if( d_stepL == 0 )
        where = StepNext;
    d_dbgCmd = where;
    d_waiting = false;
}

void Engine3::runToBreakPoint()
{
    d_dbgCmd = RunToBreakPoint;
    d_waiting = false;
}

void Engine3::hideSource(const QByteArray& source)
{
    d_hidden.insert(source);
}

void Engine3::addBreak(const QByteArray& source, quint32 line)
{
    d_breaks[source].insert(line);
}

void Engine3::removeBreak(const QByteArray& source, quint32 line)
{
    d_breaks[source].remove(line);
}

void Engine3::removeAllBreaks(const QByteArray& source)
{
    if( source.isEmpty() )
        d_breaks.clear();
    else
        d_breaks.remove(source);
}

const Engine3::Breaks& Engine3::getBreaks(const QByteArray& source) const
{
    BreaksPerScript::const_iterator i = d_breaks.find(source);
    if( i == d_breaks.end() )
        return s_noBreaks;
    return i.value();
}

Engine3::StackLevels Engine3::getStackTrace() const
{
    StackLevels ls;
    lua_State* L = d_breakL ? d_breakL : d_ctx;
    lua_Debug ar;
    int level = 0;
    while( lua_getstack(L, level, &ar) )
    {
        lua_getinfo(L, "nSl", &ar);
        StackLevel l;
        l.d_level = level;
        l.d_inC = *ar.what == 'C';
        l.d_line = ar.currentline < 0 ? 0 : ar.currentline;
        l.d_lineDefined = ar.linedefined < 0 ? 0 : ar.linedefined;
        l.d_name = ar.name ? ar.name : "";
        l.d_source = sourceName(ar.source);
        ls << l;
        level++;
    }
    return ls;
}

Engine3::LocalVars Engine3::getLocalVars(bool includeUpvals, quint8 resolveTableToLevel, int maxArrayIndex) const
{
    LocalVars ls;
    lua_State* L = d_breakL;
    lua_Debug ar;
    if( L == 0 || !lua_getstack(L, d_activeLevel, &ar) )
        return ls;
    int n = 1;
    while( const char* name = lua_getlocal(L, &ar, n) )
    {
        LocalVar v;
        v.d_name = name;
        if( !v.d_name.isEmpty() && !v.d_name.startsWith('(') )
        {
            v.d_type = toValType(lua_type(L, -1));
            v.d_value = getValue(L, lua_gettop(L), resolveTableToLevel, maxArrayIndex);
            ls << v;
        }
        lua_pop(L, 1);
        n++;
    }
    if( includeUpvals && lua_getinfo(L, "f", &ar) != 0 )
    {
        const int f = lua_gettop(L);
        n = 1;
        while( const char* name = lua_getupvalue(L, f, n) )
        {
            LocalVar v;
            v.d_name = name;
            v.d_isUv = true;
            if( !v.d_name.isEmpty() && !v.d_name.startsWith('(') )
            {
                v.d_type = toValType(lua_type(L, -1));
                v.d_value = getValue(L, lua_gettop(L), resolveTableToLevel, maxArrayIndex);
                ls << v;
            }
            lua_pop(L, 1);
            n++;
        }
        lua_pop(L, 1); // function
    }
    std::sort(ls.begin(), ls.end(), sortLocals);
    return ls;
}

QVariant Engine3::getValue(lua_State* L, int arg, quint8 resolveTableToLevel, int maxArrayIndex) const
{
    switch( lua_type(L, arg) )
    {
    case LUA_TNIL:
        return QVariant();
    case LUA_TNUMBER:
        return lua_tonumber(L, arg);
    case LUA_TBOOLEAN:
        return lua_toboolean(L, arg) ? true : false;
    case LUA_TSTRING:
        return QByteArray(lua_tostring(L, arg));
    case LUA_TTABLE:
        if( resolveTableToLevel > 0 )
        {
            QVariantMap vals;
            lua_pushnil(L);
            while( lua_next(L, arg) != 0 )
            {
                const int top = lua_gettop(L);
                const QVariant key = getValue(L, top - 1, 0, 0);
                const bool numKey = lua_type(L, top - 1) == LUA_TNUMBER;
                if( !numKey || key.toInt() <= maxArrayIndex )
                    vals.insert(key.toString(), getValue(L, top, resolveTableToLevel - 1, maxArrayIndex));
                lua_pop(L, 1);
            }
            return vals;
        }
        return toHex(lua_topointer(L, arg));
    default:
        if( luaL_callmeta(L, arg, "__tostring") )
        {
            const QByteArray str = lua_tostring(L, -1);
            lua_pop(L, 1);
            return str;
        }
        return toHex(lua_topointer(L, arg));
    }
}

QByteArray Engine3::getValueString(int arg, bool showAddress) const
{
    lua_State* L = d_breakL ? d_breakL : d_ctx;
    switch( lua_type(L, arg) )
    {
    case LUA_TNIL:
        return "nil";
    case LUA_TNUMBER:
        return QByteArray::number(lua_tonumber(L, arg));
    case LUA_TBOOLEAN:
        return lua_toboolean(L, arg) ? "true" : "false";
    case LUA_TSTRING:
        return QByteArray("\"") + lua_tostring(L, arg) + "\"";
    case LUA_TTABLE:
    case LUA_TTHREAD:
    case LUA_TFUNCTION:
        if( showAddress )
            return toHex(lua_topointer(L, arg));
        break;
    case LUA_TLIGHTUSERDATA:
        if( showAddress )
            return toHex(lua_touserdata(L, arg));
        break;
    }
    return QByteArray();
}

Engine3::ErrorMsg Engine3::decodeRuntimeMessage(const QByteArray& msg)
{
    ErrorMsg res;
    const int rbrack = msg.indexOf(']'); // not ':' because of Windows "C:/"
    if( rbrack != -1 )
    {
        QByteArray path = msg.left(rbrack);
        const int firstTick = path.indexOf('"');
        if( firstTick != -1 )
        {
            const int secondTick = path.indexOf('"', firstTick + 1);
            path = path.mid(firstTick + 1, secondTick - firstTick - 1);
            if( path == "string" )
                path.clear();
        }else
            path.clear();
        res.d_source = path;
        const int firstColon = msg.indexOf(':', rbrack);
        if( firstColon != -1 )
        {
            const int secondColon = msg.indexOf(':', firstColon + 1);
            if( secondColon != -1 )
            {
                res.d_line = msg.mid(firstColon + 1, secondColon - firstColon - 1).toInt();
                res.d_message = msg.mid(secondColon + 1);
            }
        }else
            res.d_message = msg.mid(rbrack + 1);
    }else
        res.d_message = msg;
    return res;
}

void Engine3::notify(MessageType messageType, const QByteArray& val1, int val2)
{
    if( d_printToStdout )
    {
        if( messageType == Print )
            std::cout << val1.constData() << std::flush;
        else if( messageType == Error )
            std::cerr << val1.constData() << std::flush;
    }
    emit onNotify(messageType, val1, val2);
}

static QByteArray concatArgs( lua_State* L, int first, const char* sep )
{
    QByteArray res;
    const int n = lua_gettop(L);
    lua_getglobal(L, "tostring");
    for( int i = first; i <= n; i++ )
    {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        const char* s = lua_tostring(L, -1);
        if( s == 0 )
            luaL_error(L, "'tostring' must return a string");
        if( i > first )
            res += sep;
        res += s;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return res;
}

int Engine3::print(lua_State* L)
{
    Engine3* e = getEngine(L);
    QByteArray str = concatArgs(L, 1, "\t");
    if( !str.endsWith('\n') )
        str += '\n';
    if( e )
        e->notify(Print, str);
    return 0;
}

int Engine3::writeOut(lua_State* L)
{
    Engine3* e = getEngine(L);
    const QByteArray str = concatArgs(L, 1, "");
    if( e )
        e->notify(Print, str);
    return 0;
}

int Engine3::writeErr(lua_State* L)
{
    Engine3* e = getEngine(L);
    const QByteArray str = concatArgs(L, 2, ""); // called as io.stderr:write(...)
    if( e )
        e->notify(Error, str);
    return 0;
}
