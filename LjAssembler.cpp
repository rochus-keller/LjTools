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

#include "LjAssembler.h"
#include "LjasErrors.h"
#include <QBitArray>
#include <QtDebug>
#include <QElapsedTimer>
#include <QBuffer>
using namespace Ljas;
using namespace Lua;

#define LJ_MAX_SLOTS	250

Q_DECLARE_METATYPE( Assembler::Named* )
Q_DECLARE_METATYPE( SynTree* )

// TODO: array vars


struct Interval
{
    uint d_from : 24;
    uint d_slot : 8;
    quint32 d_to;
    void* d_payload;
    Interval(uint from, uint to, void* pl):d_from(from),d_to(to),d_payload(pl),d_slot(0){}
};
typedef QList<Interval> Intervals;

static bool sortIntervals( const Interval& lhs, const Interval& rhs )
{
    return lhs.d_from < rhs.d_from;
}

static int checkFree( const QBitArray& pool, int slot, int len )
{
    if( slot + len >= pool.size() )
        return 0;
    for( int i = 0; i < len ; i++ )
    {
        if( pool.at(slot+i) )
            return i;
    }
    return len;
}

static int nextFreeSlot( QBitArray& pool, int len = 1 )
{
    int slot = 0;
    while( true )
    {
        // skip used
        while( slot < pool.size() && pool.at(slot) )
            slot++;
        if( slot < pool.size() )
        {
            Q_ASSERT( !pool.at(slot) );
            if( len == 1 )
            {
                pool.setBit(slot);
                return slot;
            } // else
            const int free = checkFree( pool, slot, len );
            if( free == len )
            {
                pool.fill(true,slot,slot+len);
                return slot;
            } // else
            slot += free;
        }
    }
    return -1;
}

static bool allocateWithLinearScan(QBitArray& pool, Intervals& vars, int len )
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
            pool.clearBit(vars[j.value()].d_slot);
            pool.fill(false, vars[j.value()].d_slot, vars[j.value()].d_slot + len );
            j = active.erase(j);
        }
        int slot = nextFreeSlot(pool,len);
        if( active.size() >= LJ_MAX_SLOTS || slot < 0 )
            return false;
        vars[i].d_slot = slot;
        active.insert(vars[i].d_to, i);
    }
    return true;
}

Assembler::Assembler(Errors* errs):d_errs(errs),d_xref(0)
{
    Q_ASSERT( errs != 0 );
}

Assembler::~Assembler()
{
    if( d_xref )
        delete d_xref;
}

bool Assembler::process(SynTree* st, const QByteArray& sourceRef, bool createXref)
{
    Q_ASSERT( st != 0 && st->d_tok.d_type == SynTree::R_function_decl && st->d_children.size() >= 3 );

    d_comp.clear();
    d_ref = sourceRef;
    d_top = Func();
    if( d_xref )
        delete d_xref;
    d_xref = 0;
    d_createXref = createXref;

    QElapsedTimer t;
    t.start();
    const bool res = processFunc( st, 0 );
    // qDebug() << "Assembler::process runtime [ms]:" << t.elapsed();
    if( res )
    {
        QBuffer buf;
        buf.open(QIODevice::WriteOnly);
        d_comp.write(&buf);
        buf.close();
        d_bc = buf.data();
        return true;
    }else
        return false;
}

Assembler::Xref*Assembler::getXref(bool transferOwnership)
{
    Xref* res = d_xref;
    if( transferOwnership )
        d_xref = 0;
    return res;
}

bool Assembler::checkSlotOrder( const Var* v, int n )
{
    int lastSlot = v->d_slot;
    v = v->d_next;
    n--;
    while( n > 0 )
    {
        if( v == 0 || v->d_slot != lastSlot + 1 )
            return false;
        lastSlot = v->d_slot;
        v = v->d_next;
        n--;
    }
    return true;
}

Assembler::Var*Assembler::toVar(const QVariant& v)
{
    Named* n = v.value<Named*>();
    if(n)
        return n->toVar();
    else
        return 0;
}

Assembler::Var*Assembler::toVar(Assembler::Named* n)
{
    if( n == 0 )
        return 0;
    else
        return n->toVar();
}

void Assembler::findOverlaps(Assembler::VarList& out, Assembler::Var* header)
{
    // v0 v1 v2 v3
    //       u0 u1 u2
    Var* v = header->d_next;
    int n = header->d_n - 1;
    bool headerRegistered = false;
    while( v && n-- > 0 )
    {
        if( v->d_n > 1 )
        {
            if( !headerRegistered && !out.contains(header) )
            {
                headerRegistered = true;
                out << header;
            }
            if( !out.contains(v) )
                out << v;
            findOverlaps(out, v );
        }
        v = v->d_next;
    }
}

void Assembler::resolveOverlaps(const Assembler::VarList& l)
{
    for( int i = l.size() - 1; i > 0; i-- )
    {
        Var* v = l[i];
        int off = 0;
        Var* h = v;
        while( h )
        {
            h = h->d_prev;
            off++;
            if( h && h->d_n > 1 )
                break;
        }
        Q_ASSERT( h != 0 && h == l[i-1] );

        // h0 h1 h2 h3
        //          v0 v1 v2
        // off = 3, rem = 2
        // h0 h1 h2 h3
        //    v0 v1 v2
        // off = 1, rem = 0
        h->d_n += qMax( off + v->d_n - h->d_n, 0 );
        v->d_n = 0; // so no further overlap correction occurs
    }
}

static bool xrefSort( const Assembler::Xref* lhs, const Assembler::Xref* rhs )
{
    return lhs->d_line < rhs->d_line || (!(rhs->d_line < lhs->d_line) && lhs->d_col < rhs->d_col);
}

bool Assembler::processFunc(SynTree* st, Func* outer)
{
    Q_ASSERT( st != 0 && st->d_tok.d_type == SynTree::R_function_decl );

    SynTree* fname = flatten(findFirstChild(st, SynTree::R_fname ));
    if( fname == 0 && outer != 0 )
        return error( st, tr("only top-level function can be unnamed") );
    SynTree* lastName = 0;
    if( fname != 0 )
    {
        if( outer && outer->d_names.contains(fname->d_tok.d_val) )
            return error( fname, tr("function name not unique") );
        lastName = flatten(st->d_children.last());
        if( lastName == 0 || lastName->d_tok.d_type != Tok_ident )
            return error( lastName, tr("expected function name after 'end'") );
        if( lastName->d_tok.d_val != fname->d_tok.d_val )
            return error( lastName, tr("name after 'end' not equal to function name") );
    }

    Func* me = new Func();
    me->d_outer = outer;
    me->d_st = st;
    if( fname && outer )
    {
        me->d_name = fname->d_tok.d_val;
        outer->d_names.insert(me->d_name, me);
    }else if( outer )
        outer->d_names.insert(QByteArray(),me);
    else
        d_top.d_names.insert(QByteArray(),me);

    if( d_createXref )
    {
        Xref* x = new Xref();
        me->d_xref = x;
        x->d_name = me->d_name;
        x->d_kind = Xref::Func;
        x->d_role = Xref::Decl;
        x->d_line = fname->d_tok.d_lineNr;
        x->d_col = fname->d_tok.d_colNr;
        if( outer )
        {
            Q_ASSERT( outer->d_xref );
            outer->d_xref->d_subs.append(x);
        }else
            d_xref = x;
    }

    SynTree* hdr = findFirstChild(st, SynTree::R_function_header );
    Q_ASSERT( hdr != 0 );

    if( !processParams(hdr,me) )
        return false;

    if( !processConsts(hdr,me) )
        return false;

    if( !processVars(hdr,me) )
        return false;

    int id = d_comp.openFunction(me->d_params.size(),d_ref,st->d_tok.d_lineNr, st->d_children.last()->d_tok.d_lineNr );
    if( outer )
    {
        Q_ASSERT( id != -1 );
        me->d_id = id;
    }

    for( int i = 3; i < hdr->d_children.size(); i++ )
    {
        if( hdr->d_children[i]->d_tok.d_type == SynTree::R_function_decl )
        {
            if( !processFunc(hdr->d_children[i], me ) )
                return false;
        }
    }

    Stmts stmts;
    SynTree* body = findFirstChild(st, SynTree::R_function_body );
    if( body )
    {
        Labels labels;
        int i = 1;
        while( i < body->d_children.size() )
        {
            if( body->d_children[i]->d_tok.d_type == SynTree::R_labelDef )
            {
                SynTree* ld = body->d_children[i];
                Q_ASSERT( ld->d_children.size() == 2 && ld->d_children.last()->d_tok.d_type == Tok_Colon );
                SynTree* name = flatten(ld->d_children.first() );
                Q_ASSERT( name->d_tok.d_type == Tok_ident );
                if( labels.contains(name->d_tok.d_val) )
                    return error(name,tr("duplicate label"));
                else
                {
                    Xref* x = 0;
                    if( d_createXref )
                    {
                        x = new Xref();
                        x->d_name = name->d_tok.d_val;
                        x->d_kind = Xref::Label;
                        x->d_role = Xref::Decl;
                        x->d_line = name->d_tok.d_lineNr;
                        x->d_col = name->d_tok.d_colNr;
                        me->d_xref->d_subs.append(x);
                    }
                    labels.insert(name->d_tok.d_val,qMakePair(stmts.size(),x) );
                }
                i++;
            }
            if( body->d_children[i]->d_tok.d_type == SynTree::R_statement )
            {
                Q_ASSERT( body->d_children[i]->d_children.size() == 1 );
                if( !processStat( body->d_children[i]->d_children.first(), stmts, me ) )
                    return false;
                i++;
            }
        }

        if( !checkJumpsAndMore( stmts, labels, me ) )
            return false;

    }else
    {
        // add at least RET
        Stmt s;
        s.d_st = st->d_children.last();
        s.d_pc = 0;
        s.d_op = JitBytecode::OP_RET0;
        s.d_vals << quint8(0) << quint8(0);
        stmts << s;
    }

    if( !allocateRegisters3(me) )
        return false;

//#ifdef _DEBUG
    if( !checkSlotOrder(stmts) )
        return false;
//#endif

    if( !generateCode(me,stmts) )
        return false;

    d_comp.closeFunction(me->d_firstUnused.d_slot);

    if( d_createXref )
    {
        if( lastName && lastName->d_tok.d_type == Tok_ident )
        {
            Xref* x = new Xref();
            x->d_name = me->d_name;
            x->d_kind = Xref::Func;
            x->d_role = Xref::Ref;
            x->d_line = lastName->d_tok.d_lineNr;
            x->d_col = lastName->d_tok.d_colNr;
            x->d_decl = me->d_xref;
            me->d_xref->d_usedBy.append(x);
            me->d_xref->d_subs.append(x);
        }

        Xref* func = outer ? outer->d_xref->d_subs.last() : d_xref;
        std::sort( func->d_subs.begin(), func->d_subs.end(), xrefSort );
    }

    return true;
}

