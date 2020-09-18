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

enum MetaEvent { ADD, SUB, MUL, DIV, MOD, POW, UNM, CAT, LEN, EQ, LT, LE, IDX, NIDX, CALL };
static const char* metaEventName[] =
{
    "__add",
    "__sub",
    "__mul",
    "__div",
    "__mod",
    "__pow",
    "__unm",
    "__concat",
    "__len",
    "__eq",
    "__lt",
    "__le",
    "__index",
    "__newindex",
    "__call"
};

/* TODO

  require
  finish meta methods

*/

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
    if( f.constData() == 0 )
        return error(tr("invalid chunk"));

    Closure c(f.data());
    QVariantList vl;
    const bool res = run(&d_root,&c, vl);
    if( res )
    {
        foreach( const QVariant& v, vl )
            emit sigPrint( tostring(v) );
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
    d_globals[QByteArray("dbgout")] = QVariant::fromValue( CFunction(_print) );
    d_globals[QByteArray("setmetatable")] = QVariant::fromValue( CFunction(_setmetatable) );
    d_globals[QByteArray("getmetatable")] = QVariant::fromValue( CFunction(_getmetatable) );
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

JitEngine::Slot*JitEngine::getSlot(const JitEngine::Frame& f, int i) const
{
    if( i < f.d_slots.size() )
        return f.d_slots[i].data();
    else
    {
        error2( f, tr("accessing invalid slot number %1").arg(i));
        return 0;
    }
}

QVariant JitEngine::getSlotVal(const JitEngine::Frame& f, int i) const
{
    Slot* s = getSlot(f,i);
    if( s )
        return s->d_val;
    else
        return QVariant();
}

JitEngine::TableRef JitEngine::getTable(const Frame& f, int i) const
{
    const QVariant v = getSlotVal(f, i);
    if( !v.canConvert<TableRef>() )
    {
        error2(f,"not a table reference");
        return TableRef();
    }else
        return v.value<TableRef>();
}

void JitEngine::setSlotVal(const JitEngine::Frame& f, int i, const QVariant& v)
{
    Slot* s = getSlot(f,i);
    if( s )
        s->d_val = v;
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
    for( int i = 0; i < inout.size(); i++ )
    {
        const QVariant& v = inout[i];
        if( i != 0 )
            str += "\t";
        str += tostring(v);
    }
    emit eng->sigPrint( str );
    return 0;
}

QByteArray JitEngine::tostring(const QVariant& v)
{
    if( v.canConvert<TableRef>() )
    {
        TableRef t = v.value<TableRef>();
        return "table: 0x" + QByteArray::number( quintptr(t.deref()), 16 );
    }else if( v.canConvert<Closure>() )
    {
        Closure c = v.value<Closure>();
        return "function: 0x" + QByteArray::number( quintptr(c.d_func.constData()), 16 );
    }else if( v.canConvert<CFunction>())
    {
        CFunction f = v.value<CFunction>();
        return "native: 0x" + QByteArray::number( quintptr(f.d_func), 16 );
    }else
        return v.toByteArray();
}

int JitEngine::_setmetatable(JitEngine* eng, QVariantList& inout)
{
    if( inout.size() != 2 )
        eng->error(tr("expecting two argument"));
    else if( !inout[0].canConvert<TableRef>() )
        eng->error(tr("expecting a table as first argument"));
    else if( !inout[1].isNull() && !inout[1].canConvert<TableRef>() )
        eng->error(tr("expecting a table or null as second argument"));
    else
    {
        TableRef t = inout[0].value<TableRef>();
        if( inout[1].isNull() )
            t.deref()->d_metaTable = 0;
        else
            t.deref()->d_metaTable = inout[1].value<TableRef>().deref();
        inout.clear();
        inout << QVariant::fromValue(t);
        return 1;
    }
    return -1;
}

int JitEngine::_getmetatable(JitEngine* eng, QVariantList& inout)
{
    if( inout.size() != 1 )
        eng->error(tr("expecting one argument"));
    else if( !inout[0].canConvert<TableRef>() )
        eng->error(tr("expecting a table"));
    else
    {
        TableRef t = inout[0].value<TableRef>();
        inout.clear();
        if( t.deref()->d_metaTable.deref() )
            inout << QVariant::fromValue( TableRef(t.deref()->d_metaTable.deref()) );
        else
            inout << QVariant();
        return 1;
    }
    return -1;
}

bool JitEngine::isTrue(const QVariant& v)
{
    return !v.isNull() && !( v.type() == QVariant::Bool && v.toBool() == false );
}

QVariant JitEngine::getBinHandler(const QVariant& lhs, const QVariant& rhs, int event)
{
    QVariant v = getHandler(lhs, event);
    if( v.isValid() )
        return v;
    return getHandler(rhs,event);
}

QVariant JitEngine::getHandler(const QVariant& v, int event)
{
    if( v.canConvert<TableRef>() )
    {
        TableRef t = v.value<TableRef>();
        if( t.deref()->d_metaTable.deref() )
            return t.deref()->d_metaTable.deref()->d_hash.value(QByteArray(metaEventName[event]));
    }
    return QVariant();
}

QVariant JitEngine::getCompHandler(const QVariant& lhs, const QVariant& rhs, int event)
{
    if( lhs.type() != rhs.type() )
        return QVariant();
    QVariant h1 = getHandler(lhs,event);
    QVariant h2 = getHandler(rhs,event);
    if( h1 == h2 )
        return h1;
    else
        return QVariant();
}

bool JitEngine::doCompare(JitEngine::Frame& f, const JitBytecode::Instruction& bc)
{
    const QVariant lhs = getSlotVal(f, bc.d_a );
    const QVariant rhs = getSlotVal(f, bc.getCd() );
    bool res = false;
    if( JitBytecode::isNumber( lhs ) && JitBytecode::isNumber( rhs ) )
    {
        switch( bc.d_op )
        {
        case BC_ISLT:
            res = lhs.toDouble() < rhs.toDouble();
            break;
        case BC_ISGE:
            res = lhs.toDouble() >= rhs.toDouble();
            break;
        case BC_ISLE:
            res = lhs.toDouble() <= rhs.toDouble();
            break;
        case BC_ISGT:
            res = lhs.toDouble() > rhs.toDouble();
            break;
        default:
            break;
        }
    }else if( JitBytecode::isString( lhs ) && JitBytecode::isString( rhs ) )
    {
        switch( bc.d_op )
        {
        case BC_ISLT:
            res = lhs.toByteArray() < rhs.toByteArray();
            break;
        case BC_ISGE:
            res = lhs.toByteArray() >= rhs.toByteArray();
            break;
        case BC_ISLE:
            res = lhs.toByteArray() <= rhs.toByteArray();
            break;
        case BC_ISGT:
            res = lhs.toByteArray() > rhs.toByteArray();
            break;
        default:
            break;
        }
    }else
    {
        QVariantList inout;
        QVariant c;
        switch( bc.d_op )
        {
        case BC_ISLT:
            c = getCompHandler(lhs,rhs, LT );
            inout << lhs << rhs;
            break;
        case BC_ISGT:
            c = getCompHandler(lhs,rhs, LT );
            inout << rhs << lhs;
            break;
        case BC_ISLE:
            c = getCompHandler(lhs,rhs, LE );
            inout << lhs << rhs;
            break;
        case BC_ISGE:
            c = getCompHandler(lhs,rhs, LE );
            inout << rhs << lhs;
            break;
        }
        if( !c.isValid() && ( bc.d_op == BC_ISLE || bc.d_op == BC_ISGE ) )
        {
            c = getCompHandler(lhs,rhs, LT );
            qSwap( inout[0], inout[1] );
        }
        if( c.isValid() )
        {
            if( doCall(f,c,inout) < 0 )
                return false;
            if( inout.isEmpty() )
                return error2(f,tr("bind handler not returning a value"));
            res = inout.first().toBool();
        }else
            return error2(f, "incompatible types for comparison");
    }

    return doJumpAfterCompare(f, res);
}

bool JitEngine::doEquality(JitEngine::Frame& f, const JitBytecode::Instruction& bc)
{
    const QVariant lhs = getSlotVal(f, bc.d_a );
    QVariant rhs;
    switch( bc.d_op )
    {
    case BC_ISEQV:
    case BC_ISNEV:
        rhs = getSlotVal(f, bc.getCd() );
        break;
    case BC_ISEQS:
    case BC_ISNES:
        rhs = getGcConst( f, bc.getCd() );
        break;
    case BC_ISEQN:
    case BC_ISNEN:
        rhs = getNumConst( f, bc.getCd() );
        break;
    case BC_ISEQP:
    case BC_ISNEP:
        rhs = getPriConst( bc.getCd() );
        break;
    }
    bool res = false;
    if( ( JitBytecode::isNumber( lhs ) && JitBytecode::isNumber( rhs ) ) ||
            ( JitBytecode::isString( lhs ) && JitBytecode::isString( rhs ) ) )
    {
        switch( bc.d_op )
        {
        case BC_ISEQV:
        case BC_ISEQS:
        case BC_ISEQN:
        case BC_ISEQP:
            res = lhs.toByteArray() == rhs.toByteArray();
            break;
        default:
            res = lhs.toByteArray() != rhs.toByteArray();
            break;
        }
    }else if( lhs.canConvert<TableRef>() && rhs.canConvert<TableRef>() )
    {
        switch( bc.d_op )
        {
        case BC_ISEQV:
        case BC_ISEQS:
        case BC_ISEQN:
        case BC_ISEQP:
            res = lhs.value<TableRef>().deref() == rhs.value<TableRef>().deref();
            break;
        default:
            res = lhs.value<TableRef>().deref() != rhs.value<TableRef>().deref();
            break;
        }
    }else if( lhs.canConvert<Closure>() && rhs.canConvert<Closure>() )
    {
        switch( bc.d_op )
        {
        case BC_ISEQV:
        case BC_ISEQS:
        case BC_ISEQN:
        case BC_ISEQP:
            res = lhs.value<Closure>().d_func.data() == rhs.value<Closure>().d_func.data();
            break;
        default:
            res = lhs.value<Closure>().d_func.data() != rhs.value<Closure>().d_func.data();
            break;
        }
    }else if( lhs.canConvert<CFunction>() && rhs.canConvert<CFunction>() )
    {
        switch( bc.d_op )
        {
        case BC_ISEQV:
        case BC_ISEQS:
        case BC_ISEQN:
        case BC_ISEQP:
            res = lhs.value<CFunction>().d_func == rhs.value<CFunction>().d_func;
            break;
        default:
            res = lhs.value<CFunction>().d_func != rhs.value<CFunction>().d_func;
            break;
        }
    }else
    {
        QVariant c = getCompHandler(lhs,rhs, EQ );
        if( c.isValid() )
        {
            QVariantList inout;
            inout << lhs << rhs;
            if( doCall(f,c,inout) < 0 )
                return false;
            if( inout.isEmpty() )
                return error2(f,tr("bind handler not returning a value"));
            res = inout.first().toBool();
            switch( bc.d_op )
            {
            case BC_ISNEV:
            case BC_ISNES:
            case BC_ISNEN:
            case BC_ISNEP:
                res = !res;
                break;
            }
        }// else res == false
    }
    return doJumpAfterCompare(f, res);
}

bool JitEngine::doJumpAfterCompare(JitEngine::Frame& f, bool res)
{
    f.d_pc++;
    if( f.d_pc >= f.d_func->d_func->d_byteCodes.size() )
        return true; // handle error in main loop
    JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f.d_func->d_func->d_byteCodes[f.d_pc]);
    if( bc.d_op != BC_JMP )
        return error2(f, "comparison op must be followed by JMP");
    f.d_pc++; // relative to next instruction
    if( res )
        f.d_pc += bc.getCd();
    return true;
}

