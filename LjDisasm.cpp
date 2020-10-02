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
#include "LuaJitComposer.h"
#include <QtDebug>
#include <QSet>
#include <QTextStream>
#include <QFile>
using namespace Ljas;
using namespace Lua;

const char* Disasm::s_opName[] = {
    "???",
    "ISLT", "ISGE", "ISLE", "ISGT",
    "ISEQ", "ISNE",
    "ISTC", "ISFC", "IST", "ISF",
    "MOV",
    "NOT", "UNM",
    "LEN", "ADD", "SUB", "MUL", "DIV", "MOD",
    "POW",
    "CAT",
    "KSET", "KNIL",
    "UGET", "USET",
    "UCLO",
    "FNEW",
    "TNEW", "TDUP",
    "GGET", "GSET",
    "TGET", "TSET",
    "CALL", "CALLT", "RET",
    "FORI", "FORL",
    "LOOP",
    "JMP"
};

const char* Disasm::s_opHelp[] = {
    "operation not supported",
    "ISLT lhs:desig rhs:desig",
    "ISGE lhs:desig rhs:desig",
    "ISLE lhs:desig rhs:desig",
    "ISGT lhs:desig rhs:desig",
    "ISEQ lhs:desig rhs:( desig | string | number | primitive )",
    "ISNE lhs:desig rhs:( desig | string | number | primitive )",
    "ISTC lhs:desig rhs:desig, copy rhs to lhs and jump, if rhs is true",
    "ISFC lhs:desig rhs:desig, copy rhs to lhs and jump, if rhs is false",
    "IST slot:desig, jump if slot is true",
    "ISF slot:desig, jump if slot is false",
    "MOV dst:desig src:desig",
    "NOT dst:desig src:desig",
    "UNM dst:desig src:desig",
    "LEN dst:desig table:desig",
    "ADD dst:desig lhs:( desig | number ) rhs:( desig | number )",
    "SUB dst:desig lhs:( desig | number ) rhs:( desig | number )",
    "MUL dst:desig lhs:( desig | number ) rhs:( desig | number )",
    "DIV dst:desig lhs:( desig | number ) rhs:( desig | number )",
    "MOD dst:desig lhs:( desig | number ) rhs:( desig | number )",
    "POW dst:desig lhs:desig rhs:desig, dst = lhs ^ rhs",
    "CAT dst:desig from:desig [ len:posint ], dst = from .. ~ .. from + len - 1",
    "KSET dst:desig const:( string | number | primitive | cname )",
    "KNIL from:desig [ len:posint ], Set slots from to from + len - 1 to nil",
    "UGET dst:desig uv:desig",
    "USET uv:desig src:( string | number | primitive | desig )",
    "UCLO from:desig [ label ], Close upvalues for slots ≥ from and jump to label",
    "FNEW dst:desig fname",
    "TNEW dst:desig [ arraySize:posint [ hashSize:posint ] ]",
    "TDUP dst:desig src:( cname | table_literal )",
    "GGET dst:desig index:( string | cname )",
    "GSET src:desig index:( string | cname )",
    "TGET dst:desig table:desig index:( desig | string | posint )",
    "TSET src:desig table:desig index:( desig | string | posint )",
    "CALL slots:desig [ numOfReturns:posint [ numOfArgs:posint ] ]",
    "CALLT slots:desig [ numOfArgs:posint ]",
    "RET [ slots:desig [ numOfSlots:posint ] ]",
    "FORI slots:desig label, slots=index,stop,step,index copy",
    "FORL desig label",
    "LOOP",
    "JMP label"
};


bool Disasm::disassemble(const JitBytecode& bc, QIODevice* f, const QString& path, bool stripped, bool alloc)
{
    QTextStream out(f);
    out.setCodec("UTF-8");
    out << "-- disassembled from ";
    if( path.isEmpty() )
        out << "Lua source";
    else
        out << path;
    out << endl << endl;
    if( !writeFunc( out, bc.getRoot(), stripped, alloc, 0 ) )
        return false;
    return true;
}

