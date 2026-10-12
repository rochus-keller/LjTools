#ifndef LUA_ENGINE3_H
#define LUA_ENGINE3_H

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

#include <QObject>
#include <QSet>
#include <QMap>
#include <QVariant>
#include <QAtomicInt>

typedef struct lua_State lua_State;
typedef struct lua_Debug lua_Debug;

namespace Lua
{
    class Engine3;

    class DbgShell3
    {
    public:
        virtual ~DbgShell3() {}
        virtual void handleBreak( Engine3*, const QByteArray& source, quint32 line ) = 0;
        virtual void handleAliveSignal( Engine3* ) {}
    };

    // Subset of Engine2 used by coroutine-aware languages + debugger.
    class Engine3 : public QObject
    {
        Q_OBJECT
    public:
        enum Lib { BASE, PACKAGE, TABLE, STRING, MATH, OS, IO, BIT, JIT, FFI, DBG };
        enum DebugCommand { StepNext, StepOver, StepOut, RunToBreakPoint, Abort, AbortSilently };
        enum MessageType { Print, Error, Started, Continued, LineHit, BreakHit, ErrorHit, Finished, Aborted };

        typedef QSet<quint32> Breaks; // plain line numbers
        typedef QMap<QByteArray,Breaks> BreaksPerScript; // source -> lines

        struct StackLevel
        {
            QByteArray d_name;
            QByteArray d_source;
            quint32 d_line;
            quint32 d_lineDefined;
            quint16 d_level;
            bool d_inC;
            StackLevel():d_line(0),d_lineDefined(0),d_level(0),d_inC(false){}
        };
        typedef QList<StackLevel> StackLevels;

        struct LocalVar
        {
            enum Type { NIL, BOOL, NUMBER, STRING, FUNC, TABLE, CDATA, UNKNOWN };
            QByteArray d_name;
            QVariant d_value;
            quint8 d_type;
            bool d_isUv;
            LocalVar():d_type(NIL),d_isUv(false){}
        };
        typedef QList<LocalVar> LocalVars;

        struct ErrorMsg
        {
            QByteArray d_source;
            QByteArray d_message;
            quint32 d_line;
            ErrorMsg():d_line(0){}
            bool isEmpty() const { return d_source.isEmpty() && d_message.isEmpty() && d_line == 0; }
        };

        explicit Engine3(QObject* parent = 0);
        ~Engine3();

        static Engine3* getEngine( lua_State* );
        lua_State* getCtx() const { return d_ctx; }

        void addLibrary( Lib );
        void addStdLibs();
        bool addPreloadLib( const QByteArray& code, const QByteArray& name );
        bool executeCmd( const QByteArray& source, const QByteArray& name = QByteArray() );
        bool pcall( int nargs = 0, int nresults = 0 ); // pre: func, args; post: results on success
        const QByteArray& getLastError() const { return d_lastError; }
        static ErrorMsg decodeRuntimeMessage( const QByteArray& );
        void setPrintToStdout( bool on ) { d_printToStdout = on; }

        // abort is reliable in any thread as long as the JIT is off
        void terminate( bool silent = false );
        bool isAborted() const;
        void setJit( bool on ); // default on
        void setInterruptible( bool on ); // count hook which checks the abort flag

        void setDbgShell( DbgShell3* s ) { d_shell = s; }
        void setDebug( bool on ); // switches the JIT off while on
        bool isDebug() const { return d_debugging; }
        void setAliveSignal( bool on );
        void hideSource( const QByteArray& source ); // stepping never stops there
        void addBreak( const QByteArray& source, quint32 line );
        void removeBreak( const QByteArray& source, quint32 line );
        void removeAllBreaks( const QByteArray& source = QByteArray() );
        const Breaks& getBreaks( const QByteArray& source ) const;
        void runToNextLine( DebugCommand = StepNext );
        void runToBreakPoint();
        bool isStepping() const { return d_dbgCmd == StepNext || d_dbgCmd == StepOver || d_dbgCmd == StepOut; }
        bool isWaiting() const { return d_waiting; }
        bool isBreakHit() const { return d_breakHit; }

        // refer to the coroutine in which the break occurred
        lua_State* getBreakThread() const { return d_breakL; }
        StackLevels getStackTrace() const;
        void setActiveLevel( int level ) { d_activeLevel = level; }
        int getActiveLevel() const { return d_activeLevel; }
        LocalVars getLocalVars( bool includeUpvals = true, quint8 resolveTableToLevel = 0,
                                int maxArrayIndex = 10 ) const;
        QByteArray getValueString( int arg, bool showAddress = true ) const;
    signals:
        void onNotify( int messageType, QByteArray val1 = "", int val2 = 0 );
    protected:
        void notify( MessageType, const QByteArray& val1 = "", int val2 = 0 );
        QVariant getValue( lua_State*, int arg, quint8 resolveTableToLevel, int maxArrayIndex ) const;
    private:
        static void debugHook( lua_State*, lua_Debug* );
        static void countHook( lua_State*, lua_Debug* );
        static int errHandler( lua_State* );
        static int print( lua_State* );
        static int writeOut( lua_State* );
        static int writeErr( lua_State* );
        void updateHook();
        bool stepTarget( lua_State* ) const;
        void waitForCommand( lua_State*, const QByteArray& source, quint32 line, MessageType );

        lua_State* d_ctx;
        lua_State* d_breakL;
        lua_State* d_curL;
        lua_State* d_stepL;
        DbgShell3* d_shell;
        BreaksPerScript d_breaks;
        QSet<QByteArray> d_hidden;
        QByteArray d_curScript;
        QByteArray d_lastError;
        quint32 d_curLine;
        int d_stepDepth;
        int d_activeLevel;
        QAtomicInt d_abort; // 0, Abort, AbortSilently
        quint8 d_dbgCmd;
        bool d_debugging;
        bool d_aliveSignal;
        bool d_interruptible;
        bool d_jit;
        bool d_waiting;
        bool d_breakHit;
        bool d_printToStdout;
    };
}

#endif // LUA_ENGINE3_H