bool Assembler::processParams(SynTree* hdr, Assembler::Func* me )
{
    SynTree* fp = findFirstChild(hdr, SynTree::R_formal_params );
    Q_ASSERT( fp != 0 );
    for( int i = 0; i < fp->d_children.size(); i++ )
    {
        SynTree* p = flatten(fp->d_children[i]);
        if( me->d_names.contains(p->d_tok.d_val) )
            return error( p, tr("parameter name not unique") );
        Var* s = new Var();
        s->d_name = p->d_tok.d_val;
        s->d_slot = i; // allocate already here
        s->d_to = 1; // mark all params as being used. TODO: why is this? can't we reuse param slots?
        me->d_names.insert( s->d_name, s );
        s->d_func = me;
        me->d_params.append( s );
        createDeclXref(s,p,me);
    }
    return true;
}

bool Assembler::processConsts(SynTree* hdr, Assembler::Func* me)
{
    SynTree* c = findFirstChild(hdr, SynTree::R_const_decls );
    if( c == 0 )
        return true;

    for( int i = 1; i < c->d_children.size(); i += 3 )
    {
        SynTree* name = flatten(c->d_children[i]);
        SynTree* val = c->d_children[i+2];
        Q_ASSERT( name->d_tok.d_type == Tok_ident && val->d_tok.d_type == SynTree::R_const_val &&
                 c->d_children[i+1]->d_tok.d_type == Tok_Eq );
        if( me->d_names.contains(name->d_tok.d_val) )
            return error( name, tr("constant name not unique") );
        Const* cc = new Const();
        cc->d_name = name->d_tok.d_val;
        me->d_names.insert( cc->d_name, cc );
        createDeclXref(cc,name,me);
        if( !processConst( val, cc, true ) )
            return false;
    }

    return true;
}

bool Assembler::processConst(SynTree* st, Assembler::Const* c, bool allowTable)
{
    st = flatten(st);
    bool ok;
    switch( st->d_tok.d_type )
    {
    case Tok_string:
        c->d_val = st->d_tok.d_val.mid(1,st->d_tok.d_val.size()-2); // remove ""
        return true;
    case Tok_real:
        c->d_val = st->d_tok.d_val.toDouble(&ok);
        Q_ASSERT( ok );
        return true;
    case Tok_negint:
        c->d_val = st->d_tok.d_val.toInt(&ok);
        Q_ASSERT( ok );
        return true;
    case Tok_posint:
        c->d_val = st->d_tok.d_val.toInt(&ok);
        Q_ASSERT( ok );
        return true;
    case Tok_nil:
        Q_ASSERT( c->d_val.isNull() );
        return true;
    case Tok_true:
        c->d_val = true;
        return true;
    case Tok_false:
        c->d_val = false;
        return true;
    case SynTree::R_table_literal:
        if( allowTable )
            return processTable(st,c);
        else
            break;
    default:
        break;
    }
    Q_ASSERT( false );
    return false;
}

bool Assembler::processVars(SynTree* hdr, Assembler::Func* me)
{
    SynTree* v = findFirstChild(hdr, SynTree::R_var_decls );
    if( v == 0 )
        return true;

    Q_ASSERT( v->d_children.first()->d_tok.d_type == Tok_var );

    for( int i = 1; i < v->d_children.size(); i++ )
    {
        if( v->d_children[i]->d_tok.d_type == SynTree::R_var_decl )
        {
            SynTree* d = v->d_children[i];
            Q_ASSERT( !d->d_children.isEmpty() && d->d_children.first()->d_tok.d_type == SynTree::R_vname );
            SynTree* nameSt = flatten(d->d_children.first());
            const QByteArray name = nameSt->d_tok.d_val;
            if( me->d_names.contains(name) )
                return error( d, tr("variable name not unique") );
#ifdef _SUPPORT_ARRAYS_
            if( d->d_children.size() == 2 )
            {
                Arr* a = new Arr();
                a->d_name = name;
                me->d_names.insert( name, a );
                Q_ASSERT( d->d_children.last()->d_tok.d_type == SynTree::R_array &&
                          d->d_children.last()->d_children.size() == 3 );
                SynTree* n = d->d_children.last()->d_children[1];
                Q_ASSERT( n->d_tok.d_type == Tok_posint );
                const int len = n->d_tok.d_val.toUInt();
                if( len == 0 || len > 255 )
                    return error(n,tr("invalid array size") );
                Var* prev = 0;
                for( int j = 0; j < len; j++ )
                {
                    Var* vv = new Var();
                    vv->d_name = name;
                    if( prev )
                    {
                        prev->d_next = vv;
                        vv->d_prev = prev;
                    }
                    prev = vv;
                    a->d_elems.append(vv);
                    vv->d_func = me;
                    createDeclXref(vv,nameSt,me);
                }
            }else
#endif
            {
                Var* vv = new Var();
                vv->d_name = name;
                me->d_names.insert( name, vv );
                vv->d_func = me;
                createDeclXref(vv,nameSt,me);
           }
        }else if( v->d_children[i]->d_tok.d_type == SynTree::R_record )
        {
            SynTree* r = v->d_children[i];
            Q_ASSERT( r->d_children.size() >= 3 && r->d_children.first()->d_tok.d_type == Tok_Lbrace &&
                      r->d_children.last()->d_tok.d_type == Tok_Rbrace );
            Var* prev = 0;
            for( int j = 1; j < r->d_children.size() - 1; j++ )
            {
                SynTree* name = flatten(r->d_children[j]);
                Q_ASSERT( name->d_tok.d_type == Tok_ident );
                if( me->d_names.contains(name->d_tok.d_val) )
                    return error( name, tr("variable name not unique") );
                Var* vv = new Var();
                vv->d_name = name->d_tok.d_val;
                if( prev )
                {
                    prev->d_next = vv;
                    vv->d_prev = prev;
                }
                prev = vv;
                vv->d_func = me;
                me->d_names.insert( vv->d_name, vv );
                createDeclXref(vv,name,me);
            }
        }else
            Q_ASSERT( false );
    }

    return true;
}