bool JitEngine::doArith(JitEngine::Frame& f, const JitBytecode::Instruction& bc)
{
    QVariant lhs, rhs;

    switch( bc.d_op )
    {
    case BC_ADDVN:
    case BC_SUBVN:
    case BC_MULVN:
    case BC_DIVVN:
    case BC_MODVN:
        lhs = getSlotVal( f, bc.d_b );
        rhs = getNumConst( f, bc.getCd() );
        break;
    case BC_ADDNV:
    case BC_SUBNV:
    case BC_MULNV:
    case BC_DIVNV:
    case BC_MODNV:
        lhs = getNumConst( f, bc.getCd() );
        rhs = getSlotVal( f, bc.d_b );
        break;
    case BC_ADDVV:
    case BC_SUBVV:
    case BC_MULVV:
    case BC_DIVVV:
    case BC_MODVV:
    case BC_POW:
        lhs = getSlotVal( f, bc.d_b );
        rhs = getSlotVal( f, bc.getCd() );
        break;
    }

    if( JitBytecode::isNumber(lhs) && JitBytecode::isNumber(rhs) )
    {
        switch( bc.d_op )
        {
        case BC_ADDVN:
            setSlotVal(f, bc.d_a, lhs.toDouble() + rhs.toDouble() );
            break;
        case BC_SUBVN:
            setSlotVal(f, bc.d_a, lhs.toDouble() - rhs.toDouble() );
            break;
        case BC_MULVN:
            setSlotVal(f, bc.d_a, lhs.toDouble() * rhs.toDouble() );
            break;
        case BC_DIVVN:
            setSlotVal(f, bc.d_a, lhs.toDouble() / rhs.toDouble() );
            break;
        case BC_MODVN:
            setSlotVal(f, bc.d_a, std::fmod(lhs.toDouble(), rhs.toDouble()) );
            break;

        case BC_ADDNV:
            setSlotVal(f, bc.d_a, lhs.toDouble() + rhs.toDouble() );
            break;
        case BC_SUBNV:
            setSlotVal(f, bc.d_a, lhs.toDouble() - rhs.toDouble() );
            break;
        case BC_MULNV:
            setSlotVal(f, bc.d_a, lhs.toDouble() * rhs.toDouble() );
            break;
        case BC_DIVNV:
            setSlotVal(f, bc.d_a, lhs.toDouble() / rhs.toDouble() );
            break;
        case BC_MODNV:
            setSlotVal(f, bc.d_a, std::fmod(lhs.toDouble(), rhs.toDouble()) );
            break;

        case BC_ADDVV:
            setSlotVal(f, bc.d_a, lhs.toDouble() + rhs.toDouble() );
            break;
        case BC_SUBVV:
            setSlotVal(f, bc.d_a, lhs.toDouble() - rhs.toDouble() );
            break;
        case BC_MULVV:
            setSlotVal(f, bc.d_a, lhs.toDouble() * rhs.toDouble() );
            break;
        case BC_DIVVV:
            setSlotVal(f, bc.d_a, lhs.toDouble() / rhs.toDouble() );
            break;
        case BC_MODVV:
            setSlotVal(f, bc.d_a, std::fmod( lhs.toDouble(), rhs.toDouble() ) );
            break;

        case BC_POW:
            setSlotVal(f, bc.d_a, std::pow( lhs.toDouble(), rhs.toDouble() ) );
            break;
        }
        return true;
    }else
    {
        int op;
        switch( bc.d_op )
        {
        case BC_ADDVN:
        case BC_ADDNV:
        case BC_ADDVV:
            op = ADD;
            break;
        case BC_SUBVN:
        case BC_SUBNV:
        case BC_SUBVV:
            op = SUB;
            break;
        case BC_MULVN:
        case BC_MULNV:
        case BC_MULVV:
            op = MUL;
            break;
        case BC_DIVVN:
        case BC_DIVNV:
        case BC_DIVVV:
            op = DIV;
            break;
        case BC_MODVN:
        case BC_MODNV:
        case BC_MODVV:
            op = MOD;
            break;
        case BC_POW:
            op = POW;
            break;
        }

        QVariant c = getBinHandler( lhs, rhs, op );
        QVariantList inout;
        inout << lhs << rhs;
        if( doCall(f,c,inout) < 0 )
            return false;
        if( inout.isEmpty() )
            return error2(f,tr("bind handler not returning a value"));
        setSlotVal(f, bc.d_a, inout.first() );
        return true;
    } // else
    return error2(f, tr("operation not compatible with operands") );
}

