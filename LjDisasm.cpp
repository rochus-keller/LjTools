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
#include <QFile>
using namespace Ljas;
using namespace Lua;


bool Disasm::disassemble(const JitBytecode& bc, QIODevice* f, const QString& path, bool stripped)
{
    QTextStream out(f);
    out.setCodec("UTF-8");
    out << "-- disassembled from ";
    if( path.isEmpty() )
        out << "Lua source";
    else
        out << path;
    out << endl << endl;
    if( !writeFunc( out, bc.getRoot(), stripped, 0 ) )
        return false;
    return true;
}

static inline QByteArray ws(int level)
{
    return QByteArray(level,'\t');
}

static inline QByteArray varName( const QByteArray& name, int v )
{
    if( name.startsWith('(') )
    {
        return "R" + QByteArray::number(v);
    }else
        return name;
}

bool Disasm::writeFunc(QTextStream& out, const JitBytecode::Function* f, bool stripped, int level)
{
    if( f == 0 )
        return false;

    f->calcVarNames();

    const bool doStrip = f->isStripped() || stripped;

    //int nextR = 0;
    const int nextR = f->d_numparams;
    out << ws(level) << "function F" << f->d_id << "(";
    for( int i = 0; i < f->d_numparams; i++ )
    {
        if( i < f->d_varNames.size() && !doStrip )
        {
            if( i != 0 )
                out << " ";
            Q_ASSERT( !f->d_varNames[i].isEmpty() );
            out << f->d_varNames[i];
        }else
        {
            if( i != 0 )
                out << " ";
            out << "R" << i;
            // out << "__p" << i;
            // nextR = i+1;
        }
    }
    out << ") ";
    if( !f->isStripped() )
        out << "\t-- lines "<< f->d_firstline << " to " << f->d_firstline + f->d_numline;
    out << endl;

#if 0
    QHash<QByteArray,int> unique;
    for( int i = 0; i < f->d_numparams && i < f->d_vars.size(); i++ )
        unique.insert( f->d_vars[i].d_name,i);

    if( f->d_framesize > f->d_numparams || f->d_framesize != 0 && f->d_vars.isEmpty() )
    {
        // print variable declarations
        QByteArray buf;
        buf = ws(level+1);
        buf += "var\t";
        if( f->d_vars.size() > f->d_numparams )
        {
            buf += "{ ";
            for( int i = f->d_numparams; i < f->d_vars.size(); i++ )
            {
                QByteArray name = f->d_vars[i].d_name;
                if( name.startsWith('(') )
                    continue; // don't use these as vars; they're used as Rx instead

                if( unique.contains(name) )
                {
                    // qDebug() << "duplicate var" << name;
                    continue;
                }
                unique.insert(name,i);
                buf += name + " ";
                if( buf.size() > 80 )
                {
                    out << buf << endl;
                    buf = ws(level+2);
                }
            }
            buf += "} ";
        }
        out << buf;
#ifdef _USE_REGISTER_ARRAY_
        if( f->d_framesize > 0 )
        {
            out << "T[" << f->d_framesize << "]" << endl;
        }
#else
        buf.clear();
        if( f->d_vars.size() > f->d_numparams )
        {
            out << endl;
            buf = ws(level+2);
        }
        buf += "{ ";
        for( int i = 0; i < qMax( int(f->d_framesize), unique.size() ); i++ )
        {
            buf += "R" + QByteArray::number( i ) + " ";
            if( buf.size() > 80 )
            {
                out << buf << endl;
                buf = ws(level+2);
            }
        }
        buf += "} ";
        out << buf << endl;
#endif
    }
#else
    // print variable declarations
    if( f->d_framesize > f->d_numparams )
    {
        QHash<QByteArray,int> unique;
        QByteArray buf;
        buf = ws(level+1);
        buf += "var\t";
        if( !doStrip && f->d_vars.size() > f->d_numparams )
        {
            buf += "{ ";
            for( int i = f->d_numparams; i < f->d_varNames.size(); i++ )
            {
                QByteArray name = f->d_varNames[i];
                if( name.isEmpty() )
                    continue;
                if( name.startsWith('(') )
                    continue; // don't use these as vars; they're used as Rx instead

                if( unique.contains(name) )
                    continue;
                unique.insert(name,i);

                buf += name + "(" + QByteArray::number(i) + ") ";
                if( buf.size() > 80 )
                {
                    out << buf << endl;
                    buf = ws(level+2);
                }
            }
            buf += "} ";
        }

        // print Rx declarations
        out << buf;
        buf.clear();
        if( !doStrip && f->d_vars.size() > f->d_numparams )
        {
            out << endl;
            buf = ws(level+2);
        }
        buf += "{ ";
        for( int i = nextR; i < f->d_framesize; i++ )
        {
            buf += "R" + QByteArray::number( i ) + "(" + QByteArray::number(i) + ") ";
            if( buf.size() > 80 )
            {
                out << buf << endl;
                buf = ws(level+2);
            }
        }
        buf += "} ";
        out << buf << endl;
    }

#endif

#ifdef _DEBUG_
        for( int i = 0; i < f->d_vars.size(); i++ )
            out << ws(level+2) << "-- " << f->d_vars[i].d_name << " pc " <<
                   f->d_vars[i].d_startpc << " to " << f->d_vars[i].d_endpc << endl;
#endif
#ifdef _DEBUG_
        QFile dbg(QString("F%1_vars.txt").arg(f->d_id));
        dbg.open(QIODevice::WriteOnly);
        QTextStream out2(&dbg);
        for( int pc = 0; pc < f->d_byteCodes.size(); pc++ )
        {
            QByteArray str = "pc" + QByteArray::number(pc+2) + "\t";
            for( int slot = 0; slot < f->d_framesize; slot++ )
            {
                const QByteArray name = f->getVarName(pc,slot);
                if( !name.isEmpty() )
                    str += "s" + QByteArray::number(slot) + ":" + name + "\t";
            }
            out2 << str << endl;
            //out << ws(level+2) << "-- " << str << endl;
        }
#endif

    int funCount = 0;
    for( int i = f->d_constObjs.size() - 1; i >= 0; i-- )
    {
        const QVariant& o = f->d_constObjs[i];
        if( o.canConvert<JitBytecode::FuncRef>() )
        {
            if( funCount++ == 0 )
                out << endl;
            writeFunc(out, o.value<JitBytecode::FuncRef>().constData(), stripped, level+1 );
        }
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

        int lastLine = 0;
        for( int pc = 0; pc < f->d_byteCodes.size(); pc++ )
        {
            QByteArray warning;
            if( labels.contains(pc) )
                out << ws(level) << "__L" << pc << ":" << endl;
            JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f->d_byteCodes[pc]);
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
            case JitBytecode::OP_RET:
                out << "RET";
                bc.d_cd--; // Operand D is one plus the number of results to return.
                break;
            case JitBytecode::OP_RET0:
                out << "RET";
                bc.d_tcd = JitBytecode::Instruction::Unused;
                bc.d_ta = JitBytecode::Instruction::Unused;
                break;
           case JitBytecode::OP_RET1:
                out << "RET";
                bc.d_tcd = JitBytecode::Instruction::Unused;
                break;
            case JitBytecode::OP_KNIL:
                out << "KNIL";
                bc.d_cd = bc.d_cd - bc.d_a + 1;
                if( bc.d_cd == 1 )
                    bc.d_tcd = JitBytecode::Instruction::Unused;
                else
                    bc.d_tcd = JitBytecode::Instruction::_lit;
                break;
            case JitBytecode::OP_TNEW:
                out << "TNEW";
                bc.d_b = bc.d_cd & 0x7ff;  // lowest 11 bits
                bc.d_cd = bc.d_cd >> 11; // upper 5 bits
                bc.d_tb = JitBytecode::Instruction::_lit;
                bc.d_tcd = JitBytecode::Instruction::_lit;
                if( bc.d_cd == 0 && bc.d_b == 0 )
                {
                    bc.d_tcd = JitBytecode::Instruction::Unused;
                    bc.d_tb = JitBytecode::Instruction::Unused;
                }else if( bc.d_cd == 0 )
                    bc.d_tcd = JitBytecode::Instruction::Unused;
                break;
            case JitBytecode::OP_CALL:
                {
                    out << "CALL";
                    bc.d_cd--;
                    if( bc.d_b >= 1 )
                        bc.d_b = bc.d_b - 1;
                    else if( bc.d_b == 0 ) // Operand B is zero for calls which return all results; modify to 1 return here
                    {
                        warning = "original second argument is MULTRES (not supported)";
                        bc.d_b = 1;
                    }
                    if( bc.d_b == 0 && bc.d_cd == 0 )
                    {
                        bc.d_tb = JitBytecode::Instruction::Unused;
                        bc.d_tcd = JitBytecode::Instruction::Unused;
                    }else if( bc.d_cd == 0 )
                        bc.d_tcd = JitBytecode::Instruction::Unused;
                }
                break;
            case JitBytecode::OP_CALLM:
                {
                    warning = "original is CALLM " + QByteArray::number(bc.d_b) + " "
                            + QByteArray::number(bc.d_cd) + " (not supported)";
                    out << "CALL";
                    // compared to CALL bc.d_cd is already the true number of fixed args
                    if( bc.d_cd == 0 )
                        bc.d_cd = 1; // we return at least one fixed arg
                    if( bc.d_b >= 1 )
                        bc.d_b = bc.d_b - 1;
                    else if( bc.d_b == 0 ) // Operand B is zero for calls which return all results; modify to 1 return here
                        bc.d_b = 1;
                    if( bc.d_b == 0 && bc.d_cd == 0 )
                    {
                        bc.d_tb = JitBytecode::Instruction::Unused;
                        bc.d_tcd = JitBytecode::Instruction::Unused;
                    }else if( bc.d_cd == 0 )
                        bc.d_tcd = JitBytecode::Instruction::Unused;
                }
                break;
            case JitBytecode::OP_CALLT:
                out << "CALLT";
                bc.d_cd--;
                break;
            case JitBytecode::OP_CALLMT:
                warning = "original is CALLMT " + QByteArray::number(bc.d_cd) + " (not supported)";
                if( bc.d_cd == 0 )
                    bc.d_cd = 1; // we return at least one fixed arg
                break;
            case JitBytecode::OP_CAT:
                out << "CAT";
                bc.d_cd = bc.d_cd - bc.d_b + 1;
                if( bc.d_cd == 1 )
                    bc.d_tcd = JitBytecode::Instruction::Unused;
                else
                    bc.d_tcd = JitBytecode::Instruction::_lit;
                break;
                // implement later
            case JitBytecode::OP_TSETM:
            case JitBytecode::OP_RETM:
            case JitBytecode::OP_VARG:
            case JitBytecode::OP_ITERC:
            case JitBytecode::OP_ITERN:
            case JitBytecode::OP_ITERL:
                // internals
            case JitBytecode::OP_JFORI:
            case JitBytecode::OP_IFORL:
            case JitBytecode::OP_JFORL:
            case JitBytecode::OP_IITERL:
            case JitBytecode::OP_JITERL:
            case JitBytecode::OP_ILOOP:
            case JitBytecode::OP_JLOOP:
            case JitBytecode::OP_ISNEXT:
                warning = "operator not supported";
                out << bc.d_name;
                break;
            default:
                out << bc.d_name;
                break;
            }
            out << " ";

            const QByteArray a = renderArg(f,bc.d_ta,bc.d_a,pc,stripped);
            const QByteArray b = renderArg(f,bc.d_tb,bc.d_b,pc,stripped);
            const QByteArray c = renderArg(f,bc.d_tcd,bc.getCd(),pc,stripped);
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
            if( !warning.isEmpty() )
                out << " -- WARNING " << warning;

#ifdef _DEBUG_
            out << " -- #" << pc + 1;
#endif
            if( !f->isStripped() )
            {
                if( lastLine != f->d_lines[pc] )
                {
                    lastLine = f->d_lines[pc];
                    out << "\t\t-- " << lastLine;
                }
            }

            out << endl;
        } // end for each statement

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