bool Assembler::processTable(SynTree* st, Assembler::Const* c)
{
    Q_ASSERT( st->d_children.size() >= 2 && st->d_children.first()->d_tok.d_type == Tok_Lbrace &&
              st->d_children.last()->d_tok.d_type == Tok_Rbrace );
    JitBytecode::ConstTable t;
    int i = 1;
    while( i < st->d_children.size() - 1 )
    {
        SynTree* name = flatten(st->d_children[i]);
        if( name->d_tok.d_type == Tok_ident )
        {
            Q_ASSERT( i < st->d_children.size() - 3 && st->d_children[i+1]->d_tok.d_type == Tok_Eq );
            if( t.d_hash.contains(name->d_tok.d_val) )
                return error( name, tr("duplicate name in const table") );
            Const tmp;
            if( !processConst( st->d_children[i+2], &tmp, false ) )
                return false;
            t.d_hash[name->d_tok.d_val] = tmp.d_val;
            i += 3;
        }else
        {
            Const tmp;
            if( !processConst( st->d_children[i], &tmp, false ) )
                return false;
            t.d_array.append( tmp.d_val );
            i++;
        }
    }
    c->d_val = QVariant::fromValue(t);
    return true;
}

bool Assembler::processStat(SynTree* st, Assembler::Stmts& l, Func* me)
{
    Stmt s;
    s.d_pc = l.size();
    s.d_st = st;
    switch( st->d_tok.d_type )
    {
    case SynTree::R_ISTC_:
    case SynTree::R_ISFC_:
    case SynTree::R_MOV_:
    case SynTree::R_NOT_:
    case SynTree::R_UNM_:
    case SynTree::R_LEN_:
    case SynTree::R_ISGE_:
    case SynTree::R_ISLE_:
    case SynTree::R_ISGT_:
        switch( st->d_tok.d_type)
        {
        case SynTree::R_ISTC_:
            s.d_op = JitBytecode::OP_ISTC;
            break;
        case SynTree::R_ISFC_:
            s.d_op = JitBytecode::OP_ISFC;
            break;
        case SynTree::R_MOV_:
            s.d_op = JitBytecode::OP_MOV;
            break;
        case SynTree::R_NOT_:
            s.d_op = JitBytecode::OP_NOT;
            break;
        case SynTree::R_UNM_:
            s.d_op = JitBytecode::OP_UNM;
            break;
        case SynTree::R_LEN_:
            s.d_op = JitBytecode::OP_LEN;
            break;
        case SynTree::R_ISGE_:
            s.d_op = JitBytecode::OP_ISGE;
            break;
        case SynTree::R_ISLE_:
            s.d_op = JitBytecode::OP_ISLE;
            break;
        case SynTree::R_ISGT_:
            s.d_op = JitBytecode::OP_ISGT;
            break;
        }
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( !fetchV( st->d_children[2], s, me, 1, false ) )
            return false;
        break;
    case SynTree::R_IST_:
    case SynTree::R_ISF_:
        s.d_op = st->d_tok.d_type == SynTree::R_IST_ ? JitBytecode::OP_IST : JitBytecode::OP_ISF;
        Q_ASSERT( st->d_children.size() == 2 );
        s.d_vals << quint8(0);
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        break;
    case SynTree::R_POW_:
        s.d_op = JitBytecode::OP_POW;
        Q_ASSERT( st->d_children.size() == 4 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( !fetchV( st->d_children[2], s, me, 1, false ) )
            return false;
        if( !fetchV( st->d_children[3], s, me, 1, false ) )
            return false;
        break;
    case SynTree::R_ISEQ_:
    case SynTree::R_ISNE_:
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( !fetchVcsnp(st->d_children[2], s, me) )
            return false;
        Q_ASSERT( s.d_vals.size() == 2 );
        if( s.d_vals.last().canConvert<Named*>() )
            s.d_op = st->d_tok.d_type == SynTree::R_ISEQ_ ? JitBytecode::OP_ISEQV : JitBytecode::OP_ISNEV;
        else if( JitBytecode::isString( s.d_vals.last() ) )
            s.d_op = st->d_tok.d_type == SynTree::R_ISEQ_ ? JitBytecode::OP_ISEQS : JitBytecode::OP_ISNES;
        else if( JitBytecode::isPrimitive( s.d_vals.last() ) )
            s.d_op = st->d_tok.d_type == SynTree::R_ISEQ_ ? JitBytecode::OP_ISEQP : JitBytecode::OP_ISNEP;
        else if( JitBytecode::isNumber( s.d_vals.last() ) )
            s.d_op = st->d_tok.d_type == SynTree::R_ISEQ_ ? JitBytecode::OP_ISEQN : JitBytecode::OP_ISNEN;
        else
            return error(st->d_children[2],tr("argument 2 has not supported type") );
        break;
    case SynTree::R_ADD_:
    case SynTree::R_SUB_:
    case SynTree::R_MUL_:
    case SynTree::R_DIV_:
    case SynTree::R_MOD_:
        Q_ASSERT( st->d_children.size() == 4 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( !fetchVcn( st->d_children[2], s, me ) )
            return false;
        if( !fetchVcn( st->d_children[3], s, me ) )
            return false;
        Q_ASSERT( s.d_vals.size() == 3 );
        if( JitBytecode::isNumber(s.d_vals[1]) && s.d_vals[2].canConvert<Named*>() )
        {
            switch( st->d_tok.d_type )
            {
            case SynTree::R_ADD_:
                s.d_op = JitBytecode::OP_ADDNV;
                break;
            case SynTree::R_SUB_:
                s.d_op = JitBytecode::OP_SUBNV;
                break;
            case SynTree::R_MUL_:
                s.d_op = JitBytecode::OP_MULNV;
                break;
            case SynTree::R_DIV_:
                s.d_op = JitBytecode::OP_DIVNV;
                break;
            case SynTree::R_MOD_:
                s.d_op = JitBytecode::OP_MODNV;
                break;
            }
        }else if( s.d_vals[1].canConvert<Named*>() && JitBytecode::isNumber(s.d_vals[2]) )
        {
            switch( st->d_tok.d_type )
            {
            case SynTree::R_ADD_:
                s.d_op = JitBytecode::OP_ADDVN;
                break;
            case SynTree::R_SUB_:
                s.d_op = JitBytecode::OP_SUBVN;
                break;
            case SynTree::R_MUL_:
                s.d_op = JitBytecode::OP_MULVN;
                break;
            case SynTree::R_DIV_:
                s.d_op = JitBytecode::OP_DIVVN;
                break;
            case SynTree::R_MOD_:
                s.d_op = JitBytecode::OP_MODVN;
                break;
            }
        }else if( s.d_vals[1].canConvert<Named*>() && s.d_vals[2].canConvert<Named*>() )
        {
            switch( st->d_tok.d_type )
            {
            case SynTree::R_ADD_:
                s.d_op = JitBytecode::OP_ADDVV;
                break;
            case SynTree::R_SUB_:
                s.d_op = JitBytecode::OP_SUBVV;
                break;
            case SynTree::R_MUL_:
                s.d_op = JitBytecode::OP_MULVV;
                break;
            case SynTree::R_DIV_:
                s.d_op = JitBytecode::OP_DIVVV;
                break;
            case SynTree::R_MOD_:
                s.d_op = JitBytecode::OP_MODVV;
                break;
            }
        }else
            return error(st->d_children[2],tr("argument types not supported") );
        break;
    case SynTree::R_LOOP_:
        {
            Named* n = &me->d_firstUnused;
            s.d_op = JitBytecode::OP_LOOP;
            s.d_vals << QVariant::fromValue(n);
            s.d_vals << int(0);
            // TODO: LOOP depends label from the JMP pointing to it
        }
        break;
    case SynTree::R_KSET_:
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( !fetchCsnp(st->d_children[2], s, me) )
            return false;
        Q_ASSERT( s.d_vals.size() == 2 );
        if( JitBytecode::isString( s.d_vals.last() ) )
            s.d_op = JitBytecode::OP_KSTR;
        else if( JitBytecode::isPrimitive( s.d_vals.last() ) )
            s.d_op = JitBytecode::OP_KPRI;
        else if( JitBytecode::isNumber( s.d_vals.last() ) )
        {
            QVariant v = s.d_vals.last();
            if( v.type() == QVariant::Double )
                s.d_op = JitBytecode::OP_KNUM;
            else
            {
                if( v.toInt() >= SHRT_MIN && v.toInt() <= SHRT_MAX )
                    s.d_op = JitBytecode::OP_KSHORT;
                else
                    s.d_op = JitBytecode::OP_KNUM;
            }
        }else if( s.d_vals.last().canConvert<JitBytecode::ConstTable>() )
            s.d_op = JitBytecode::OP_KCDATA;
        else
            return error(st->d_children[2],tr("argument 2 has not supported type") );
        break;
    case SynTree::R_CAT_:
        Q_ASSERT( st->d_children.size() == 3 || st->d_children.size() == 4 );
        s.d_op = JitBytecode::OP_CAT;
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        else
        {
            int n = 0;
            if( st->d_children.size() == 4 )
            {
                if( !fetchN( st->d_children[3], s ) )
                    return false;
                Q_ASSERT( !s.d_vals.isEmpty() );
                n = s.d_vals.last().toInt();
                if( n == 0 )
                    return error( st->d_children[3], tr("expecting integer greater than zero "));
            }else
            {
                Var* v = toVar( derefDesig(st->d_children[2],me).first ); // only local var
                while( v )
                {
                    n++;
                    v = v->d_next;
                }
                s.d_vals << n;
            }
            if( !fetchV( st->d_children[2], s, me, n, false ) )
                return false;
            // reorder
            Q_ASSERT( s.d_vals.size() == 3 );
            qSwap( s.d_vals[1], s.d_vals[2] );
        }
        break;
    case SynTree::R_KNIL_:
        {
            Q_ASSERT( st->d_children.size() == 2 || st->d_children.size() == 3 );
            s.d_op = JitBytecode::OP_KNIL;
            int n = 0;
            if( st->d_children.size() == 3 )
            {
                if( !fetchN( st->d_children[2], s ) )
                    return false;
                Q_ASSERT( !s.d_vals.isEmpty() );
                n = s.d_vals.last().toInt();
                if( n == 0 )
                    return error( st->d_children[2], tr("expecting integer greater than zero "));
            }else
            {
                Var* v = toVar( derefDesig(st->d_children[1],me).first ); // only local var
                while( v )
                {
                    n++;
                    v = v->d_next;
                }
                s.d_vals << n;
            }

            if( !fetchV( st->d_children[1], s, me, n ) )
                return false;
            // reorder
            Q_ASSERT( s.d_vals.size() == 2 );
            qSwap(s.d_vals[0],s.d_vals[1]);
        }
        break;
    case SynTree::R_USET_:
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchU( st->d_children[1], s, me, true ) )
            return false;
        if( !fetchVcsnp(st->d_children[2], s, me) )
            return false;
        Q_ASSERT( s.d_vals.size() == 2 );
        if( JitBytecode::isString( s.d_vals.last() ) )
            s.d_op = JitBytecode::OP_USETS;
        else if( JitBytecode::isPrimitive( s.d_vals.last() ) )
            s.d_op = JitBytecode::OP_USETP;
        else if( JitBytecode::isNumber( s.d_vals.last() ) )
            s.d_op = JitBytecode::OP_USETN;
        else if( s.d_vals.last().canConvert<Named*>() )
            s.d_op = JitBytecode::OP_USETV;
        else
            return error(st->d_children[2],tr("argument 2 has not supported type") );
        break;
    case SynTree::R_UGET_:
        s.d_op = JitBytecode::OP_UGET;
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( !fetchU( st->d_children[2], s, me, false ) )
            return false;
        break;
    case SynTree::R_UCLO_:
        s.d_op = JitBytecode::OP_UCLO;
        Q_ASSERT( st->d_children.size() == 2 || st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( st->d_children.size() == 3 )
            s.d_vals << QVariant::fromValue( flatten(st->d_children[2]) );
        else
            s.d_vals << QVariant();
        break;
    case SynTree::R_FNEW_:
        s.d_op = JitBytecode::OP_FNEW;
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( !fetchF( st->d_children[2], s, me ) )
            return false;
        break;
    case SynTree::R_TNEW_:
        s.d_op = JitBytecode::OP_TNEW;
        Q_ASSERT( st->d_children.size() >= 2 && st->d_children.size() <= 4 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( st->d_children.size() > 2 )
        {
            if( !fetchN( st->d_children[2], s ) )
                return false;
            const quint16 a = s.d_vals.back().toUInt();
            if( a > 2047 )
                return error( st->d_children[2], tr("array size 0..2047 (11 bits)") );
            if( st->d_children.size() > 3 )
            {
                if( !fetchN( st->d_children[3], s ) )
                    return false;
                const quint16 h = s.d_vals.back().toUInt();
                if( h > 31 )
                    return error( st->d_children[2], tr("hash size 0..31 (5 bits)") );
                s.d_vals.pop_back();
                s.d_vals.back() = a + ( h << 11 );
            }
        }else
            s.d_vals << quint16(0);
        Q_ASSERT( s.d_vals.size() == 2 );
        break;
    case SynTree::R_TDUP_:
        s.d_op = JitBytecode::OP_TDUP;
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( st->d_children[2]->d_tok.d_type == SynTree::R_cname )
        {
            if( !fetchC( st->d_children[2], s, me ) )
                return false;
            Q_ASSERT( s.d_vals.size() == 2 );
            if( !s.d_vals.last().canConvert<JitBytecode::ConstTable>() )
                return error( st->d_children[2], tr("expecting table literal") );
        }else if( st->d_children[2]->d_tok.d_type == SynTree::R_table_literal )
        {
            Const c;
            if( !processTable(st->d_children[2], &c ) )
                return false;
            s.d_vals << c.d_val;
        }
        break;
    case SynTree::R_GGET_:
    case SynTree::R_GSET_:
        s.d_op = st->d_tok.d_type == SynTree::R_GGET_ ? JitBytecode::OP_GGET : JitBytecode::OP_GSET;
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( st->d_children[2]->d_tok.d_type == SynTree::R_cname )
        {
            if( !fetchC( st->d_children[2], s, me ) )
                return false;
            Q_ASSERT( s.d_vals.size() == 2 );
            if( !JitBytecode::isString(s.d_vals.last()) )
                return error( st->d_children[2], tr("expecting string") );
        }else if( st->d_children[2]->d_tok.d_type == Tok_string )
        {
            if( !fetchS(st->d_children[2], s  ) )
                return false;
        }
        break;
    case SynTree::R_TGET_:
    case SynTree::R_TSET_:
        Q_ASSERT( st->d_children.size() == 4 );
        if( !fetchV( st->d_children[1], s, me ) )
            return false;
        if( !fetchV( st->d_children[2], s, me, 1, false ) )
            return false;
        if( !fetchVcsnp(st->d_children[3], s, me) )
            return false;
        Q_ASSERT( s.d_vals.size() == 3 );
        if( s.d_vals.last().canConvert<Named*>() )
            s.d_op = st->d_tok.d_type == SynTree::R_TGET_ ? JitBytecode::OP_TGETV : JitBytecode::OP_TSETV;
        else if( JitBytecode::isString( s.d_vals.last() ) )
            s.d_op = st->d_tok.d_type == SynTree::R_TGET_ ? JitBytecode::OP_TGETS : JitBytecode::OP_TSETS;
        else if( JitBytecode::isNumber( s.d_vals.last() ) && s.d_vals.last().toInt() <= USHRT_MAX )
            s.d_op = st->d_tok.d_type == SynTree::R_TGET_ ? JitBytecode::OP_TGETB : JitBytecode::OP_TSETB;
        else
            return error(st->d_children[2],tr("argument 3 has not supported type") );
        break;
    case SynTree::R_CALL_:
        {
            Q_ASSERT( st->d_children.size() >= 2 && st->d_children.size() <= 4 );
            s.d_op = JitBytecode::OP_CALL;
            int rets = 0, args = 0;
            if( st->d_children.size() > 2 )
            {
                if( !fetchN( st->d_children[2], s ) )
                    return false;
                rets = s.d_vals.back().toInt();
                if( st->d_children.size() > 3 )
                {
                    if( !fetchN( st->d_children[3], s ) )
                        return false;
                    args = s.d_vals.back().toInt();
                }else
                    s.d_vals << args;
                if( rets > LJ_MAX_SLOTS )
                    return error( st->d_children[2], tr("invalid number of return values") );
                if( args > LJ_MAX_SLOTS )
                    return error( st->d_children[2], tr("invalid number of argument") );
            }else
                s.d_vals << rets << args;
            const int n = qMax( rets, args + 1 );
            if( !fetchV( st->d_children[1], s, me, n ) )
                return false;
            Q_ASSERT( s.d_vals.size() == 3 );
            s.d_vals.push_front(s.d_vals.back());
            s.d_vals.pop_back();
        }
        break;
    case SynTree::R_CALLT_:
        {
            Q_ASSERT( st->d_children.size() == 3 );
            s.d_op = JitBytecode::OP_CALLT;
            if( !fetchN( st->d_children[2], s ) )
                return false;
            const int n = s.d_vals.back().toInt() + 1;
            if( n > LJ_MAX_SLOTS )
                return error( st->d_children[2], tr("invalid number of argument") );
            if( !fetchV( st->d_children[1], s, me, n ) )
                return false;
            Q_ASSERT( s.d_vals.size() == 2 );
            qSwap(s.d_vals[0],s.d_vals[1]);
        }
        break;
    case SynTree::R_RET_:
        Q_ASSERT( st->d_children.size() >= 1 && st->d_children.size() <= 3 );
        if( st->d_children.size() > 1 )
        {
            int n = 1;
            if( st->d_children.size() > 2 )
            {
                if( !fetchN( st->d_children[2], s ) )
                    return false;
                n = s.d_vals.back().toInt();
                if( n > LJ_MAX_SLOTS )
                    return error( st->d_children[2], tr("invalid number of return values") );
                s.d_op = JitBytecode::OP_RET;
            }else
            {
                s.d_op = JitBytecode::OP_RET1;
                s.d_vals << n;
            }
            if( !fetchV( st->d_children[1], s, me, n ) )
                return false;
            Q_ASSERT( s.d_vals.size() == 2 );
            qSwap(s.d_vals[0],s.d_vals[1]);
        }else
        {
            s.d_op = JitBytecode::OP_RET0;
            s.d_vals << quint8(0) << quint16(0);
        }
        break;
    case SynTree::R_FORI_:
    case SynTree::R_FORL_:
        s.d_op = st->d_tok.d_type == SynTree::R_FORI_ ? JitBytecode::OP_FORI : JitBytecode::OP_FORL;
        Q_ASSERT( st->d_children.size() == 3 );
        if( !fetchV( st->d_children[1], s, me, 4 ) )
            return false;
        s.d_vals << QVariant::fromValue( flatten(st->d_children[2]) );
        qWarning() << "generated bytecode FORI/FORL doesn't work with LuaJIT yet";
        break;
    case SynTree::R_JMP_:
        {
            Named* n = &me->d_firstUnused;
            s.d_op = JitBytecode::OP_JMP;
            Q_ASSERT( st->d_children.size() == 2 );
            s.d_vals << QVariant::fromValue( n );
            s.d_vals << QVariant::fromValue( flatten(st->d_children[1]) );
            // TODO: check that JMP jumps to a LOOP, but not always
        }
        break;
    default:
        //Q_ASSERT( false );
        return error(st,tr("operator not yet supported") );
        break;
    }
    l.append(s);
    return true;
}

bool Assembler::fetchV(SynTree* st, Assembler::Stmt& s, Assembler::Func* me, int count , bool lhs)
{
    Q_ASSERT( count > 0 ); // number of consecutive variables required

    const int origCount = count;

    NameSym ns = derefDesig(st,me); // only local vars
    if( ns.first == 0 )
        return false;
    createUseXref(ns.first,ns.second, me,count,lhs);
    Var* v = ns.first->toVar();
    if( v == 0 )
        return error(st,tr("argument doesn't designate a variable"));
    if( v->d_n < count )
        v->d_n = count;
    while( count )
    {
        if( v == 0 )
            return error(st, tr("%1 consecutive variables required").arg(origCount) );
        s.registerRange(v);
        v = v->d_next;
        count--;
    }
    s.d_vals << QVariant::fromValue(ns.first);
    return true;
}

bool Assembler::fetchU(SynTree* st, Assembler::Stmt& s, Assembler::Func* me, bool lhs)
{
    NameSym ns = derefDesig(st,me->d_outer, false); // only upvalue vars
    if( ns.first == 0 )
        return false;
    Var* v = ns.first->toVar();
    if( v == 0 )
        return error(st,tr("argument doesn't designate a variable"));
    createUseXref(ns.first,ns.second,me,1,lhs);
    me->resolveUpval(v);
    v->d_uv = true;
    if( lhs )
        v->d_uvRo = false;
    s.d_vals << QVariant::fromValue(ns.first);
    return true;
}

bool Assembler::fetchN(SynTree* st, Assembler::Stmt& s)
{
    st = flatten(st);
    bool ok;
    switch( st->d_tok.d_type )
    {
    case Tok_real:
        s.d_vals << st->d_tok.d_val.toDouble(&ok);
        Q_ASSERT( ok );
        return true;
    case Tok_negint:
        s.d_vals << st->d_tok.d_val.toInt(&ok);
        Q_ASSERT( ok );
        return true;
    case Tok_posint:
        s.d_vals << st->d_tok.d_val.toInt(&ok);
        Q_ASSERT( ok );
        return true;
    default:
        break;
    }
    Q_ASSERT( false );
    return false;
}

bool Assembler::fetchS(SynTree* st, Assembler::Stmt& s)
{
    st = flatten(st);
    switch( st->d_tok.d_type )
    {
    case Tok_string:
        s.d_vals << st->d_tok.d_val.mid(1,st->d_tok.d_val.size()-2); // remove ""
        return true;
    default:
        break;
    }
    Q_ASSERT( false );
    return false;
}

bool Assembler::fetchP(SynTree* st, Assembler::Stmt& s)
{
    st = flatten(st);
    switch( st->d_tok.d_type )
    {
    case Tok_nil:
        s.d_vals << QVariant();
        return true;
    case Tok_true:
        s.d_vals << true;
        return true;
    case Tok_false:
        s.d_vals << false;
        return true;
    default:
        break;
    }
    Q_ASSERT( false );
    return false;
}

bool Assembler::fetchVc(SynTree* st, Assembler::Stmt& s, Assembler::Func* me)
{
    NameSym ns = derefDesig(st,me); // onlyLocalVars is true
    if( ns.first == 0 )
        return false;
    createUseXref(ns.first,ns.second,me,1,false);
    if( Const* c = ns.first->toConst() )
    {
        s.d_vals << c->d_val;
        return true;
    }
    Var* v = ns.first->toVar();
    if( v == 0 )
        return error(st,tr("argument doesn't designate a variable"));
    s.registerRange(v);
    s.d_vals << QVariant::fromValue(ns.first);
    return true;
}

bool Assembler::fetchC(SynTree* st, Assembler::Stmt& s, Assembler::Func* me)
{
    Q_ASSERT( st != 0 && st->d_tok.d_type == SynTree::R_cname && !st->d_children.isEmpty() );
    st = flatten(st);
    Named* n = me->findAll(st->d_tok.d_val);
    if( n == 0 )
        return error(st,tr("unknown const"));
    createUseXref(n,st,me,1,false);
    if( Const* c = n->toConst() )
    {
        s.d_vals << c->d_val;
        return true;
    }else
        return error(st,tr("invalid const"));
}

bool Assembler::fetchF(SynTree* st, Assembler::Stmt& s, Assembler::Func* me)
{
    Q_ASSERT( st != 0 && st->d_tok.d_type == SynTree::R_fname && !st->d_children.isEmpty() );
    st = flatten(st);
    Named* n = me->findLocal(st->d_tok.d_val);
    if( n == 0 )
        return error(st,tr("unknown function"));
    createUseXref(n,st,me,1,false);
    if( n->isFunc() )
    {
        s.d_vals << QVariant::fromValue(n);
        return true;
    }else
        return error(st,tr("invalid function"));
}

bool Assembler::fetchVcsnp(SynTree* st, Assembler::Stmt& s, Assembler::Func* me)
{
    switch( st->d_tok.d_type )
    {
    case SynTree::R_desig:
        return fetchVc(st,s,me);
    case Tok_string:
        return fetchS(st,s);
    case SynTree::R_number:
    case Tok_real:
    case Tok_negint:
    case Tok_posint:
        return fetchN(st,s);
    case SynTree::R_primitive:
        return fetchP(st,s);
    default:
        break;
    }
    Q_ASSERT( false );
    return false;
}

bool Assembler::fetchCsnp(SynTree* st, Assembler::Stmt& s, Assembler::Func* me)
{
    switch( st->d_tok.d_type )
    {
    case SynTree::R_cname:
        return fetchC(st,s,me);
    case Tok_string:
        return fetchS(st,s);
    case SynTree::R_number:
        return fetchN(st,s);
    case SynTree::R_primitive:
        return fetchP(st,s);
    default:
        break;
    }
    Q_ASSERT( false );
    return false;
}

bool Assembler::fetchVcn(SynTree* st, Assembler::Stmt& s, Assembler::Func* me)
{
    switch( st->d_tok.d_type )
    {
    case SynTree::R_desig:
        return fetchVc(st,s,me);
    case SynTree::R_number:
        return fetchN(st,s);
    default:
        break;
    }
    Q_ASSERT( false );
    return false;
}

bool Assembler::checkJumpsAndMore(Assembler::Stmts& stmts, const Assembler::Labels& lbls, Func* f)
{
    for( int pc = 0; pc < stmts.size(); pc++ )
    {
        if( !checkTestOp( stmts, pc ) )
            return false;

        Stmt& s = stmts[pc];
        switch( s.d_op )
        {
        case JitBytecode::OP_UCLO:
        case JitBytecode::OP_FORI:
        case JitBytecode::OP_FORL:
        case JitBytecode::OP_JMP:
            {
                Q_ASSERT( !s.d_vals.isEmpty() );
                if( !s.d_vals.last().canConvert<SynTree*>() )
                {
                    Q_ASSERT( s.d_op == JitBytecode::OP_UCLO );
                    continue;
                }
                SynTree* name = s.d_vals.last().value<SynTree*>();
                Labels::const_iterator i = lbls.find(name->d_tok.d_val);
                if( i == lbls.end() )
                    return error(s.d_st,tr("label not defined") );
                s.d_vals.back() = quint16( i.value().first - ( pc + 1 ) + JitBytecode::Instruction::JumpBias );
                if( i.value().second )
                {
                    Xref* x = new Xref();
                    x->d_name = i.key();
                    x->d_kind = Xref::Label;
                    x->d_role = Xref::Ref;
                    x->d_line = name->d_tok.d_lineNr;
                    x->d_col = name->d_tok.d_colNr;
                    x->d_decl = i.value().second;
                    i.value().second->d_usedBy.append(x);
                    Q_ASSERT( f->d_xref != 0 );
                    f->d_xref->d_subs.append(x);
                }
            }
            break;
        }
    }
    Q_ASSERT( !stmts.isEmpty() );
    switch( stmts.last().d_op )
    {
    case JitBytecode::OP_RET:
    case JitBytecode::OP_RET0:
    case JitBytecode::OP_RET1:
    case JitBytecode::OP_RETM:
    case JitBytecode::OP_CALLT:
        return true;
    default:
        return error( stmts.last().d_st, tr("last statement must be return or tail call") );
    }
    return true;
}

bool Assembler::sortVars1( Assembler::Var* lhs, Assembler::Var* rhs )
{
    return lhs->d_from < rhs->d_from;
}

static void printPool( const QBitArray& pool, int len )
{
    QByteArray res;
    for( int i = 0; i < pool.size() && i < len; i++ )
    {
        if( pool.at(i) )
            res += QByteArray::number(i) + " ";
        else
            res += "_ ";
    }
    qDebug() << "pool:" << res.constData();
}

bool Assembler::allocateRegisters3(Assembler::Func* me)
{
    // collect all pending registers which are part of an array
    QSet<Var*> arrays,overlaps;
    QList<Var*> headers, all;
    Func::Names::const_iterator ni;
    for( ni = me->d_names.begin(); ni != me->d_names.end(); ++ni )
    {
        Var* v = ni.value()->toVar();
        if( v && !v->isUnused() )
            all << v;
        if( v && v->d_n > 1 && !v->isUnused() && !v->isFixed() )
        {
            Var* h = v;
            headers << h;
            int n = v->d_n;
            while( v && n > 0 )
            {
                // overlaps are only possible for Vars which belong to the same group or array
                // it is sufficient to only register the overlaping header
                if( arrays.contains(v) )
                    overlaps << h;
                else
                    arrays << v;
                v = v->d_next;
                n--;
            }
        }
    }

#ifdef _DEBUG
    std::sort( all.begin(), all.end(), sortVars1 );
#endif
#ifdef _DEBUG_
    qDebug() << "*** locals of" << me->d_name << "before allocation";
    foreach( Var* v, all )
    {
        QString str;
        if( arrays.contains(v) )
            str += "array ";
        if( overlaps.contains(v) )
            str += "overlap";
        qDebug() << QString("%1\tf/t:%2-%3\tn:%4\t %5").arg(v->d_name.constData())
                        .arg(v->d_from).arg(v->d_to).arg(v->d_n).arg(str).toUtf8().constData();
    }
#endif

    // prepare slot pool and enter all params (which are fix allocated)
    QBitArray pool(LJ_MAX_SLOTS);
    for( int i = 0; i < me->d_params.size(); i++ )
        pool.setBit(i);

    // collect all pending registers which use only one slot and handle these first
    Intervals vars;
    for( ni = me->d_names.begin(); ni != me->d_names.end(); ++ni )
    {
        Var* v = ni.value()->toVar();
        if( v && !v->isUnused() && !arrays.contains(v) && !v->isFixed() )
        {
            Q_ASSERT( v->d_n == 1 );
            if( v->d_uv )
            {
                // Do fix allocation for each slot used as upvalue
                const int slot = nextFreeSlot(pool);
                if( slot < 0 )
                    return error( me->d_st, tr("running out of slots for up values") );
                v->d_slot = slot;
            }else
                vars << Interval(v->d_from,v->d_to,v);
        }
    }

    // allocate the scalars
    if( !allocateWithLinearScan( pool, vars, 1 ) )
         return error( me->d_st, tr("function requires more slots of length 1 than supported") );
    for( int i = 0; i < vars.size(); i++ )
        static_cast<Var*>(vars[i].d_payload)->d_slot = vars[i].d_slot;
    //printPool(pool,all.size());

    // resolve overlaps
    foreach( Var* h, headers )
    {
        if( h->d_n == 0 )
            continue;
        // go rightward to find the end of a successing header without overlap
        VarList overlap;
        findOverlaps( overlap, h );
#ifdef _DEBUG_
        if( !overlap.isEmpty() )
        {
            qDebug() << "**** pending overlap cluster" << me->d_name;
            foreach( Var* v, overlap )
                qDebug() << QString("%1\tf/t:%2-%3\tn:%4").arg(v->d_name.constData())
                                .arg(v->d_from).arg(v->d_to).arg(v->d_n).toUtf8().constData();
        }
#endif
        resolveOverlaps( overlap );
#ifdef _DEBUG_
        if( !overlap.isEmpty() )
        {
            qDebug() << "**** resolved overlap cluster" << me->d_name;
            foreach( Var* v, overlap )
                qDebug() << QString("%1\tf/t:%2-%3\tn:%4").arg(v->d_name.constData())
                                .arg(v->d_from).arg(v->d_to).arg(v->d_n).toUtf8().constData();
        }
#endif
    }

    QMap<quint8,VarList> headersByN;
    foreach( Var* h, headers )
    {
        if( h->d_n ) // no longer interested in resolved overlaping headers
            headersByN[h->d_n].append(h);
    }

    QMap<quint8,VarList>::const_iterator i;
    for( i = headersByN.begin(); i != headersByN.end(); ++i )
    {
        vars.clear();
        foreach( Var* h, i.value() )
        {
            QPair<int,int> ft = h->bounds();
            vars << Interval(ft.first,ft.second,h);
        }
        if( i.key() > 4 && vars.size() == 1 && i != --headersByN.end() )
            qWarning() << "TODO: quantize array lenghts >=" << i.key();

        // allocate the scalars
        if( !allocateWithLinearScan( pool, vars, i.key() ) )
             return error( me->d_st, tr("function requires more slots of length %1 than supported").arg(i.key()) );
        // printPool(pool,all.size());
        for( int j = 0; j < vars.size(); j++ )
        {
            Var* v = static_cast<Var*>(vars[j].d_payload);
            int n = i.key();
            while( n-- > 0 )
            {
                Q_ASSERT( v != 0 );
                if( v->d_uv ) // v can be both a header or an element of the array!
                    qWarning() << me->d_name << "using slot" << v->d_name << "which is part of array as upvalue";
                v->d_slot = vars[j].d_slot++;
                v = v->d_next;
            }
        }
    }
    int frameSize;
    for( frameSize = pool.size() - 1; frameSize >= 0; frameSize-- )
    {
        if( pool.at(frameSize) )
            break;
    }
    frameSize++;
    me->d_firstUnused.d_slot = qMin(frameSize,255);


#ifdef _DEBUG_
    qDebug() << "*** locals of" << me->d_name << "after allocation";
    for( int i = 0; i < all.size(); i++ )
        qDebug() << QString("%1\tf/t:%2-%3\tn:%4\ts:%5").arg(all[i]->d_name.constData())
                    .arg(all[i]->d_from).arg(all[i]->d_to).arg(all[i]->d_n).arg(all[i]->d_slot).toUtf8().constData();

#endif

    return true;
}

bool Assembler::checkSlotOrder(const Assembler::Stmts& stmts)
{
    static const char* msg = "allocator issue: invalid slot order";
    for( int pc = 0; pc < stmts.size(); pc++ )
    {
        const Stmt& s = stmts[pc];
        switch( s.d_op )
        {
        case JitBytecode::OP_CAT:
        case JitBytecode::OP_KNIL:
            {
                const int n = s.d_vals.last().toInt();
                const Var* v = toVar(s.d_vals[s.d_vals.size()-2]);
                Q_ASSERT( n > 0 && v != 0 );
                if( !checkSlotOrder( v, n ) )
                    return error(s.d_st,tr(msg) );
            }
            break;
        case JitBytecode::OP_FORI:
        case JitBytecode::OP_FORL:
            {
                const Var* v = toVar(s.d_vals.first());
                Q_ASSERT( v != 0 );
                if( !checkSlotOrder( v, 4 ) )
                    return error(s.d_st,tr(msg) );
            }
            break;
        case JitBytecode::OP_CALL:
            {
                Q_ASSERT( s.d_vals.size() == 3 );
                const int rets = s.d_vals[1].toInt();
                const int args = s.d_vals[2].toInt();
                const Var* v = toVar(s.d_vals[0]);
                const int n = qMax( rets, args + 1 );
                if( !checkSlotOrder( v, n ) )
                    return error(s.d_st,tr(msg) );
            }
            break;
        case JitBytecode::OP_CALLT:
            {
                Q_ASSERT( s.d_vals.size() == 2 );
                const int args = s.d_vals[1].toInt();
                const Var* v = toVar(s.d_vals[0]);
                const int n = args + 1;
                if( !checkSlotOrder( v, n ) )
                    return error(s.d_st,tr(msg) );
            }
            break;
        case JitBytecode::OP_RET:
            {
                const int n = s.d_vals[1].toInt();
                const Var* v = toVar(s.d_vals[0]);
                if( !checkSlotOrder( v, n ) )
                    return error(s.d_st,tr(msg) );
            }
            break;
        }
    }
    return true;
}

bool Assembler::checkTestOp(const Assembler::Stmts& stmts, int pc)
{
    Q_ASSERT( pc < stmts.size() );
    switch( stmts[pc].d_op )
    {
    case JitBytecode::OP_ISLT:
    case JitBytecode::OP_ISGE:
    case JitBytecode::OP_ISLE:
    case JitBytecode::OP_ISGT:
    case JitBytecode::OP_ISEQV:
    case JitBytecode::OP_ISNEV:
    case JitBytecode::OP_ISEQS:
    case JitBytecode::OP_ISNES:
    case JitBytecode::OP_ISEQN:
    case JitBytecode::OP_ISNEN:
    case JitBytecode::OP_ISEQP:
    case JitBytecode::OP_ISNEP:
    case JitBytecode::OP_ISTC:
    case JitBytecode::OP_ISFC:
    case JitBytecode::OP_IST:
    case JitBytecode::OP_ISF:
        if( pc == stmts.size() - 1 || stmts[pc+1].d_op != JitBytecode::OP_JMP )
            return error( stmts[pc].d_st, tr("expecting JMP after comparison or test ops"));
        break;
    }
    return true;
}

bool Assembler::generateCode(Func* f, const Stmts& stmts)
{
    // TODO: check for read-only upvals
    d_comp.setUpvals(f->getUpvals());
    d_comp.setVarNames(f->getVarNames());

    for( int pc = 0; pc < stmts.size(); pc++ )
    {
        Stmt s = stmts[pc];
        const JitBytecode::Op op = JitBytecode::Op(s.d_op);
        switch( op )
        {
        case JitBytecode::OP_CAT:
            {
                Q_ASSERT( s.d_vals.size() == 3 );
                const int n = s.d_vals[2].toUInt();
                // replace C count by slot number relative to B
                const int b = toValue(f, JitBytecode::typeBFromOp(s.d_op), s.d_vals[1] );
                s.d_vals[2] = b + n - 1;
            }
            break;
        case JitBytecode::OP_KNIL:
            {
                Q_ASSERT( s.d_vals.size() == 2 );
                const int n = s.d_vals[1].toUInt();
                // replace C count by slot number relative to A
                const int a = toValue(f, JitBytecode::typeAFromOp(s.d_op), s.d_vals[0] );
                s.d_vals[1] = a + n - 1;
            }
            break;
        case JitBytecode::OP_RET0:
        case JitBytecode::OP_RET1:
        case JitBytecode::OP_RET:
            // Operand D is one plus the number of results to return.
            s.d_vals[1] = s.d_vals[1].toUInt() + 1;
            break;
        case JitBytecode::OP_CALL:
            // Operand C is one plus the number of fixed arguments.
            s.d_vals[2] = s.d_vals[2].toUInt() + 1;
            // Operand B is one plus the number of return values (MULTRES is not supported)
            s.d_vals[1] = s.d_vals[1].toUInt() + 1;
            break;
        case JitBytecode::OP_CALLT:
            // Operand C is one plus the number of fixed arguments.
            s.d_vals[1] = s.d_vals[1].toUInt() + 1;
            break;
        default:
            break;
        }

        if( JitBytecode::formatFromOp( s.d_op ) == JitBytecode::ABC )
        {
            Q_ASSERT( s.d_vals.size() == 3 );
            const int a = toValue(f, JitBytecode::typeAFromOp(s.d_op), s.d_vals[0] );
            const int b = toValue(f, JitBytecode::typeBFromOp(s.d_op), s.d_vals[1] );
            const int c = toValue(f, JitBytecode::typeCdFromOp(s.d_op), s.d_vals[2] );
            if( a < 0 || b < 0 || c < 0 )
                return error( s.d_st, tr("internal arument error") );
            d_comp.addAbc( op, a, b, c, s.d_st->d_tok.d_lineNr );
        }else
        {
            Q_ASSERT( s.d_vals.size() == 2 );
            const int a = toValue(f, JitBytecode::typeAFromOp(s.d_op), s.d_vals[0] );
            const int d = toValue(f, JitBytecode::typeCdFromOp(s.d_op), s.d_vals[1] );
            if( a < 0 || d < 0 )
                return error( s.d_st, tr("internal arument error") );
            d_comp.addAd( op, a, d, s.d_st->d_tok.d_lineNr );
        }
    }
    return true;
}

void Assembler::createUseXref(Assembler::Named* n, SynTree* st, Assembler::Func* f, int count, bool lhs)
{
    if( d_createXref )
    {
        while( n != 0 && count-- > 0 )
        {
            Xref* x = new Xref();
            x->d_name = n->d_name;
            x->d_role = lhs ? Xref::Lhs : Xref::Rhs;
            x->d_kind = n->isConst() ? Xref::Const : ( n->isVar() ? Xref::Var : Xref::Func );
            x->d_line = st->d_tok.d_lineNr;
            x->d_col = st->d_tok.d_colNr;
            Q_ASSERT( n->d_xref != 0 );
            x->d_decl = n->d_xref;
            n->d_xref->d_usedBy.append(x);
            Q_ASSERT( f->d_xref != 0 );
            f->d_xref->d_subs.append(x);
            Var* v = n->toVar();
            if( v )
                n = v->d_next;
            else
                n = 0;
        }
    }
}

void Assembler::createDeclXref(Assembler::Named* n, SynTree* st, Assembler::Func* f)
{
    if( d_createXref )
    {
        Xref* x = new Xref();
        n->d_xref = x;
        x->d_name = n->d_name;
        x->d_role = Xref::Decl;
        x->d_kind = n->isConst() ? Xref::Const : ( n->isVar() ? Xref::Var : Xref::Func );
        x->d_line = st->d_tok.d_lineNr;
        x->d_col = st->d_tok.d_colNr;
        Q_ASSERT( f->d_xref != 0 );
        f->d_xref->d_subs.append(x);
    }
}

int Assembler::toValue(Assembler::Func* f, JitBytecode::Instruction::FieldType t, const QVariant& v)
{
    switch( t )
    {
    case JitBytecode::Instruction::_var:
    case JitBytecode::Instruction::_dst:
    case JitBytecode::Instruction::_rbase:
    case JitBytecode::Instruction::_base:
        if( Var* vv = toVar(v) )
            return vv->d_slot;
        else if( JitBytecode::isNumber(v) )
        {
            const qint32 slot = v.toInt();
            if( slot < 0 || slot > LJ_MAX_SLOTS )
                return -1;
            return slot;
        }
        break;
    case JitBytecode::Instruction::_str:
        if( JitBytecode::isString(v) )
            return d_comp.getConstSlot(v); // TODO negate
        break;
    case JitBytecode::Instruction::_num:
        if( JitBytecode::isNumber(v) )
            return d_comp.getConstSlot(v);
        break;
    case JitBytecode::Instruction::_pri:
        if( v.isNull() )
            return 0;
        else if( v.type() == QVariant::Bool )
        {
            if( v.toBool() )
                return 2;
            else
                return 1;
        }
        break;
    case JitBytecode::Instruction::_cdata:
        return d_comp.getConstSlot(v); // TODO negate
    case JitBytecode::Instruction::_lit:
    case JitBytecode::Instruction::_jump:
        if( JitBytecode::isNumber(v) )
        {
            qint32 i = v.toInt();
            if( i >= 0 && i <= USHRT_MAX )
                return i;
        }
        break;
    case JitBytecode::Instruction::_lits:
        if( JitBytecode::isNumber(v) )
        {
            qint32 i = v.toInt();
            if( i >= SHRT_MIN && i <= SHRT_MAX )
                return (quint16)i;
        }
        break;
    case JitBytecode::Instruction::_uv:
        if( Var* vv = toVar(v) )
        {
            return f->resolveUpval(vv,false);
        }else if( JitBytecode::isNumber(v) )
            return v.toUInt();
        break;
    case JitBytecode::Instruction::_func:
        if( Named* n = v.value<Named*>() )
        {
            Func* f = n->toFunc();
            Q_ASSERT( f );
            return f->d_id;
        }
        break;
    case JitBytecode::Instruction::_tab:
        if( v.canConvert<JitBytecode::ConstTable>() )
            return d_comp.getConstSlot(v); // TODO negate
        break;
    default:
        return 0;
    }

    return -1;
}

Assembler::NameSym Assembler::derefDesig(SynTree* st, Assembler::Func* me, bool onlyLocalVars)
{
    Q_ASSERT( st != 0 && st->d_tok.d_type == SynTree::R_desig && !st->d_children.isEmpty() );

    Named* sym = 0;
    int vnameIdx = 0;
    Func* func = me;

    if( st->d_children.first()->d_tok.d_type == SynTree::R_fname )
    {
        Q_ASSERT( st->d_children.size() >= 3 &&
                  st->d_children[1]->d_tok.d_type == Tok_Dot && st->d_children[2]->d_tok.d_type == SynTree::R_vname );
        SynTree* name = flatten( st->d_children.first() );
        if( me->d_name != name->d_tok.d_val )
        {
            Named* t = me->findAll(name->d_tok.d_val);
            if( t )
                func = t->toFunc();
        }
        if( func == 0 )
        {
            error(name,tr("name doesn't designate a function"));
            return NameSym();
        }
        vnameIdx = 2;
    }

    Q_ASSERT( st->d_children[vnameIdx]->d_tok.d_type == SynTree::R_vname );
    bool isLocal;
    SynTree* symName = flatten( st->d_children[vnameIdx]);
    sym = func->findAll( symName->d_tok.d_val, &isLocal );

    if( sym == 0 )
    {
        error(st->d_children[vnameIdx],tr("name is not defined"));
        return NameSym();
    }

    if( sym->isVar() && onlyLocalVars &&  !isLocal )
    {
        error( st->d_children[vnameIdx],tr("cannot use non-local variables here"));
        return NameSym();
    }

#ifdef _SUPPORT_ARRAYS_
    vnameIdx++;
    if( vnameIdx < st->d_children.size() )
    {
        // there is an array selector
        Q_ASSERT( vnameIdx + 2 < st->d_children.size() && st->d_children[vnameIdx]->d_tok.d_type == Tok_Lbrack &&
                  st->d_children[vnameIdx+1]->d_tok.d_type == SynTree::R_integer &&
                st->d_children[vnameIdx+2]->d_tok.d_type == Tok_Rbrack );
        Q_ASSERT( !st->d_children[vnameIdx+1]->d_children.isEmpty() );
        int off = st->d_children[vnameIdx+1]->d_children.first()->d_tok.d_val.toInt();
        if( Var* v = sym->toVar() )
        {
            if( off == 0 )
                return v;
            while( v && off > 0 )
            {
                v = v->d_next;
                off--;
            }
            while( v && off < 0 )
            {
                v = v->d_prev;
                off++;
            }
            if( v == 0 )
            {
                error( st->d_children[vnameIdx+1], tr("index out of range") );
                return 0;
            }
            return v;
        }else if( Arr* a = sym->toArr() )
        {
            if( off < 0 || off >= a->d_elems.size() )
            {
                error( st->d_children[vnameIdx+1], tr("index out of range") );
                return 0;
            }
            return a->d_elems[off];
        }else
        {
            error( st->d_children[vnameIdx-1],tr("object cannot be used as array") );
            return 0;
        }
    }
#endif
    return NameSym(sym,symName);
}

SynTree*Assembler::findFirstChild(const SynTree* st, int type, int startWith)
{
    if( st == 0 )
        return 0;
    for( int i = startWith; i < st->d_children.size(); i++ )
    {
        SynTree* sub = st->d_children[i];
        if( sub->d_tok.d_type == type )
            return sub;
    }
    if( st->d_tok.d_type == type )
        return const_cast<SynTree*>(st);
    return 0;
}

SynTree*Assembler::flatten(SynTree* st, int stopAt)
{
    if( st == 0 )
        return 0;
    while( st->d_children.size() == 1 && ( stopAt == 0 || st->d_tok.d_type != stopAt ) )
        st = st->d_children.first();
    return st;
}

bool Assembler::error(SynTree* st, const QString& msg)
{
    d_errs->error( Errors::Semantics, st, msg );
    return false;
}

Assembler::Func::~Func()
{
    Names::iterator i;
    for( i = d_names.begin(); i != d_names.end(); ++i )
        delete i.value();
}

Assembler::Named*Assembler::Func::findAll(const QByteArray& name, bool* isLocal) const
{
    Names::const_iterator i = d_names.find(name);
    if( i != d_names.end() )
    {
        if( isLocal )
            *isLocal = true;
        return i.value();
    }
    if( d_outer )
    {
        if( isLocal )
            *isLocal = false;
        return d_outer->findAll(name);
    }
    return 0;
}

Assembler::Named*Assembler::Func::findLocal(const QByteArray& name) const
{
    Names::const_iterator i = d_names.find(name);
    if( i != d_names.end() )
        return i.value();
    else
        return 0;
}

int Assembler::Func::resolveUpval(Assembler::Var* v, bool recursive)
{
    if( v->d_func == this )
        return -1;
    Upvals::const_iterator i = d_upvals.find(v);
    if( i != d_upvals.end() )
        return i.value();
    const int nr = d_upvals.size();
    d_upvals[ v ] = nr;
    if( recursive && d_outer && d_outer != v->d_func )
        d_outer->resolveUpval(v,recursive);
    return nr;
}

JitComposer::UpvalList Assembler::Func::getUpvals() const
{
    JitComposer::UpvalList res( d_upvals.size() );

    Upvals::const_iterator i;
    for( i = d_upvals.begin(); i != d_upvals.end(); ++i )
    {
        Q_ASSERT( d_outer );
        JitComposer::Upval u;
        u.d_name = i.key()->d_name;
        if( i.key()->d_uvRo )
            u.d_isRo = true;
        if( i.key()->d_func == d_outer )
        {
            u.d_uv = i.key()->d_slot;
            u.d_isLocal = true;
        }else
            u.d_uv = d_outer->resolveUpval(i.key(), false );
        res[i.value()] = u;
    }
    return res;
}

JitComposer::VarNameList Assembler::Func::getVarNames() const
{
    JitComposer::VarNameList res( d_firstUnused.d_slot );
    Names::const_iterator i;
    for( i = d_names.begin(); i != d_names.end(); ++i )
    {
        Var* v = 0;
        if( ( v = i.value()->toVar() ) && !v->isUnused() )
        {
            JitComposer::VarName& n = res[v->d_slot];
            n.d_name = v->d_name;
            n.d_from = v->d_from;
            n.d_to = v->d_to;
        }
    }
    return res;
}

Assembler::Arr::~Arr()
{
    foreach( Var* v, d_elems )
        delete v;
}

void Assembler::Stmt::registerRange(Assembler::Var* v)
{
    Q_ASSERT( v );
    if( v->d_from == 0 && v->d_to == 0 )
    {
        v->d_to = v->d_from = d_pc+1;
    }else if( v->d_to < d_pc + 1 )
        v->d_to = d_pc + 1;
}


bool Assembler::Var::isUnused() const
{
    return d_from == 0 && d_to == 0 && !d_uv;
}

bool Assembler::Var::isFixed() const
{
    return d_from == 0 && d_to != 0;
}

QPair<int, int> Assembler::Var::bounds() const
{
    int from = d_from;
    int to = d_to;
    Var* v = d_next;
    int n = d_n - 1;
    while( v && n > 0 )
    {
        if( v->d_from < from )
            from = v->d_from;
        if( v->d_to > to )
            to = v->d_to;
        n--;
        v = v->d_next;
    }
    return qMakePair(from,to);
}

const char* Assembler::Xref::s_kind[] =
{
    "Func","Var","Const","Label"
};

const char* Assembler::Xref::s_role[] =
{
    "Decl","Lhs","Rhs","Ref"
};

Assembler::Xref::Xref():d_line(0),d_col(0),d_kind(0),d_role(0),d_decl(0)
{

}

Assembler::Xref::~Xref()
{
    foreach( Xref* sub, d_subs )
        delete sub;
}
