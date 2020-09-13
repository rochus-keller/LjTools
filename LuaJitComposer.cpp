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

#include "LuaJitComposer.h"
#include <QtDebug>
#include <QFile>
#include <QBitArray>
using namespace Lua;

JitComposer::JitComposer(QObject *parent) : QObject(parent),d_hasDebugInfo(false),d_stripped(false),d_useRowColFormat(true)
{

}

void JitComposer::clear()
{
    d_bc.clear();
    d_hasDebugInfo = false;
}

int JitComposer::openFunction(quint8 parCount, const QByteArray& sourceRef, quint32 firstLine, quint32 lastLine)
{
    if( d_bc.d_funcs.isEmpty() )
        d_bc.d_name = sourceRef;
    JitBytecode::FuncRef f(new Func());
    if( d_bc.d_fstack.isEmpty() )
    {
        if( firstLine == 0 || lastLine == 0 )
            d_hasDebugInfo = false;
        else
            d_hasDebugInfo = true;
    }else
    {
        if( firstLine == 0 || lastLine == 0 )
        {
            if( d_hasDebugInfo )
                qWarning() << "JitComposer::openFunction: expecting debug information";
        }else
        {
            if( !d_hasDebugInfo )
                qWarning() << "JitComposer::openFunction: not expecting debug information";
        }
    }
    f->d_sourceFile = sourceRef;
    if( d_hasDebugInfo )
    {
        if( isPacked(firstLine) )
        {
            Q_ASSERT( isPacked(lastLine) );
            if( !d_useRowColFormat )
            {
                firstLine = unpackRow(firstLine);
                lastLine = unpackRow(lastLine);
            }
        }

        f->d_firstline = firstLine;
        f->d_numline = lastLine - firstLine + 1; // NOTE: intentionally not unpack so that the larger numline determines larger word length
    }else
    {
        f->d_firstline = 0;
        f->d_numline = 1;
    }
    f->d_numparams = parCount;
    const int slot = getConstSlot(QVariant::fromValue(f));
    d_bc.d_fstack.push_back( f );
    d_bc.d_funcs.append(f);
    return slot;
}

static inline bool isRET( quint8 op )
{
    return op == JitBytecode::OP_RETM ||
            op == JitBytecode::OP_RET ||
            op == JitBytecode::OP_RET0 ||
            op == JitBytecode::OP_RET1 ||
            op == JitBytecode::OP_CALLT;
}

bool JitComposer::closeFunction(quint8 frameSize)
{
    Q_ASSERT( !d_bc.d_fstack.isEmpty() );
    Func* f = static_cast<Func*>(d_bc.d_fstack.back().data());
    f->d_framesize = frameSize;

    QHash<QVariant,int>::const_iterator n;
    f->d_constNums.resize(f->d_numConst.size());
    for( n = f->d_numConst.begin(); n != f->d_numConst.end(); ++n )
        f->d_constNums[n.value()] = n.key();
    f->d_constObjs.resize(f->d_gcConst.size());
    for( n = f->d_gcConst.begin(); n != f->d_gcConst.end(); ++n )
        f->d_constObjs[ f->d_gcConst.size() - n.value() - 1] = n.key(); // reverse

    const bool hasReturn = !d_bc.d_fstack.back()->d_byteCodes.isEmpty() &&
            isRET(JitBytecode::opFromBc(d_bc.d_fstack.back()->d_byteCodes.last()));
    if( !hasReturn )
        qWarning() << "JitComposer::closeFunction: last statement is neither RET nor CALLT in" <<
                      d_bc.d_name << "function no." << d_bc.d_fstack.back()->d_id;
    d_bc.d_fstack.pop_back();
    return hasReturn;
}

bool JitComposer::addOpImp(JitBytecode::Op op, quint8 a, quint8 b, quint16 cd, quint32 line)
{
    if( d_bc.d_fstack.isEmpty() )
        return false;

    quint32 bc = op;
    bc |= a << 8;
    if( JitBytecode::formatFromOp(op) == JitBytecode::ABC )
    {
        bc |= b << 24;
        if( cd > 255 )
            return false;
        bc |= cd << 16;
    }else
    {
        bc |= cd << 16;
    }
    if( line > 0 )
    {
        if( d_hasDebugInfo )
        {
            if( !d_useRowColFormat && isPacked(line) )
                line = unpackRow(line);
            d_bc.d_fstack.back()->d_lines.append(line);
        }else
            qWarning() << "JitComposer::addOpImp: not expecting line number";
    }else if( d_hasDebugInfo )
    {
        qWarning() << "JitComposer::addOpImp: expecting line number";
        d_bc.d_fstack.back()->d_lines.append(0);
    }
    d_bc.d_fstack.back()->d_byteCodes.append(bc);
    return true;
}

