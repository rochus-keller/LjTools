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
using namespace Lua;

JitComposer::JitComposer(QObject *parent) : QObject(parent)
{

}

void JitComposer::clear()
{
    d_bc.clear();
}

int JitComposer::openFunction(quint8 parCount, const QByteArray& sourceRef, int firstLine, int lastLine)
{
    if( d_bc.d_funcs.isEmpty() )
        d_bc.d_name = sourceRef;
    JitBytecode::FuncRef f(new Func());
    f->d_sourceFile = sourceRef;
    f->d_firstline = firstLine;
    f->d_numline = lastLine - firstLine + 1;
    f->d_numparams = parCount;
    const int slot = getConstSlot(QVariant::fromValue(f));
    d_bc.d_fstack.push_back( f );
    d_bc.d_funcs.append(f);
    return slot;
}

bool JitComposer::closeFunction(quint8 frameSize)
{
    Func* f = static_cast<Func*>(d_bc.d_fstack.back().data());
    f->d_framesize = frameSize;

    QHash<QVariant,int>::const_iterator n;
    f->d_constNums.resize(f->d_numConst.size());
    for( n = f->d_numConst.begin(); n != f->d_numConst.end(); ++n )
        f->d_constNums[n.value()] = n.key();
    f->d_constObjs.resize(f->d_gcConst.size());
    for( n = f->d_gcConst.begin(); n != f->d_gcConst.end(); ++n )
        f->d_constObjs[ f->d_gcConst.size() - n.value() - 1] = n.key(); // reverse

    d_bc.d_fstack.pop_back();
    return true;
}

bool JitComposer::addOpImp(JitBytecode::Op op, quint8 a, quint8 b, quint16 cd, int line)
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
    d_bc.d_fstack.back()->d_byteCodes.append(bc);
    if( line > 0 )
        d_bc.d_fstack.back()->d_lines.append(line);
    return true;
}

bool JitComposer::addAbc(JitBytecode::Op op, quint8 a, quint8 b, quint8 c, int line)
{
    return addOpImp( op, a, b, c, line );
}

bool JitComposer::addAd(JitBytecode::Op op, quint8 a, quint16 d, int line)
{
    return addOpImp( op, a, 0, d, line );
}

void JitComposer::setUpvals(const JitComposer::UpvalList& l)
{
    if( d_bc.d_fstack.isEmpty() )
        return;
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
    Func* f = static_cast<Func*>( d_bc.d_fstack.back().data() );
    foreach( const VarName& vn, l )
    {
        JitBytecode::Function::Var v;
        v.d_startpc = vn.d_from + 2; // see JitBytecode::Function::findVar why +2
        v.d_endpc = vn.d_to + 2;
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
    return d_bc.write(out,path);
}

bool JitComposer::write(const QString& file)
{
    if( d_bc.d_funcs.isEmpty() )
        return false;
    if( d_bc.d_fstack.isEmpty() )
        d_bc.d_fstack.push_back( d_bc.d_funcs.first() );
    return d_bc.write(file);
}