static QVariant fromPrimitive( int p )
{
    switch( p )
    {
    default:
    case 0:
        return QVariant();
    case 1:
        return false;
    case 2:
        return true;
    }
}

bool JitEngine::doGetT(JitEngine::Frame& f, const JitBytecode::Instruction& bc)
{
    QVariant table = getSlotVal(f, bc.d_b);
    QVariant key;

    switch( bc.d_op )
    {
    case BC_TGETV:
        key = getSlotVal(f,bc.getCd());
        break;
    case BC_TGETS:
        key = getGcConst(f,bc.getCd());
        break;
    case BC_TGETB:
        key = fromPrimitive(bc.getCd());
        break;
    }
    if( table.canConvert<TableRef>() )
    {
        TableRef t = table.value<TableRef>();
        QVariant v = t.deref()->d_hash.value(key);
        if( v.isValid() )
            setSlotVal(f,bc.d_a, v );
        else
        {
            QVariant c;
            c = getHandler(table,IDX);
            if( !c.isValid() )
            {
                if( t.deref()->isUserObject() )
                    return error2( f, tr("no index metha method for object") );
                else
                    setSlotVal(f,bc.d_a, QVariant() );
            }else if( c.canConvert<Closure>() || c.canConvert<CFunction>() )
            {
                QVariantList inout;
                inout << table << key;
                if( doCall(f,c,inout) < 0 )
                    return false;
                if( inout.isEmpty() )
                    return error2(f,tr("bind handler not returning a value"));
            }else if( c.canConvert<TableRef>() )
            {
                TableRef t = c.value<TableRef>();
                setSlotVal(f,bc.d_a, t.deref()->d_hash.value(key) ); // TODO reapply doGetT to c/key
            }else
                setSlotVal(f,bc.d_a, QVariant() );
        }
    }else
        return error2( f, tr("cannot index argument") );

    return true;
}