bool JitComposer::addAbc(JitBytecode::Op op, quint8 a, quint8 b, quint8 c, quint32 line)
{
    return addOpImp( op, a, b, c, line );
}

bool JitComposer::addAd(JitBytecode::Op op, quint8 a, quint16 d, quint32 line)
{
    return addOpImp( op, a, 0, d, line );
}

int JitComposer::getCurPc() const
{
    if( d_bc.d_fstack.isEmpty() )
        return -1;
    return d_bc.d_fstack.back()->d_byteCodes.size() - 1;
}

bool JitComposer::patch(quint32 pc, qint16 off)
{
    if( pc >= d_bc.d_fstack.back()->d_byteCodes.size() )
        return false;

    quint32& bc = d_bc.d_fstack.back()->d_byteCodes[pc];
    switch( JitBytecode::opFromBc(bc) )
    {
    case JitBytecode::OP_FORI:
    case JitBytecode::OP_FORL:
    case JitBytecode::OP_JMP:
    case JitBytecode::OP_LOOP:
    case JitBytecode::OP_UCLO:
        bc &= 0x0000ffff;
        bc |= quint16( off + JitBytecode::Instruction::JumpBias ) << 16;
        return true;
    default:
        break;
    }
    return false;
}

bool JitComposer::ADD(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line)
{
    if( JitBytecode::isNumber(lhs) )
        return addAbc(JitBytecode::OP_ADDNV, dst, rhs, getConstSlot(lhs), line );
    else
        return false;
}

