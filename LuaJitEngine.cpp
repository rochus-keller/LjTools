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

#include "LuaJitEngine.h"
#include <QFileInfo>
#include <QtDebug>
#include <lj_bc.h>
#include <cmath>
using namespace Lua;

QSet<JitEngine::Table*> JitEngine::Table::d_all;
QSet<JitEngine::TableRef*> JitEngine::TableRef::d_all;

JitEngine::JitEngine(QObject *parent) : QObject(parent)
{

}

JitEngine::~JitEngine()
{
    collectGarbage();
}

bool JitEngine::run(JitBytecode* bc)
{
    if( bc == 0 )
        return false;
    reset();
    JitBytecode::FuncRef f( bc->getRoot() );
    if( f == 0 )
        return error(tr("invalid chunk"));

    Closure c(f.data());
    QVariantList vl;
    const bool res = run(&d_root,&c, vl);
    if( res )
    {
        foreach( const QVariant& v, vl )
            qDebug() << "Result:" << v;
    }
    return res;
}

void JitEngine::reset()
{
    d_globals.clear();
    collectGarbage();
    installLibs();
}

void JitEngine::collectGarbage()
{
    // Clear
    QSet<Table*>::const_iterator t;
    for( t = Table::d_all.begin(); t != Table::d_all.end(); ++t )
        (*t)->d_marked = false;

    // Mark
    QSet<TableRef*>::const_iterator r;
    for( r = TableRef::d_all.begin(); r != TableRef::d_all.end(); ++r )
        (*r)->d_table->d_marked = true;

    // Sweep
    QList<Table*> toDelete;
    for( t = Table::d_all.begin(); t != Table::d_all.end(); ++t )
    {
        if( !(*t)->d_marked )
            toDelete << (*t); // don't delete inline to avoid crash
    }
    foreach( Table* t, toDelete )
        delete t;
}

bool JitEngine::error(const QString& msg) const
{
    qCritical() << msg;
    return false;
}

void JitEngine::installLibs()
{
    d_globals[QByteArray("print")] = QVariant::fromValue( CFunction(_print) );
    d_globals[QByteArray("_VERSION")] = QByteArray("TestVM");
}

bool JitEngine::error2( const Frame& f, const QString& msg ) const
{
    QFileInfo info(f.d_func->d_func->d_sourceFile);
    if( f.d_pc < f.d_func->d_func->d_lines.size() )
        return error( QString("%1:%2: %3").arg(info.fileName()).arg(f.d_func->d_func->d_lines[f.d_pc]).arg(msg) );
    else
        return error( QString("%1:%2:%3: %4").arg(info.fileName()).arg(f.d_func->d_func->d_id).arg(f.d_pc).arg(msg) );
}

QVariant JitEngine::getSlot(const JitEngine::Frame& f, int i) const
{
    if( i < f.d_slots.size() )
        return f.d_slots[i]->d_val;
    else
    {
        error2( f, tr("accessing invalid slot number %1").arg(i));
        return QVariant();
    }
}

JitEngine::TableRef JitEngine::getTable(const Frame& f, int i) const
{
    const QVariant v = getSlot(f, i);
    if( !v.canConvert<TableRef>() )
    {
        error2(f,"not a table reference");
        return TableRef();
    }else
        return v.value<TableRef>();
}

void JitEngine::setSlot(const JitEngine::Frame& f, int i, const QVariant& v)
{
    if( i < f.d_slots.size() )
    {
        f.d_slots[i]->d_val = v;
    }else
        error2( f, tr("accessing invalid slot number %1").arg(i));
}

QVariant JitEngine::getUpvalue(const JitEngine::Frame& f, int i) const
{
    if( i < f.d_func->d_upvals.size() )
        return f.d_func->d_upvals[i]->d_val;
    else
    {
        error2( f, tr("accessing invalid upvalue number %1").arg(i));
        return QVariant();
    }
}

void JitEngine::setUpvalue(const JitEngine::Frame& f, int i, const QVariant& v)
{
    if( i < f.d_func->d_upvals.size() )
    {
        f.d_func->d_upvals[i]->d_val = v;
    }else
        error2( f, tr("accessing invalid upvalue number %1").arg(i));
}