bool JitEngine::doSetT(JitEngine::Frame& f, const JitBytecode::Instruction& bc)
{
    QVariant table = getSlotVal(f, bc.d_b);
    QVariant key;
    QVariant value = getSlotVal(f,bc.d_a);

    switch( bc.d_op )
    {
    case BC_TSETV:
        key = getSlotVal(f,bc.getCd());
        break;
    case BC_TSETS:
        key = getGcConst(f,bc.getCd());
        break;
    case BC_TSETB:
        key = fromPrimitive(bc.getCd());
        break;
    }

    // TODO: settable_event meta
    if( !table.canConvert<TableRef>() )
    {
        error2(f,"not a table reference");
        return false;
    }else
    {
        TableRef t = table.value<TableRef>();
        t.deref()->d_hash.insert( key, value );
    }

    return true;
}

int JitEngine::doCall(JitEngine::Frame& f, const QVariant& vc, QVariantList& inout )
{
    if( vc.canConvert<Closure>())
    {
        Closure c = vc.value<Closure>();
        if( !run( &f, &c, inout ) )
            return -1;
        else
            return inout.size();
    }else if( vc.canConvert<CFunction>() )
    {
        CFunction c = vc.value<CFunction>();
        return c.d_func(this,inout);
    }else
    {
        error2(f,tr("slot value is not callable") );
        return -1;
    }
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
        // see http://wiki.luajit.org/Bytecode-2.0

        if( f.d_pc >= c->d_func->d_byteCodes.size() )
            return error2(f,"pc points out of bytecode");
        JitBytecode::Instruction bc = JitBytecode::dissectInstruction(c->d_func->d_byteCodes[f.d_pc]);
        switch( bc.d_op )
        {
        // Comparison ops (fully implemented) **********************************
        case BC_ISLT:
        case BC_ISGE:
        case BC_ISLE:
        case BC_ISGT:
            if( !doCompare(f, bc ) )
                return false;
            break;
        case BC_ISEQV:
        case BC_ISNEV:
        case BC_ISEQS:
        case BC_ISNES:
        case BC_ISEQN:
        case BC_ISNEN:
        case BC_ISEQP:
        case BC_ISNEP:
            if( !doEquality(f, bc ) )
                return false;
            break;

        // Loops and branches **********************************
        case BC_JMP:
            f.d_pc += bc.getCd() + 1; // relative to next instruction
            break;
        case BC_FORI:
            {
                const qint32 idx = getSlotVal(f,bc.d_a).toInt();
                const qint32 stop = getSlotVal(f,bc.d_a+1).toInt();
                const qint32 step = getSlotVal(f,bc.d_a+2).toInt();
                Slot* extIdxSlot = getSlot(f,bc.d_a+3);
                setSlotVal(f,bc.d_a+3,idx);
                f.d_pc++; // relative to next instruction
                // All *FOR* instructions check that idx <= stop (if step >= 0 ) or idx >= stop (if step < 0 )
                if( ( step >= 0 && idx <= stop ) || ( step < 0 && idx >= stop ) )
                {
                    // If true, idx is copied to the ext idx slot (visible loop variable in the loop body)
                    extIdxSlot->d_val = idx;
                    // Then the loop body is entered
                }else
                    // Otherwise, the loop is left by continuing with the next instruction after the *FORL .
                    f.d_pc += bc.getCd();
            }
            break;
        case BC_FORL:
            {
                Slot* idxSlot = getSlot(f,bc.d_a);
                const qint32 stop = getSlotVal(f,bc.d_a+1).toInt();
                const qint32 step = getSlotVal(f,bc.d_a+2).toInt();
                Slot* extIdxSlot = getSlot(f,bc.d_a+3);
                // The FORL instructions does idx = idx + step first
                const qint32 idx = idxSlot->d_val.toInt() + step;
                idxSlot->d_val = idx;
                f.d_pc++; // relative to next instruction
                // All *FOR* instructions check that idx <= stop (if step >= 0 ) or idx >= stop (if step < 0 )
                if( ( step >= 0 && idx <= stop ) || ( step < 0 && idx >= stop ) )
                {
                    // If true, idx is copied to the ext idx slot (visible loop variable in the loop body)
                    extIdxSlot->d_val = idx;
                    // Then the loop body is entered
                    f.d_pc += bc.getCd();
                } // else
                    // Otherwise, the loop is left by continuing with the next instruction after the *FORL .
            }
            break;
        case BC_LOOP:
            f.d_pc++;
            break;
            // TODO Pending: ITERL

        // Unary Test and Copy ops (fully implemented) **********************************
        case BC_ISTC:
        case BC_ISFC:
            {
                const QVariant d = getSlotVal(f,bc.getCd());
                if( ( bc.d_op == BC_ISTC && isTrue(d) ) || !isTrue(d) )
                {
                    setSlotVal(f,bc.d_a,d);
                    doJumpAfterCompare(f,true);
                }else
                    f.d_pc++;
            }
            break;
        case BC_IST:
        case BC_ISF:
            {
                const QVariant d = getSlotVal(f,bc.getCd());
                doJumpAfterCompare(f, ( bc.d_op == BC_IST && isTrue(d) ) || !isTrue(d) );
            }
            break;

        // Returns, good exits **********************************
        case BC_RET0:
            inout.clear();
            return true;
        case BC_RET1:
            inout.clear();
            inout.append( getSlotVal( f, bc.d_a ) );
            return true;
        case BC_RET:
            inout.clear();
            for( int i = bc.d_a; i < ( bc.d_a + bc.getCd() - 2 ); i++ )
                inout.append( getSlotVal( f, i ) );
            return true;
            // TODO Pending: RETM

        // Unary ops (fully implemented) **********************************
        case BC_MOV:
            setSlotVal(f, bc.d_a, getSlotVal( f, bc.getCd() ) );
            f.d_pc++;
            break;
        case BC_NOT:
            setSlotVal(f, bc.d_a, !getSlotVal( f, bc.getCd() ).toBool() );
            f.d_pc++;
            break;
        case BC_UNM:
            {
                QVariant val = getSlotVal( f, bc.getCd() );
                if( JitBytecode::isNumber(val) )
                    setSlotVal(f, bc.d_a, -val.toDouble() );
                else
                {
                    QVariant c = getHandler(val, UNM );
                    QVariantList inout;
                    inout << val;
                    if( doCall(f,c,inout) < 0 )
                        return false;
                    if( inout.isEmpty() )
                        return error2(f,tr("bind handler not returning a value"));
                    setSlotVal(f, bc.d_a, inout.first() );
                }
            }
            f.d_pc++;
            break;
        case BC_LEN:
            {
                const QVariant v = getSlotVal( f, bc.getCd() );
                if( v.type() == QVariant::ByteArray )
                    setSlotVal(f, bc.d_a, v.toByteArray().size() );
                else if( v.canConvert<TableRef>() )
                {
                    TableRef t = v.value<TableRef>();
                    if( !t.deref()->isUserObject() )
                        setSlotVal(f, bc.d_a, t.deref()->d_hash.size() );
                    else
                    {
                        QVariant c = getHandler(v, LEN );
                        if( c.isValid() )
                        {
                            QVariantList inout;
                            inout << v;
                            if( doCall(f,c,inout) < 0 )
                                return false;
                            if( inout.isEmpty() )
                                return error2(f,tr("bind handler not returning a value"));
                            setSlotVal(f, bc.d_a, inout.first() );
                        }else
                            return error2(f,tr("no __len meta method found"));
                    }
                }else
                    return error2(f,tr("invalid application of LEN").arg(bc.getCd()) );
                f.d_pc++;
            }
            break;

        // Binary Ops (fully implemented) **********************************
        case BC_ADDVN:
        case BC_SUBVN:
        case BC_MULVN:
        case BC_DIVVN:
        case BC_MODVN:
        case BC_ADDNV:
        case BC_SUBNV:
        case BC_MULNV:
        case BC_DIVNV:
        case BC_MODNV:
        case BC_ADDVV:
        case BC_SUBVV:
        case BC_MULVV:
        case BC_DIVVV:
        case BC_MODVV:
        case BC_POW:
            if( !doArith( f, bc ) )
                return false;
            f.d_pc++;
            break;

        case BC_CAT:
            {
                QByteArray res;
                for( int i = bc.d_b; i <= bc.getCd(); i++ )
                    res += getSlotVal( f, i ).toByteArray();
                setSlotVal( f, bc.d_a, res );
                f.d_pc++;
                // TODO: call meta method if avail
            }
            break;

        // Constant Ops **********************************
        case BC_KSHORT:
            setSlotVal(f, bc.d_a, bc.getCd() );
            f.d_pc++;
            break;
        case BC_KSTR:
        case BC_KCDATA:
            setSlotVal(f, bc.d_a, getGcConst(f,bc.getCd()));
            f.d_pc++;
            break;
        case BC_KNUM:
            setSlotVal(f, bc.d_a, getNumConst(f,bc.getCd()));
            f.d_pc++;
            break;
        case BC_KPRI:
            setSlotVal(f, bc.d_a, getPriConst(bc.getCd()));
            f.d_pc++;
            break;
        case BC_KNIL:
            for( int i = bc.d_a; i <= bc.getCd(); i++ )
                setSlotVal(f, i, QVariant() );
            f.d_pc++;
            break;

        // Table Ops **********************************
        case BC_GSET:
            d_globals[ getGcConst(f,bc.getCd()) ] = getSlotVal( f, bc.d_a );
            f.d_pc++;
            break;
        case BC_GGET:
            setSlotVal(f, bc.d_a, d_globals.value(getGcConst(f,bc.getCd())) );
            f.d_pc++;
            break;
        case BC_TNEW:
            setSlotVal(f, bc.d_a, QVariant::fromValue(TableRef(new Table() ) ) );
            f.d_pc++;
            break;
        case BC_TGETV:
        case BC_TGETS:
        case BC_TGETB:
            if( !doGetT( f, bc ) )
                return false;
            f.d_pc++;
            break;
        case BC_TSETV:
        case BC_TSETS:
        case BC_TSETB:
            if( !doSetT( f, bc ) )
                return false;
            f.d_pc++;
            break;
        case BC_TDUP:
            {
                TableRef t(new Table() );
                const QVariant v = getGcConst( f, bc.getCd() );
                if( !v.canConvert<JitBytecode::ConstTable>() )
                    return error2(f,"");
                t.deref()->d_hash = v.value<JitBytecode::ConstTable>().merged();
                setSlotVal(f, bc.d_a, QVariant::fromValue( t ) );
                f.d_pc++;
            }
            break;
            // TODO Pending: TSETM
            // Operand D of TSETM points to a biased floating-point number in the constant table. Only the
            // lowest 32 bits from the mantissa are used as a starting table index. MULTRES from the previous
            // bytecode gives the number of table slots to fill.
            //         if op == "TSETM " then kc = kc - 2^52 end


        // Upvalue and Function ops (fully implemented) **********************************
        case BC_UGET:
            setSlotVal(f, bc.d_a, getUpvalue( f, bc.getCd() ) );
            f.d_pc++;
            break;
        case BC_USETV:
            setUpvalue( f, bc.d_a, getSlotVal( f, bc.getCd() ) );
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
                setSlotVal(f, bc.d_a, QVariant::fromValue( cc ) );
                f.d_pc++;
            }
            break;

        // Calls and Vararg Handling **********************************
        case BC_CALL:
            {
                const QVariant vc = getSlotVal( f, bc.d_a );
                QVariantList inout;
                for( int i = bc.d_a + 1; i <= ( bc.d_a + bc.getCd() - 1 ); i++ )
                    inout.append( getSlotVal(f,i) );
                const int res = doCall( f, vc, inout );
                if( res < 0 )
                    return false;
                for( int i = bc.d_a; i <= ( bc.d_a + bc.d_b - 2 ) && i < inout.size() && i < res; i++ )
                    setSlotVal( f, i, inout[i] );
                f.d_pc++;
            }
            break;
            // TODO Pending: CALLM, CALLMT, CALLT, ITERC, ITERN, VARG, ISNEXT

        // Bad exit **********************************
        default:
            return error2( f, tr("opcode not yet supported: %1").arg(bc.d_name));
        }
    }
    return true;
}

void JitEngine::Frame::dump()
{
    for( int i = 0; i < d_slots.size(); i++ )
        qDebug() << "Slot" << i << "=" << tostring(d_slots[i]->d_val);
}


void JitEngine::Table::allocateUserData(quint32 size)
{
    if( d_userData == 0 )
        d_userData = ::malloc(size);
}
