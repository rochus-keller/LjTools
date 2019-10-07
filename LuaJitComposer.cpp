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
using namespace Lua;

JitComposer::JitComposer(QObject *parent) : QObject(parent)
{

}

void JitComposer::clear()
{
    d_funcStack.clear();
}

bool JitComposer::openFunction(quint8 parCount, const QByteArray& sourceRef, int firstLine, int lastLine)
{
    FuncRef f(new Function());
    f->d_sourceRef = sourceRef;
    f->d_firstLine = firstLine;
    f->d_lastLine = lastLine;
    f->d_numOfParams = parCount;
    if( !d_funcStack.empty() )
        d_funcStack.back()->d_gcConst.insert( QVariant::fromValue(f),d_funcStack.back()->d_gcConst.size());
    else
        d_top = f;
    d_funcStack.push_back( f );
    return true;
}

bool JitComposer::closeFunction()
{
    d_funcStack.pop_back();
    return true;
}

bool JitComposer::addOp(JitBytecode::Op op, quint8 a, quint8 b, int cd, int line)
{
    if( d_funcStack.isEmpty() )
        return false;

    quint32 bc = op;
    bc |= a << 8;
    if( JitBytecode::formatFromOp(op) == JitBytecode::ABC )
    {
        bc |= b << 24;
        if( cd < 0 || cd > 255 )
            return false;
        bc |= cd << 16;
    }else
    {
        const JitBytecode::Instruction::FieldType t = JitBytecode::typeCdFromOp(op);
        if( t == JitBytecode::Instruction::_lits || JitBytecode::Instruction::_jump )
        {
            if( cd < SHRT_MIN || cd > SHRT_MAX )
                return false;
        }else
        {
            if( cd < 0 || cd > USHRT_MAX )
                return false;
        }
        bc |= cd << 16;
    }
    d_funcStack.back()->d_byteCodes.append(bc);
    if( line > 0 )
        d_funcStack.back()->d_lines.append(line);
    return true;
}

bool JitComposer::addOp(JitBytecode::Op op, quint8 a, int d, int line)
{
    return addOp( op, a, 0, d, line );
}

#if 0
bool JitComposer::addAD(JitBytecode::Op op, quint8 a, quint32 d, int line)
{
    if( d_funcStack.isEmpty() )
        return false;
    quint16 id = 0;
    if( !d_funcStack.back()->d_numConst.contains(d) )
    {
        id = d_funcStack.size();
        d_funcStack.back()->d_numConst[d] = id;
    }else
        id = d_funcStack.back()->d_numConst[d];
    return addOp(op, a, 0, id, line );
}

bool JitComposer::addAD(JitBytecode::Op op, quint8 a, double d, int line)
{
    if( d_funcStack.isEmpty() )
        return false;
    quint16 id = 0;
    if( !d_funcStack.back()->d_numConst.contains(d) )
    {
        id = d_funcStack.size();
        d_funcStack.back()->d_numConst[d] = id;
    }else
        id = d_funcStack.back()->d_numConst[d];
    return addOp(op, a, 0, id, line );
}

bool JitComposer::addAD(JitBytecode::Op op, quint8 a, const QByteArray& d, int line)
{
    if( d_funcStack.isEmpty() )
        return false;
    quint16 id = 0;
    if( !d_funcStack.back()->d_gcConst.contains(d) )
    {
        id = d_funcStack.size();
        d_funcStack.back()->d_gcConst[d] = id;
    }else
        id = d_funcStack.back()->d_gcConst[d];
    return addOp(op, a, 0, id, line );
}
#endif

int JitComposer::getLocalSlot(const QByteArray& name)
{
    if( d_funcStack.isEmpty() )
        return -1;
    int slot;
    if( !d_funcStack.back()->d_var.contains(name) )
    {
        slot = d_funcStack.back()->d_var.size();
        d_funcStack.back()->d_var[name] = slot;
    }else
        slot = d_funcStack.back()->d_var[name];
    return slot;
}

int JitComposer::getConstSlot(const QVariant& v)
{
    if( d_funcStack.isEmpty() )
        return -1;

    QHash<QVariant,int>* p = 0;
    if( JitBytecode::isNumber(v) )
        p = &d_funcStack.back()->d_numConst;
    else
        p = &d_funcStack.back()->d_gcConst;
    int slot;
    if( !p->contains(v) )
    {
        slot = p->size();
        p->insert(v,slot);
    }else
        slot = p->value(v);
    return slot;
}