QVariant JitEngine::getNumConst(const JitEngine::Frame& f, int i) const
{
    if( i < f.d_func->d_func->d_constNums.size() )
        return f.d_func->d_func->d_constNums[i];
    else
    {
        error2( f, tr("accessing invalid constant number %1").arg(i));
        return QVariant();
    }
}

QVariant JitEngine::getGcConst(const JitEngine::Frame& f, int i) const
{
    // negated!
    if( i < f.d_func->d_func->d_constObjs.size() )
        return f.d_func->d_func->d_constObjs[ f.d_func->d_func->d_constObjs.size() - i - 1 ];
    else
    {
        error2( f, tr("accessing invalid constant object %1").arg(i));
        return QVariant();
    }
}

QVariant JitEngine::getPriConst(int i)
{
    switch(i)
    {
    case 1:
        return false;
    case 2:
        return true;
    default:
        return QVariant();
    }
}

int JitEngine::_print(JitEngine* eng, QVariantList& inout)
{
    QByteArray str;
    foreach( const QVariant& v, inout )
        str += tostring(v);
    emit eng->sigPrint( str );
    return 0;
}

QByteArray JitEngine::tostring(const QVariant& v)
{
    if( v.canConvert<TableRef>() )
    {
        TableRef t = v.value<TableRef>();
        return "table: 0x" + QByteArray::number( quint32(t.deref()), 16 );
    }else if( v.canConvert<Closure>() )
    {
        Closure c = v.value<Closure>();
        return "function: 0x" + QByteArray::number( quint32(c.d_func.constData()), 16 );
    }else if( v.canConvert<CFunction>())
    {
        CFunction f = v.value<CFunction>();
        return "native: 0x" + QByteArray::number( quint32(f.d_func), 16 );
    }else
        return v.toByteArray();
}

