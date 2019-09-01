#ifndef LUAJITENGINE_H
#define LUAJITENGINE_H

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

#include <QObject>
#include <QSet>
#include <QVariant>
#include <QPointer>
#include <LjTools/LuaJitBytecode.h>

namespace Lua
{
    class JitBytecode;

    // Execute LuaJIT bytecode; focus is on verification and comprehension, not performance
    class JitEngine : public QObject
    {
        Q_OBJECT
    public:
        class Table : public QHash<QVariant,QVariant>
        {
        public:
            static Table* create() { return new Table(); }
            QVariant d_metaTable;
       private:
            friend class JitEngine;
            Table():d_marked(false)
            {
                d_all.insert(this);
            }
            ~Table()
            {
                d_all.remove(this);
            }
            static QSet<Table*> d_all;
            bool d_marked;
        };
        class TableRef
        {
        public:
            TableRef( Table* t = 0 ):d_table(t)
            {
                if( t )
                    d_all.insert(this);
            }
            TableRef( const TableRef& rhs ):d_table(0)
            {
                *this = rhs;
            }
            TableRef& operator=( const TableRef& rhs )
            {
                if( d_table && rhs.d_table == 0 )
                    d_all.remove(this);
                if( d_table == 0 && rhs.d_table != 0 )
                    d_all.insert(this);
                d_table = rhs.d_table;
                return *this;
            }
            ~TableRef()
            {
                if( d_table )
                    d_all.remove(this);
            }
            Table* deref() const { return d_table; }
            bool isNull() const { return d_table == 0; }
        private:
            Table* d_table;
            static QSet<TableRef*> d_all;
            friend class JitEngine;
        };
        struct Slot : public QSharedData
        {
            QVariant d_val;
            bool d_outOfScope;
            bool d_closed;
        };
        typedef QExplicitlySharedDataPointer<Slot> SlotRef;
        struct Closure
        {
            Closure(JitBytecode::Function* f = 0 ):d_func(f) {}
            JitBytecode::FuncRef d_func;
            QList<SlotRef> d_upvals;
        };
        struct CFunction
        {
            typedef int (*Imp)(JitEngine*, QVariantList& inout );
            CFunction(Imp f = 0):d_func(f){}
            Imp d_func;
            // returns 0..n ...number of return values, -1 ...error
        };


        explicit JitEngine(QObject *parent = 0);
        ~JitEngine();

        bool run(JitBytecode*);

    signals:
        void sigPrint( const QString&, bool err = false );

    protected:
        void reset();
        void collectGarbage();
        bool error( const QString& ) const;
        void installLibs();

        struct Frame
        {
            Frame(Frame* outer = 0, Closure* f = 0):d_pc(0),d_func(f),d_outer(outer){}

            Frame* d_outer;
            Closure* d_func;
            QList<SlotRef> d_slots;
            quint32 d_pc;
        };

        bool run(Frame* outer, Closure*, QVariantList& inout );
        bool error2(const Frame&, const QString& msg ) const;
        QVariant getSlot( const Frame&, int i ) const;
        TableRef getTable( const Frame&, int i ) const;
        void setSlot(const Frame&, int i, const QVariant& );
        QVariant getUpvalue( const Frame&, int i ) const;
        void setUpvalue(const Frame&, int i, const QVariant& );
        QVariant getNumConst( const Frame& f, int i ) const;
        QVariant getGcConst( const Frame& f, int i ) const;
        QVariant getPriConst(int i );

        static int _print(JitEngine*,  QVariantList& inout );
        static QByteArray tostring( const QVariant& v );
    private:
        QHash<QVariant,QVariant> d_globals;
        Frame d_root;
    };
}

Q_DECLARE_METATYPE( Lua::JitEngine::TableRef )
Q_DECLARE_METATYPE( Lua::JitEngine::Closure )
Q_DECLARE_METATYPE( Lua::JitEngine::CFunction )

#endif // LUAJITENGINE_H