bool Disasm::adaptToLjasm(JitBytecode::Instruction& bc, OP& op, QByteArray& warning )
{
    switch( JitBytecode::Op(bc.d_op) )
    {
    case JitBytecode::OP_INVALID:
        op = INVALID;
        break;
    case JitBytecode::OP_ISLT:
        op = ISLT;
        break;
    case JitBytecode::OP_ISGE:
        op = ISGE;
        break;
    case JitBytecode::OP_ISLE:
        op = ISLE;
        break;
    case JitBytecode::OP_ISGT:
        op = ISGT;
        break;
    case JitBytecode::OP_ISTC:
        op = ISTC;
        break;
    case JitBytecode::OP_ISFC:
        op = ISFC;
        break;
    case JitBytecode::OP_IST:
        op = IST;
        break;
    case JitBytecode::OP_ISF:
        op = ISF;
        break;
    case JitBytecode::OP_MOV:
        op = MOV;
        break;
    case JitBytecode::OP_NOT:
        op = NOT;
        break;
    case JitBytecode::OP_UNM:
        op = UNM;
        break;
    case JitBytecode::OP_LEN:
        op = LEN;
        break;
    case JitBytecode::OP_POW:
        op = POW;
        break;
    case JitBytecode::OP_UGET:
        op = UGET;
        break;
    case JitBytecode::OP_UCLO:
        op = UCLO;
        break;
    case JitBytecode::OP_FNEW:
        op = FNEW;
        break;
    case JitBytecode::OP_TDUP:
        op = TDUP;
        break;
    case JitBytecode::OP_GGET:
        op = GGET;
        break;
    case JitBytecode::OP_GSET:
        op = GSET;
        break;
    case JitBytecode::OP_FORI:
        op = FORI;
        break;
    case JitBytecode::OP_FORL:
        op = FORL;
        break;
    case JitBytecode::OP_LOOP:
        op = LOOP;
        break;
    case JitBytecode::OP_JMP:
        op = JMP;
        break;
    case JitBytecode::OP_ISEQV:
    case JitBytecode::OP_ISEQS:
    case JitBytecode::OP_ISEQN:
    case JitBytecode::OP_ISEQP:
        op = ISEQ;
        break;
    case JitBytecode::OP_ISNEV:
    case JitBytecode::OP_ISNES:
    case JitBytecode::OP_ISNEN:
    case JitBytecode::OP_ISNEP:
        op = ISNE;
        break;
    case JitBytecode::OP_ADDVN:
    case JitBytecode::OP_ADDNV:
    case JitBytecode::OP_ADDVV:
        op = ADD;
        break;
    case JitBytecode::OP_SUBVN:
    case JitBytecode::OP_SUBNV:
    case JitBytecode::OP_SUBVV:
        op = SUB;
        break;
    case JitBytecode::OP_MULVN:
    case JitBytecode::OP_MULNV:
    case JitBytecode::OP_MULVV:
        op = MUL;
        break;
    case JitBytecode::OP_DIVVN:
    case JitBytecode::OP_DIVNV:
    case JitBytecode::OP_DIVVV:
        op = DIV;
        break;
    case JitBytecode::OP_MODVN:
    case JitBytecode::OP_MODNV:
    case JitBytecode::OP_MODVV:
        op = MOD;
        break;
    case JitBytecode::OP_KSTR:
    case JitBytecode::OP_KCDATA:
    case JitBytecode::OP_KSHORT:
    case JitBytecode::OP_KNUM:
    case JitBytecode::OP_KPRI:
        op = KSET;
        break;
    case JitBytecode::OP_USETV:
    case JitBytecode::OP_USETS:
    case JitBytecode::OP_USETN:
    case JitBytecode::OP_USETP:
        op = USET;
        break;
    case JitBytecode::OP_TGETV:
    case JitBytecode::OP_TGETS:
    case JitBytecode::OP_TGETB:
        op = TGET;
        break;
    case JitBytecode::OP_TSETV:
    case JitBytecode::OP_TSETS:
    case JitBytecode::OP_TSETB:
        op = TSET;
        break;
    case JitBytecode::OP_RET:
        op = RET;
        bc.d_cd--; // Operand D is one plus the number of results to return.
        break;
    case JitBytecode::OP_RET0:
        op = RET;
        bc.d_tcd = JitBytecode::Instruction::Unused;
        bc.d_ta = JitBytecode::Instruction::Unused;
        break;
   case JitBytecode::OP_RET1:
        op = RET;
        bc.d_tcd = JitBytecode::Instruction::Unused;
        break;
    case JitBytecode::OP_KNIL:
        op = KNIL;
        bc.d_cd = bc.d_cd - bc.d_a + 1;
        if( bc.d_cd == 1 )
            bc.d_tcd = JitBytecode::Instruction::Unused;
        else
            bc.d_tcd = JitBytecode::Instruction::_lit;
        break;
    case JitBytecode::OP_TNEW:
        op = TNEW;
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
            op = CALL;
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
            op = CALL;
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
        op = CALLT;
        bc.d_cd--;
        break;
    case JitBytecode::OP_CALLMT:
        op = INVALID;
        warning = "original is CALLMT " + QByteArray::number(bc.d_cd) + " (not supported)";
        if( bc.d_cd == 0 )
            bc.d_cd = 1; // we return at least one fixed arg
        break;
    case JitBytecode::OP_CAT:
        op = CAT;
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
        op = INVALID;
        break;
    }
    return true;
}