bool JitEngine::run(Frame* outer, Closure* c, QVariantList& inout)
{
    Q_ASSERT( c != 0 );
    Frame f(outer,c);
    for( int i = 0; i < c->d_func->d_framesize; i++ )
    {
        f.d_slots.append( SlotRef(new Slot()) );
        if( i < inout.size() )
            f.d_slots.back()->d_val = inout[i];
    }

    while( true )
    {
        if( f.d_pc >= c->d_func->d_byteCodes.size() )
            return error2(f,"pc points out of bytecode");
        JitBytecode::ByteCode bc = JitBytecode::dissectByteCode(c->d_func->d_byteCodes[f.d_pc]);
        switch( bc.d_op )
        {
        // Returns, good exits **********************************
        case BC_RET0:
            inout.clear();
            return true;
        case BC_RET1:
            inout.clear();
            inout.append( getSlot( f, bc.d_a ) );
            return true;
        case BC_RET:
            inout.clear();
            for( int i = bc.d_a; i < ( bc.d_a + bc.getCd() - 2 ); i++ )
                inout.append( getSlot( f, i ) );
            return true;

        // Unary ops
        case BC_MOV:
            setSlot(f, bc.d_a, getSlot( f, bc.getCd() ) );
            f.d_pc++;
            break;
        case BC_NOT:
            setSlot(f, bc.d_a, !getSlot( f, bc.getCd() ).toBool() );
            f.d_pc++;
            break;
        case BC_UNM:
            setSlot(f, bc.d_a, -getSlot( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_LEN:
            {
                const QVariant v = getSlot( f, bc.getCd() );
                if( v.type() == QVariant::ByteArray )
                    setSlot(f, bc.d_a, v.toByteArray().size() );
                else if( v.canConvert<TableRef>() )
                    setSlot(f, bc.d_a, v.value<TableRef>().deref()->size() ); // TODO: may be wrong
                else
                    return error2(f,tr("invalid application of LEN").arg(bc.getCd()) );
                f.d_pc++;
            }
            break;


        // Binary Ops **********************************
        case BC_ADDVN:
            setSlot(f, bc.d_a, getSlot( f, bc.d_b ).toDouble() + getNumConst( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_SUBVN:
            setSlot(f, bc.d_a, getSlot( f, bc.d_b ).toDouble() - getNumConst( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_MULVN:
            setSlot(f, bc.d_a, getSlot( f, bc.d_b ).toDouble() * getNumConst( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_DIVVN:
            setSlot(f, bc.d_a, getSlot( f, bc.d_b ).toDouble() / getNumConst( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_MODVN:
            setSlot(f, bc.d_a, std::fmod(getSlot( f, bc.d_b ).toDouble(), getNumConst( f, bc.getCd() ).toDouble()) );
            f.d_pc++;
            break;

        case BC_ADDNV:
            setSlot(f, bc.d_a, getNumConst( f, bc.getCd() ).toDouble() + getSlot( f, bc.d_b ).toDouble() );
            f.d_pc++;
            break;
        case BC_SUBNV:
            setSlot(f, bc.d_a, getNumConst( f, bc.getCd() ).toDouble() - getSlot( f, bc.d_b ).toDouble() );
            f.d_pc++;
            break;
        case BC_MULNV:
            setSlot(f, bc.d_a, getNumConst( f, bc.getCd() ).toDouble() * getSlot( f, bc.d_b ).toDouble() );
            f.d_pc++;
            break;
        case BC_DIVNV:
            setSlot(f, bc.d_a, getNumConst( f, bc.getCd() ).toDouble() / getSlot( f, bc.d_b ).toDouble() );
            f.d_pc++;
            break;
        case BC_MODNV:
            setSlot(f, bc.d_a, std::fmod(getNumConst( f, bc.getCd() ).toDouble(), getSlot( f, bc.d_b ).toDouble()) );
            f.d_pc++;
            break;

        case BC_ADDVV:
            setSlot(f, bc.d_a, getSlot( f, bc.d_b ).toDouble() + getSlot( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_SUBVV:
            setSlot(f, bc.d_a, getSlot( f, bc.d_b ).toDouble() - getSlot( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_MULVV:
            setSlot(f, bc.d_a, getSlot( f, bc.d_b ).toDouble() * getSlot( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_DIVVV:
            setSlot(f, bc.d_a, getSlot( f, bc.d_b ).toDouble() / getSlot( f, bc.getCd() ).toDouble() );
            f.d_pc++;
            break;
        case BC_MODVV:
            setSlot(f, bc.d_a, std::fmod( getSlot( f, bc.d_b ).toDouble(), getSlot( f, bc.getCd() ).toDouble() ) );
            f.d_pc++;
            break;

        case BC_POW:
            setSlot(f, bc.d_a, std::pow( getSlot( f, bc.d_b ).toDouble(), getSlot( f, bc.getCd() ).toDouble() ) );
            f.d_pc++;
            break;

        case BC_CAT:
            {
                QByteArray res;
                for( int i = bc.d_b; i <= bc.getCd(); i++ )
                    res += getSlot( f, i ).toByteArray();
                setSlot( f, bc.d_a, res );
                f.d_pc++;
            }
            break;

        // Constant Ops **********************************
        case BC_KSHORT:
            setSlot(f, bc.d_a, bc.getCd() );
            f.d_pc++;
            break;
        case BC_KSTR:
            setSlot(f, bc.d_a, getGcConst(f,bc.getCd()));
            f.d_pc++;
            break;

        // Table Ops **********************************
        case BC_GSET:
            d_globals[ getGcConst(f,bc.getCd()) ] = getSlot( f, bc.d_a );
            f.d_pc++;
            break;
        case BC_GGET:
            setSlot(f, bc.d_a, d_globals.value(getGcConst(f,bc.getCd())) );
            f.d_pc++;
            break;
        case BC_TNEW:
            setSlot(f, bc.d_a, QVariant::fromValue(TableRef(new Table() ) ) );
            f.d_pc++;
            break;
        case BC_TGETV:
            {
                TableRef t = getTable(f,bc.d_b);
                if( t.isNull() )
                    return false;
                setSlot(f,bc.d_a, t.deref()->value(getSlot(f,bc.getCd()) ) );
                f.d_pc++;
            }
            break;
        case BC_TGETS:
            {
                TableRef t = getTable(f,bc.d_b);
                if( t.isNull() )
                    return false;
                setSlot(f,bc.d_a, t.deref()->value(getGcConst(f,bc.getCd()) ) );
                f.d_pc++;
            }
            break;
        case BC_TGETB:
            {
                TableRef t = getTable(f,bc.d_b);
                if( t.isNull() )
                    return false;
                setSlot(f,bc.d_a, t.deref()->value(bc.getCd()) );
                f.d_pc++;
            }
        case BC_TSETV:
            {
                TableRef t = getTable(f,bc.d_b);
                if( t.isNull() )
                    return false;
                t.deref()->insert( getSlot(f,bc.getCd()), getSlot(f,bc.d_a) );
                f.d_pc++;
            }
            break;
        case BC_TSETS:
            {
                TableRef t = getTable(f,bc.d_b);
                if( t.isNull() )
                    return false;
                t.deref()->insert( getGcConst(f,bc.getCd()), getSlot(f,bc.d_a) );
                f.d_pc++;
            }
            break;
        case BC_TSETB:
            {
                TableRef t = getTable(f,bc.d_b);
                if( t.isNull() )
                    return false;
                t.deref()->insert( bc.getCd(), getSlot(f,bc.d_a) );
                f.d_pc++;
            }
            break;

        // Upvalue and Function ops **********************************
        case BC_UGET:
            setSlot(f, bc.d_a, getUpvalue( f, bc.getCd() ) );
            f.d_pc++;
            break;
        case BC_USETV:
            setUpvalue( f, bc.d_a, getSlot( f, bc.getCd() ) );
            f.d_pc++;
            break;
        case BC_USETS:
            setUpvalue( f, bc.d_a, getGcConst( f, bc.getCd() ) );
            f.d_pc++;
            break;
        case BC_USETN:
            setUpvalue( f, bc.d_a, getNumConst( f, bc.getCd() ) );
            f.d_pc++;
            break;
        case BC_USETP:
            setUpvalue( f, bc.d_a, getPriConst( bc.getCd() ) );
            f.d_pc++;
            break;
        case BC_UCLO:
            for( int i = bc.d_a; i < f.d_slots.size(); i++ )
                f.d_slots[i]->d_closed = true;
            if( bc.getCd() )
                f.d_pc += bc.getCd();
            else
                f.d_pc++;
            break;
        case BC_FNEW:
            {
                const QVariant fv = getGcConst(f,bc.getCd());
                JitBytecode::FuncRef p;
                if( fv.canConvert<JitBytecode::FuncRef>() )
                    p = fv.value<JitBytecode::FuncRef>();
                else
                    return error2(f,tr("slot %1 is not a function prototype").arg(bc.getCd()) );

                Closure cc(p.data());
                for( int i = 0; i < p->d_upvals.size(); i++ )
                {
                    const int u = p->getUpval(i);
                    if( p->isLocalUpval(i) )
                    {
                        if( u < f.d_slots.size() )
                            cc.d_upvals << f.d_slots[u];
                        else
                            error2( f, tr("accessing invalid slot number %1 through upval %2").arg(p->d_upvals[i]).arg(i));
                    }else
                    {
                        if( u < c->d_upvals.size() )
                            cc.d_upvals << c->d_upvals[u];
                        else
                            error2( f, tr("accessing invalid upval number %1").arg(p->d_upvals[i]));
                    }
                }
                setSlot(f, bc.d_a, QVariant::fromValue( cc ) );
                f.d_pc++;
            }
            break;

        // Calls and Vararg Handling **********************************
        case BC_CALL:
            {
                const QVariant vc = getSlot( f, bc.d_a );
                if( vc.canConvert<Closure>())
                {
                    Closure c = vc.value<Closure>();
                    QVariantList inout;
                    for( int i = bc.d_a + 1; i <= ( bc.d_a + bc.getCd() - 1 ); i++ )
                        inout.append( getSlot(f,i) );
                    if( !run( &f, &c, inout ) )
                        return false;
                    for( int i = bc.d_a; i <= ( bc.d_a + bc.d_b - 2 ) && i < inout.size(); i++ )
                        setSlot( f, i, inout[i] );
                }else if( vc.canConvert<CFunction>() )
                {
                    CFunction c = vc.value<CFunction>();
                    QVariantList inout;
                    for( int i = bc.d_a + 1; i <= ( bc.d_a + bc.getCd() - 1 ); i++ )
                        inout.append( getSlot(f,i) );
                    const int res = c.d_func(this,inout);
                    if( res < 0 )
                        return false;
                    for( int i = bc.d_a; i <= ( bc.d_a + bc.d_b - 2 ) && i < inout.size() && i < res; i++ )
                        setSlot( f, i, inout[i] );
                }else
                    return error2(f,tr("slot %1 is neither a closure nor a cfunction").arg(bc.d_a) );
                f.d_pc++;
            }
            break;

        // Bad exit **********************************
        default:
            return error2( f, tr("opcode not yet supported: %1").arg(bc.d_name));
        }
    }
    return true;
}