bool JitComposer::ADD(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line)
{
    if( JitBytecode::isNumber(rhs) )
        return addAbc(JitBytecode::OP_ADDVN, dst, lhs, getConstSlot(rhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::ADD(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAbc(JitBytecode::OP_ADDVV, dst, lhs, rhs, line );
}

bool JitComposer::SUB(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line)
{
    if( JitBytecode::isNumber(lhs) )
        return addAbc(JitBytecode::OP_SUBNV, dst, rhs, getConstSlot(lhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::SUB(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line)
{
    if( JitBytecode::isNumber(rhs) )
        return addAbc(JitBytecode::OP_SUBVN, dst, lhs, getConstSlot(rhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::SUB(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAbc(JitBytecode::OP_SUBVV, dst, lhs, rhs, line );
}

bool JitComposer::TDUP(SlotNr dst, const QVariant& constTable, quint32 line )
{
    if( constTable.canConvert<JitBytecode::ConstTable>() )
        return addAd(JitBytecode::OP_TDUP, dst, getConstSlot(constTable), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::TGET(SlotNr to, SlotNr table, quint8 index, quint32 line)
{
    return addAbc(JitBytecode::OP_TGETV, to, table, index, line );
}

bool JitComposer::TGET(SlotNr to, SlotNr table, const QByteArray& name, quint32 line)
{
    return addAbc(JitBytecode::OP_TGETS, to, table, getConstSlot(name), line );
}

bool JitComposer::TGETi(SlotNr to, SlotNr table, quint8 index, quint32 line)
{
    return addAbc(JitBytecode::OP_TGETB, to, table, index, line );
}

bool JitComposer::MUL(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line)
{
    if( JitBytecode::isNumber(lhs) )
        return addAbc(JitBytecode::OP_MULNV, dst, rhs, getConstSlot(lhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::MUL(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line)
{
    if( JitBytecode::isNumber(rhs) )
        return addAbc(JitBytecode::OP_MULVN, dst, lhs, getConstSlot(rhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::MUL(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAbc(JitBytecode::OP_MULVV, dst, lhs, rhs, line );
}

bool JitComposer::DIV(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line)
{
    if( JitBytecode::isNumber(lhs) )
        return addAbc(JitBytecode::OP_DIVNV, dst, rhs, getConstSlot(lhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::DIV(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line)
{
    if( JitBytecode::isNumber(rhs) )
        return addAbc(JitBytecode::OP_DIVVN, dst, lhs, getConstSlot(rhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::DIV(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAbc(JitBytecode::OP_DIVVV, dst, lhs, rhs, line );
}

bool JitComposer::FNEW(SlotNr dst, quint16 func, quint32 line)
{
    return addAd(JitBytecode::OP_FNEW, dst, func, line );
}

bool JitComposer::FORI(SlotNr base, Jump offset, quint32 line)
{
    return addAd(JitBytecode::OP_FORI, base, quint16( offset + JitBytecode::Instruction::JumpBias ), line );
}

bool JitComposer::FORL(SlotNr base, Jump offset, quint32 line)
{
    return addAd(JitBytecode::OP_FORL, base, quint16( offset + JitBytecode::Instruction::JumpBias ), line );
}

bool JitComposer::MOD(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line)
{
    if( JitBytecode::isNumber(lhs) )
        return addAbc(JitBytecode::OP_MODNV, dst, rhs, getConstSlot(lhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::MOD(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line)
{
    if( JitBytecode::isNumber(rhs) )
        return addAbc(JitBytecode::OP_MODVN, dst, lhs, getConstSlot(rhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::MOD(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAbc(JitBytecode::OP_MODVV, dst, lhs, rhs, line );
}

bool JitComposer::CALL(SlotNr slot, quint8 numOfReturns, quint8 numOfArgs, quint32 line)
{
    // Operand C is one plus the number of fixed arguments.
    // Operand B is one plus the number of return values (MULTRES is not supported)
    return addAbc(JitBytecode::OP_CALL, slot, numOfReturns + 1, numOfArgs + 1, line );
}

bool JitComposer::CALLT(SlotNr slot, quint8 numOfArgs, quint32 line)
{
    return addAd(JitBytecode::OP_CALLT, slot, numOfArgs + 1, line );
}

bool JitComposer::CAT(SlotNr dst, SlotNr from, SlotNr to, quint32 line)
{
    return addAbc(JitBytecode::OP_CAT, dst, from, to, line );
}

bool JitComposer::KSET(SlotNr dst, const QVariant& v, quint32 line )
{
    if( JitBytecode::isString( v ) )
        return addAd(JitBytecode::OP_KSTR, dst, getConstSlot(v), line );
    else if( JitBytecode::isPrimitive( v ) )
        return addAd(JitBytecode::OP_KPRI, dst, JitBytecode::toPrimitive(v), line );
    else if( JitBytecode::isNumber( v ) )
    {
        if( v.type() == QVariant::Double )
            return addAd(JitBytecode::OP_KNUM, dst, getConstSlot(v), line );
        else
        {
            if( v.toInt() >= SHRT_MIN && v.toInt() <= SHRT_MAX )
                return addAd(JitBytecode::OP_KSHORT, dst, (quint16)v.toInt(), line );
            else
                return addAd(JitBytecode::OP_KNUM, dst, getConstSlot(v), line );
        }
    }else if( v.canConvert<JitBytecode::ConstTable>() )
        return addAd(JitBytecode::OP_KCDATA, dst, getConstSlot(v), line );

    Q_ASSERT( false );
    return false;
}

bool JitComposer::LEN(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_LEN, lhs, rhs, line );
}

bool JitComposer::LOOP(SlotNr base, Jump offset, quint32 line)
{
    return addAd(JitBytecode::OP_LOOP, base, (quint16)offset, line );
}

bool JitComposer::MOV(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_MOV, lhs, rhs, line );
}

bool JitComposer::NOT(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_NOT, lhs, rhs, line );
}

bool JitComposer::POW(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_POW, lhs, rhs, line );
}

bool JitComposer::RET(SlotNr slot, quint8 len, quint32 line)
{
    // Operand D is one plus the number of results to return.
    if( len == 1 )
        return addAd(JitBytecode::OP_RET1, slot, 2, line );
    else
        return addAd(JitBytecode::OP_RET, slot, len + 1, line );
}

bool JitComposer::RET(quint32 line)
{
    return addAd(JitBytecode::OP_RET0, 0, 1, line );
}

bool JitComposer::TNEW(SlotNr slot, quint16 arrSize, quint8 hashSize, quint32 line)
{
    return addAd(JitBytecode::OP_TNEW, slot, arrSize + ( hashSize << 11 ), line );
}

bool JitComposer::TSET(SlotNr value, SlotNr table, quint8 index, quint32 line)
{
    return addAbc(JitBytecode::OP_TSETV, value, table, index, line );
}

bool JitComposer::TSETi(JitComposer::SlotNr value, JitComposer::SlotNr table, quint8 index, quint32 line)
{
    return addAbc(JitBytecode::OP_TSETB, value, table, index, line );
}

bool JitComposer::TSET(JitComposer::SlotNr value, JitComposer::SlotNr table, const QByteArray& index, quint32 line)
{
    return addAbc(JitBytecode::OP_TSETS, value, table, getConstSlot(index), line );
}

bool JitComposer::UCLO(SlotNr slot, Jump offset, quint32 line)
{
    return addAd(JitBytecode::OP_UCLO, slot, quint16(offset+JitBytecode::Instruction::JumpBias), line );
}

bool JitComposer::UGET(SlotNr toSlot, UvNr fromUv, quint32 line)
{
    return addAd(JitBytecode::OP_UGET, toSlot, fromUv, line );
}

bool JitComposer::USET(UvNr toUv, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_USETV, toUv, rhs, line );
}

bool JitComposer::USET(UvNr toUv, const QVariant& rhs, quint32 line)
{
    if( JitBytecode::isString(rhs) )
        return addAd(JitBytecode::OP_USETS, toUv, getConstSlot(rhs), line );
    else if( JitBytecode::isNumber(rhs) )
        return addAd(JitBytecode::OP_USETN, toUv, getConstSlot(rhs), line );
    else if( JitBytecode::isPrimitive(rhs) )
        return addAd(JitBytecode::OP_USETP, toUv, JitBytecode::toPrimitive(rhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::UNM(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_UNM, lhs, rhs, line );
}

bool JitComposer::GGET(SlotNr to, const QByteArray& name, quint32 line)
{
    return addAd(JitBytecode::OP_GGET, to, getConstSlot(name), line );
}

bool JitComposer::GSET(SlotNr value, const QByteArray& name, quint32 line)
{
    return addAd(JitBytecode::OP_GSET, value, getConstSlot(QVariant::fromValue(name)), line );
}

bool JitComposer::ISGE(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_ISGE, lhs, rhs, line );
}

bool JitComposer::ISGT(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_ISGT, lhs, rhs, line );
}

bool JitComposer::ISLE(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_ISLE, lhs, rhs, line );
}

bool JitComposer::ISLT(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_ISLT, lhs, rhs, line );
}

bool JitComposer::ISEQ(SlotNr lhs, const QVariant& rhs, quint32 line)
{
    if( JitBytecode::isNumber(rhs) )
        return addAd(JitBytecode::OP_ISEQN, lhs, getConstSlot(rhs), line );
    else if( JitBytecode::isString(rhs) )
        return addAd(JitBytecode::OP_ISEQS, lhs, getConstSlot(rhs), line );
    else if( JitBytecode::isPrimitive(rhs) )
        return addAd(JitBytecode::OP_ISEQP, lhs, JitBytecode::toPrimitive(rhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::ISEQ(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_ISEQV, lhs, rhs, line );
}

bool JitComposer::ISNE(SlotNr lhs, const QVariant& rhs, quint32 line)
{
    if( JitBytecode::isNumber(rhs) )
        return addAd(JitBytecode::OP_ISNEN, lhs, getConstSlot(rhs), line );
    else if( JitBytecode::isString(rhs) )
        return addAd(JitBytecode::OP_ISNES, lhs, getConstSlot(rhs), line );
    else if( JitBytecode::isPrimitive(rhs) )
        return addAd(JitBytecode::OP_ISNEP, lhs, JitBytecode::toPrimitive(rhs), line );
    Q_ASSERT( false );
    return false;
}

bool JitComposer::ISNE(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_ISNEV, lhs, rhs, line );
}

bool JitComposer::ISF(SlotNr slot, quint32 line)
{
    return addAd(JitBytecode::OP_ISF, 0, slot, line );
}

bool JitComposer::ISFC(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_ISFC, lhs, rhs, line );
}

bool JitComposer::IST(SlotNr slot, quint32 line)
{
    return addAd(JitBytecode::OP_IST, 0, slot, line );
}

bool JitComposer::ISTC(SlotNr lhs, SlotNr rhs, quint32 line)
{
    return addAd(JitBytecode::OP_ISTC, lhs, rhs, line );
}

bool JitComposer::JMP(SlotNr base, Jump offset, quint32 line)
{
    return addAd(JitBytecode::OP_JMP, base, quint16(offset + JitBytecode::Instruction::JumpBias), line );
}

bool JitComposer::KNIL(SlotNr base, quint8 len, quint32 line )
{
    return addAd(JitBytecode::OP_KNIL, base, base + len - 1, line );
}

void JitComposer::setUpvals(const JitComposer::UpvalList& l)
{
    if( d_bc.d_fstack.isEmpty() )
        return;
    if( !d_hasDebugInfo )
        qWarning() << "JitComposer::setUpvals: not expecting debug info";
    Func* f = static_cast<Func*>( d_bc.d_fstack.back().data() );
    foreach( const Upval& uv, l )
    {
        quint16 tmp = uv.d_uv;
        if( uv.d_isLocal )
            tmp |= JitBytecode::Function::UvLocalMask;
        if( uv.d_isRo )
            tmp |= JitBytecode::Function::UvImmutableMask;
        f->d_upvals << tmp;
        if( !uv.d_name.isEmpty() )
            f->d_upNames << uv.d_name;
    }
}

void JitComposer::setVarNames(const JitComposer::VarNameList& l)
{
    if( d_bc.d_fstack.isEmpty() )
        return;
    if( !d_hasDebugInfo )
        qWarning() << "JitComposer::setUpvals: not expecting debug info";
    Func* f = static_cast<Func*>( d_bc.d_fstack.back().data() );
    foreach( const VarName& vn, l )
    {
        JitBytecode::Function::Var v;
        v.d_startpc = vn.d_from + 0;
        v.d_endpc = vn.d_to + 2;    // see JitBytecode::Function::findVar why +2
        v.d_name = vn.d_name;
        f->d_vars.append(v);
    }
}

int JitComposer::getLocalSlot(const QByteArray& name)
{
    // Obsolete?

    if( d_bc.d_fstack.isEmpty() )
        return -1;
    int slot;
    Func* f = static_cast<Func*>( d_bc.d_fstack.back().data() );
    if( !f->d_var.contains(name) )
    {
        slot = f->d_var.size();
        f->d_var[name] = slot;
    }else
        slot = f->d_var[name];
    return slot;
}

int JitComposer::getConstSlot(const QVariant& v)
{
    if( d_bc.d_fstack.isEmpty() )
        return -1;

    Func* f = static_cast<Func*>( d_bc.d_fstack.back().data() );
    QHash<QVariant,int>* p = 0;
    if( JitBytecode::isNumber(v) )
        p = &f->d_numConst;
    else
        p = &f->d_gcConst;
    int slot;
    if( !p->contains(v) )
    {
        slot = p->size();
        p->insert(v,slot);
    }else
        slot = p->value(v);
    return slot;
}

bool JitComposer::write(QIODevice* out, const QString& path)
{
    if( d_bc.d_funcs.isEmpty() )
        return false;
    if( d_bc.d_fstack.isEmpty() )
        d_bc.d_fstack.push_back( d_bc.d_funcs.first() );
    d_bc.setStripped( d_stripped || !d_hasDebugInfo );
    return d_bc.write(out,path);
}

bool JitComposer::write(const QString& file)
{
    if( d_bc.d_funcs.isEmpty() )
        return false;
    if( d_bc.d_fstack.isEmpty() )
        d_bc.d_fstack.push_back( d_bc.d_funcs.first() );
    d_bc.setStripped( d_stripped || !d_hasDebugInfo );
    return d_bc.write(file);
}

void JitComposer::setStripped(bool on)
{
    d_stripped = on;
}

void JitComposer::setUseRowColFormat(bool on)
{
    d_useRowColFormat = on;
}

static bool sortIntervals( const JitComposer::Interval& lhs, const JitComposer::Interval& rhs )
{
    return lhs.d_from < rhs.d_from;
}

static int checkFree( const JitComposer::SlotPool& pool, int slot, int len )
{
    if( slot + len >= pool.d_slots.size() )
        return 0;
    for( int i = 0; i < len ; i++ )
    {
        if( pool.d_slots.test(slot+i) )
            return i;
    }
    return len;
}

static inline void fill( JitComposer::SlotPool& pool, bool val, int from, int to )
{
    // Sets bits at index positions begin up to (but not including) end to value.
    for( int i = from; i < to; i++ )
        pool.d_slots.set(i,val);
}

static inline void setFrameSize(JitComposer::SlotPool& pool, int slot, int len )
{
    if( slot < 0 )
        return;
    const int max = slot + len;
    if( max > int(pool.d_frameSize) )
        pool.d_frameSize = max;
}

static inline void setCallArgs(JitComposer::SlotPool& pool, int slot, bool callArgs )
{
    // in case of a call take care that slots used to evaluate the arguments are higher than
    // the allocated call base; otherwise it could happen that an argument executes yet another
    // call to which a base lower than the waiting one is allocated resulting in random modifications
    // of the waiting call base;
    // we need a stack (and not simply a scalar) because calls can be nested when the arguments
    // of call A must be calculated by call B, so the stack contains slot(A) and slots(B)

    if( slot < 0 || !callArgs )
        return;
    pool.d_callArgs.push_back(slot);
}

int JitComposer::nextFreeSlot(SlotPool& pool, int len, bool callArgs )
{
    int slot = 0;
    if( !pool.d_callArgs.isEmpty() )
        slot = pool.d_callArgs.back();
    while( true )
    {
        // skip used
        while( slot < pool.d_slots.size() && pool.d_slots.test(slot) )
            slot++;
        if( slot < pool.d_slots.size() )
        {
            Q_ASSERT( !pool.d_slots.test(slot) );
            if( len == 1 )
            {
                pool.d_slots.set(slot);
                setFrameSize(pool,slot,len);
                setCallArgs(pool,slot,callArgs);
                return slot;
            } // else
            const int free = checkFree( pool, slot, len );
            if( free == len )
            {
                fill(pool,true,slot,slot+len);
                setFrameSize(pool,slot,len);
                setCallArgs(pool,slot,callArgs);
                return slot;
            } // else
            slot += free;
        }
    }
    return -1;
}

bool JitComposer::releaseSlot(JitComposer::SlotPool& pool, quint8 slot, int len)
{
    for( int i = slot; i < slot + len; i++ )
    {
        if( !pool.d_slots.test(i) )
            return false;
        pool.d_slots.set(i,false);
    }
    pool.d_callArgs.removeOne(slot);
    return true;
}

int JitComposer::highestUsedSlot(const JitComposer::SlotPool& pool)
{
    for( int i = pool.d_slots.size() - 1; i >= 0; i-- )
        if( pool.d_slots.test(i) )
            return i;
    return -1;
}

quint32 JitComposer::packRowCol(quint32 row, quint32 col)
{
    static const quint32 maxRow = ( 1 << ROW_BIT_LEN ) - 1;
    static const quint32 maxCol = ( 1 << COL_BIT_LEN ) - 1;
    Q_ASSERT( row <= maxRow && col <= maxCol );
    return ( row << COL_BIT_LEN ) | col | MSB;
}

bool JitComposer::allocateWithLinearScan(SlotPool& pool, JitComposer::Intervals& vars, int len)
{
    // according to Poletto & Sarkar (1999): Linear scan register allocation, ACM TOPLAS, Volume 21 Issue 5

    std::sort( vars.begin(), vars.end(), sortIntervals );

    typedef QMultiMap<quint32,quint32> Active; // to -> Interval
    Active active;

    for( int i = 0; i < vars.size(); i++ )
    {
        Active::iterator j = active.begin();
        while( j != active.end() )
        {
            // ExpireOldIntervals(i)
            if( vars[j.value()].d_to >= vars[i].d_from )
            {
                break;
            }
            pool.d_slots.set(vars[j.value()].d_slot,false); // why not releaseSlot?
            fill(pool,false, vars[j.value()].d_slot, vars[j.value()].d_slot + len );
            j = active.erase(j);
        }
        const int slot = nextFreeSlot(pool,len);
        if( active.size() >= MAX_SLOTS || slot < 0 )
            return false;
        vars[i].d_slot = slot;
        active.insert(vars[i].d_to, i);
    }
    return true;
}