bool Disasm::adaptToLjasm(JitBytecode::Instruction& bc, QByteArray& mnemonic, QByteArray& warning)
{
    OP op;
    bool res = adaptToLjasm( bc, op, warning );
    mnemonic = s_opName[op];
    if( op == INVALID )
        mnemonic = bc.d_name;
    return res;
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

bool Disasm::writeFunc(QTextStream& out, const JitBytecode::Function* f, bool stripped, bool alloc, int level)
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
            // Q_ASSERT( !f->d_varNames[i].isEmpty() );
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
    {
        out << "\t-- lines ";
        out << JitComposer::unpackRow2(f->d_firstline) << " to " <<
                   JitComposer::unpackRow2(f->lastLine() );
    }
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

                buf += name;
                if( alloc )
                    buf += "(" + QByteArray::number(i) + ") ";
                else
                    buf += " ";
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
            buf += "R" + QByteArray::number( i );
            if( alloc )
                buf += "(" + QByteArray::number(i) + ") ";
            else
                buf += " ";
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
            writeFunc(out, o.value<JitBytecode::FuncRef>().constData(), stripped, alloc, level+1 );
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

        quint32 lastLine = 0;
        for( int pc = 0; pc < f->d_byteCodes.size(); pc++ )
        {
            QByteArray warning;
            if( labels.contains(pc) )
                out << ws(level) << "__L" << pc << ":" << endl;
            JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f->d_byteCodes[pc]);
            out << ws(level+1);
            QByteArray mnemonic;
            adaptToLjasm( bc, mnemonic, warning );
            out << mnemonic << " ";

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
                    out << "\t\t-- ";
                    if( JitComposer::isPacked(lastLine) )
                        out << JitComposer::unpackRow(lastLine) << ":" << JitComposer::unpackCol(lastLine);
                    else
                        out << lastLine;
                }
            }

            out << endl;
        } // end for each statement

    }

    out << ws(level) << "end F" << f->d_id << endl << endl;
    return true;
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
    if( !str.isEmpty() && str[0] == char(0) )
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

QByteArray Disasm::renderArg(const JitBytecode::Function* f, int t, int v, int pc, bool stripped, bool alt)
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
            if( alt )
                return "[" + QByteArray::number(v) + "]";
            else
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
        if( alt )
            return "->" + QByteArray::number(pc+1+v);
        else
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
            if( !stripped && up.first < up.second->d_varNames.size() && !up.second->d_varNames[up.first].isEmpty() )
                return quali + up.second->d_varNames[up.first];
            else
            {
#ifdef _USE_REGISTER_ARRAY_
                return quali + "T[" + QByteArray::number(up.first) + "]";
#else
                if( alt )
                    return quali + "[" + QByteArray::number(up.first) + "]";
                else
                    return quali + "R" + QByteArray::number(up.first);
#endif
            }
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