static QByteArray escape( QByteArray str )
{
    if( !str.isEmpty() && str[0] == 0 )
        return QByteArray();
    str.replace('\\', "\\\\");
    str.replace('\n', "\\n" );
    str.replace('\a', "\\a" );
    str.replace('\b', "\\b" );
    str.replace('\f', "\\f" );
    str.replace('\r', "\\r" );
    str.replace('\t', "\\t" );
    str.replace('\v', "\\v" );
    str.replace('"', "\\\"" );
    str.replace('\'', "\\'" );
    return str;
}

static QByteArray tostring(const QVariant& v)
{
    if( v.type() == QVariant::ByteArray )
    {
        return "\"" + escape( v.toByteArray() ) + "\"";
    }else if( JitBytecode::isString( v ) )
        return "\"" + v.toString().toUtf8() + "\"";
    else if( JitBytecode::isNumber( v ) )
        return QByteArray::number( v.toDouble() );
    else
        return v.toByteArray();
}

QByteArray Disasm::renderArg(const JitBytecode::Function* f, int t, int v, int pc, bool stripped)
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
            // int idx;
            // QByteArray name = varName(f->getVarName(pc, v, &idx ),idx);
            QByteArray name;
            if( !stripped )
                name= varName( v < f->d_varNames.size() ? f->d_varNames[v] : QByteArray(),v);
            if( !name.isEmpty() )
                return name;
//            if( v < f->d_numparams && ( f->isStripped() || stripped ) )
//                return "__p" + QByteArray::number(v);
#ifdef _USE_REGISTER_ARRAY_
            return "T[" + QByteArray::number(v) + "]";
#else
            return "R" + QByteArray::number(v);
#endif
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

            QByteArray quali;
            if( up.second != f )
                quali = "F" + QByteArray::number(up.second->d_id) + ".";
//            if( v < f->d_upNames.size() )
//                return quali + f->d_upNames[v];
            if( up.first < up.second->d_varNames.size() && !stripped )
                return quali + up.second->d_varNames[up.first];
            else
#ifdef _USE_REGISTER_ARRAY_
                return quali + "T[" + QByteArray::number(up.first) + "]";
#else
                return quali + "R" + QByteArray::number(up.first);
#endif
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
                for( int i = 0; i < t.d_array.size(); i++ )
                {
                    if( i != 0 )
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

