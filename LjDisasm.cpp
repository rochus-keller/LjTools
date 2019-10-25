/*
* Copyright 2019 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the LjAsm parser library.
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

#include "LjDisasm.h"
#include "LuaJitBytecode.h"
#include <QtDebug>
#include <QSet>
#include <QTextStream>
using namespace Ljas;
using namespace Lua;


bool Disasm::disassemble(const JitBytecode& bc, QIODevice* f, const QString& path)
{
    QTextStream out(f);
    out.setCodec("UTF-8");
    out << "-- disassembled from ";
    if( path.isEmpty() )
        out << "Lua source";
    else
        out << path;
    out << endl << endl;
    if( !writeFunc( out, bc.getRoot(), 0 ) )
        return false;
    return true;
}

static inline QByteArray ws(int level)
{
    return QByteArray(level,'\t');
}

bool Disasm::writeFunc(QTextStream& out, const JitBytecode::Function* f, int level)
{
    if( f == 0 )
        return false;

    out << ws(level) << "function F" << f->d_id << "(";
    for( int i = 0; i < f->d_numparams; i++ )
    {
        if( i < f->d_vars.size() )
        {
            if( i != 0 )
                out << " ";
            out << f->d_vars[i].d_name;
        }else
        {
            if( i != 0 )
                out << " ";
            out << "__p" << i;
        }
    }
    out << ") ";
    if( !f->isStripped() )
        out << "\t-- lines "<< f->d_firstline << " to " << f->d_firstline + f->d_numline;
    out << endl;

    QHash<QByteArray,int> unique;
    SlotMap reverse;
    if( f->d_framesize > f->d_numparams || f->d_framesize && f->d_vars.isEmpty() )
    {
        QByteArray buf;
        buf = ws(level+1);
        buf += "var ";
        if( f->d_vars.size() > f->d_numparams )
        {
            buf += "{ ";
            for( int i = f->d_numparams; i < f->d_vars.size(); i++ )
            {
                QByteArray name = f->d_vars[i].d_name;
                if( name.startsWith('(') )
                {
                    //qDebug() << "internal var" << name;
                    name = "__t" + QByteArray::number(i);
                }

                if( unique.contains(name) )
                {
                    //qDebug() << "double ident" << name;
                    name += "_" + QByteArray::number(i);
                }
                unique.insert(name,i);
                reverse.insert(i,name);
                buf += name + " ";
                if( buf.size() > 80 )
                {
                    out << buf << endl;
                    buf = ws(level+2);
                }
            }
            buf += " } ";
        }
        out << buf;
        if( f->d_framesize > 0 )
        {
            out << "T[" << f->d_framesize << "]" << endl;
        }
        out << endl;
    }

    for( int i = f->d_constObjs.size() - 1; i >= 0; i-- )
    {
        const QVariant& o = f->d_constObjs[i];
        if( o.canConvert<JitBytecode::FuncRef>() )
            writeFunc(out, o.value<JitBytecode::FuncRef>().constData(), level+1 );
    }

    if( !f->d_byteCodes.isEmpty() )
    {
        out << ws(level) << "begin" << endl;

        QSet<quint32> labels;
        for( int pc = 0; pc < f->d_byteCodes.size(); pc++ )
        {
            const JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f->d_byteCodes[pc]);
            if( bc.d_tcd == JitBytecode::Instruction::_jump && bc.d_op != JitBytecode::OP_LOOP )
                labels.insert( pc + 1 + bc.getCd() );
        }

        for( int pc = 0; pc < f->d_byteCodes.size(); pc++ )
        {
            if( labels.contains(pc) )
                out << ws(level) << "__L" << pc << ":" << endl;
            const JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f->d_byteCodes[pc]);
            out << ws(level+1);
            switch( bc.d_op )
            {
            case JitBytecode::OP_ISEQV:
            case JitBytecode::OP_ISEQS:
            case JitBytecode::OP_ISEQN:
            case JitBytecode::OP_ISEQP:
                out << "ISEQ";
                break;
            case JitBytecode::OP_ISNEV:
            case JitBytecode::OP_ISNES:
            case JitBytecode::OP_ISNEN:
            case JitBytecode::OP_ISNEP:
                out << "ISNE";
                break;
            case JitBytecode::OP_ADDVN:
            case JitBytecode::OP_ADDNV:
            case JitBytecode::OP_ADDVV:
                out << "ADD";
                break;
            case JitBytecode::OP_SUBVN:
            case JitBytecode::OP_SUBNV:
            case JitBytecode::OP_SUBVV:
                out << "SUB";
                break;
            case JitBytecode::OP_MULVN:
            case JitBytecode::OP_MULNV:
            case JitBytecode::OP_MULVV:
                out << "MUL";
                break;
            case JitBytecode::OP_DIVVN:
            case JitBytecode::OP_DIVNV:
            case JitBytecode::OP_DIVVV:
                out << "DIV";
                break;
            case JitBytecode::OP_MODVN:
            case JitBytecode::OP_MODNV:
            case JitBytecode::OP_MODVV:
                out << "MOD";
                break;
            case JitBytecode::OP_KSTR:
            case JitBytecode::OP_KCDATA:
            case JitBytecode::OP_KSHORT:
            case JitBytecode::OP_KNUM:
            case JitBytecode::OP_KPRI:
                out << "KSET";
                break;
            case JitBytecode::OP_USETV:
            case JitBytecode::OP_USETS:
            case JitBytecode::OP_USETN:
            case JitBytecode::OP_USETP:
                out << "USET";
                break;
            case JitBytecode::OP_TGETV:
            case JitBytecode::OP_TGETS:
            case JitBytecode::OP_TGETB:
                out << "TGET";
                break;
            case JitBytecode::OP_TSETV:
            case JitBytecode::OP_TSETS:
            case JitBytecode::OP_TSETB:
                out << "TSET";
                break;
            case JitBytecode::OP_RET0:
            case JitBytecode::OP_RET1:
                out << "RET";
                break;
            default:
                out << bc.d_name;
                break;
            }
            out << " ";

            const QByteArray a = renderArg(reverse,f,bc.d_ta,bc.d_a,pc);
            const QByteArray b = renderArg(reverse,f,bc.d_tb,bc.d_b,pc);
            const QByteArray c = renderArg(reverse,f,bc.d_tcd,bc.getCd(),pc);
            if( bc.d_op == JitBytecode::OP_LOOP )
            {
                // NOP
            }else if( bc.d_op == JitBytecode::OP_JMP )
            {
                out << c;
            }else
            {
                if( !a.isEmpty() )
                    out << " " << a;
                if( !b.isEmpty() )
                    out << " " << b;
                if( !c.isEmpty() )
                    out << " " << c;
            }
            out << endl;
        }

    }

    out << ws(level) << "end F" << f->d_id << endl << endl;
}

static inline QByteArray getPriConst(int i)
{
    switch(i)
    {
    case 1:
        return "false";
    case 2:
        return "true";
    default:
        return "nil";
    }
}

static QByteArray tostring(const QVariant& v)
{
    if( JitBytecode::isString( v ) )
        return "\"" + v.toString().toUtf8() + "\"";
    else if( JitBytecode::isNumber( v ) )
        return QByteArray::number( v.toDouble() );
    else
        return v.toByteArray();
}

QByteArray Disasm::renderArg(const Disasm::SlotMap& sm, const JitBytecode::Function* f, int t, int v, int pc)
{
    switch( t )
    {
    case JitBytecode::Instruction::Unused:
        return QByteArray();
    case JitBytecode::Instruction::_var:
    case JitBytecode::Instruction::_dst:
    case JitBytecode::Instruction::_base:
    case JitBytecode::Instruction::_rbase:
        {
            if( v < f->d_vars.size() && f->d_vars[v].d_startpc >= pc && pc <= f->d_vars[v].d_endpc && sm.contains(v) )
                return sm.value(v);
            else if( v < f->d_numparams && f->isStripped() )
                return "__p" + QByteArray::number(v);
            return "T[" + QByteArray::number(v) + "]";
        }
        break;
    case JitBytecode::Instruction::_str:
        return tostring( f->d_constObjs[ f->d_constObjs.size() - v - 1] );
    case JitBytecode::Instruction::_num:
        return QByteArray::number( f->d_constNums[v].toDouble() );
    case JitBytecode::Instruction::_pri:
        return getPriConst(v);
    case JitBytecode::Instruction::_cdata:
        break; // ??
    case JitBytecode::Instruction::_lit:
    case JitBytecode::Instruction::_lits:
        return QByteArray::number(v);
    case JitBytecode::Instruction::_jump:
        return "__L" + QByteArray::number(pc+1+v);
    case JitBytecode::Instruction::_uv:
        {
            const QPair<quint8, JitBytecode::Function*> up = f->getFuncSlotFromUpval(v);
            if( up.second == 0 )
                return "???";
#if 0
            // TODO: this is an issue; not yet understand how to find this name with certainty; sometimes it works.
            if( f->d_upNames[v] != up.second->d_vars[up.first].d_name )
                qDebug() << "inequal upval name pc" << pc << "F" << f->d_id << f->d_upNames[v]
                         << "F" << up.second->d_id << up.second->d_vars[up.first].d_name;
#endif
            QByteArray quali;
            if( up.second != f )
                quali = "F" + QByteArray::number(up.second->d_id) + ".";
            if( v < f->d_upNames.size() )
                return quali + f->d_upNames[v];
            // if( up.first < up.second->d_vars.size() )
                //return quali + up.second->d_vars[up.first].d_name;
            else
                return quali + "T[" + QByteArray::number(up.first) + "]";
        }
        break;
    case JitBytecode::Instruction::_func:
        {
            JitBytecode::FuncRef fr = f->d_constObjs[ f->d_constObjs.size() - v - 1 ].value<JitBytecode::FuncRef>();
            if( fr.data() != 0 )
                return QString("F%1").arg(fr->d_id).toUtf8();
        }
        break;
    case JitBytecode::Instruction::_tab:
        if( f->d_constObjs[ f->d_constObjs.size() - v - 1 ].canConvert<JitBytecode::ConstTable>() )
        {
            QByteArray str;
            QTextStream out(&str);
            out << "{ ";
            JitBytecode::ConstTable t = f->d_constObjs[ f->d_constObjs.size() - v - 1 ].value<JitBytecode::ConstTable>();
            if( !t.d_array.isEmpty() )
            {
                for( int i = 1; i < t.d_array.size(); i++ ) // index starts with one, zero is empty
                {
                    if( i != 1 )
                        out << " ";
                    out << tostring( t.d_array[i] );
                }
            }
            if( !t.d_hash.isEmpty() )
            {
                QHash<QVariant,QVariant>::const_iterator i;
                int n = 0;
                for( i = t.d_hash.begin(); i != t.d_hash.end(); ++i, n++ )
                {
                    if( n != 0 )
                        out << " ";
                    out << tostring( i.key() ) << " = " << tostring( i.value() );
                }
            }
            out << " }";
            out.flush();
            return str;
        }
        return "???";
    }
    return QByteArray();
}

