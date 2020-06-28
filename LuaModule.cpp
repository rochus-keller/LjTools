/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Lua parser library.
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

#include "LuaModule.h"
#include "LuaLexer.h"
#include "LuaParser.h"
#include "LjasErrors.h"
#include <QtDebug>
using namespace Lua;

const char* Module::Thing::s_tagName[] = {
    "Thing", "Variable", "Block", "Function", "Global", "GlobalSym", "SymbolUse"
};

static void dumpTree( Lua::SynTree* node, int level = 0)
{
    QByteArray str;
    if( node->d_tok.d_type == Lua::Tok_Invalid )
        level--;
    else if( node->d_tok.d_type < Lua::SynTree::R_First )
    {
        if( Lua::tokenTypeIsKeyword( node->d_tok.d_type ) )
            str = Lua::tokenTypeString(node->d_tok.d_type);
        else if( node->d_tok.d_type > Lua::TT_Specials )
            str = QByteArray("\"") + node->d_tok.d_val + QByteArray("\"");
        else
            str = QByteArray("\"") + tokenTypeString(node->d_tok.d_type) + QByteArray("\"");

    }else
        str = Lua::SynTree::rToStr( node->d_tok.d_type );
    if( !str.isEmpty() )
    {
        str += QByteArray("\t") /* + QFileInfo(node->d_tok.d_sourcePath).baseName().toUtf8() +
                ":" */ + QByteArray::number(node->d_tok.d_lineNr) +
                ":" + QByteArray::number(node->d_tok.d_colNr);
        QByteArray ws;
        for( int i = 0; i < level; i++ )
            ws += "|  ";
        str = ws + str;
        qDebug() << str.data();
    }
    foreach( Lua::SynTree* sub, node->d_children )
        dumpTree( sub, level + 1 );
}

static void dumpTree( Lua::Module::Thing* node, int level = 0)
{
    QByteArray str;
    if( node->d_tok.d_type == Lua::Tok_Invalid )
        level--;
    else
    {
        str = Lua::Module::Thing::s_tagName[ node->getTag() ];
        str += " ";
        if( node->d_tok.d_type < Lua::SynTree::R_First )
        {
            if( Lua::tokenTypeIsKeyword( node->d_tok.d_type ) )
                str += Lua::tokenTypeString(node->d_tok.d_type);
            else if( node->d_tok.d_type > Lua::TT_Specials )
                str += QByteArray("\"") + node->d_tok.d_val + QByteArray("\"");
            else
                str += QByteArray("\"") + tokenTypeString(node->d_tok.d_type) + QByteArray("\"");

        }else
            str += Lua::SynTree::rToStr( node->d_tok.d_type );
    }
    if( !str.isEmpty() )
    {
        str += QByteArray("\t") /* + QFileInfo(node->d_tok.d_sourcePath).baseName().toUtf8() +
                ":" */ + QByteArray::number(node->d_tok.d_lineNr) +
                ":" + QByteArray::number(node->d_tok.d_colNr);
        QByteArray ws;
        for( int i = 0; i < level; i++ )
            ws += "|  ";
        str = ws + str;
        qDebug() << str.data();
    }
    Lua::Module::Scope* scope = node->isScope() ? static_cast<Lua::Module::Scope*>(node) : 0;
    if( scope )
    {
        foreach( const Lua::Module::Ref<Lua::Module::Thing>& sub, scope->d_locals )
            dumpTree( sub.data(), level + 1 );
        foreach( const Lua::Module::Ref<Lua::Module::Block>& sub, scope->d_stats )
            dumpTree( sub.data(), level + 1 );
    }
}

Module::Module(QObject *parent) : QObject(parent),d_fcache(0),d_err(0)
{

}

bool Module::parse(const QString& path, bool clearGlobal)
{
    if( d_global.isNull() )
    {
        d_global = new Global();
        initBuiltIns(d_global.data());
    }else if( clearGlobal )
    {
        d_global->clear();
        initBuiltIns(d_global.data());
    }
    d_nonLocals.clear();
    d_path = path;
    const quint32 before = d_err ? d_err->getErrCount() : 0;
    Lexer lex;
    lex.setErrors(d_err);
    lex.setCache(d_fcache);
    lex.setIgnoreComments(false);
    lex.setPackComments(true);
    lex.setStream( path );
    Parser p(&lex,d_err);
    p.RunParser();
    bool hasError = ( d_err->getErrCount() - before ) != 0;
    if( !hasError )
    {
        //dumpTree(&p.d_root);
        analyze( &p.d_root );
#if 0
        dumpTree( d_topChunk.data() );
        for( int i = 0; i < d_nonLocals.size(); i++ )
            dumpTree(d_nonLocals[i].data());
#endif
    }
    if( d_err )
        return !hasError;
    else
        return true;
}

void Module::initBuiltIns(Global* g)
{
    addBuiltInSym(g,"_G");
    addBuiltInSym(g,"_VERSION");
    addBuiltInSym(g,"assert");
    addBuiltInSym(g,"collectgarbage");
    addBuiltInSym(g,"dofile");
    addBuiltInSym(g,"error");
    addBuiltInSym(g,"getfenv");
    addBuiltInSym(g,"getmetatable");
    addBuiltInSym(g,"ipairs");
    addBuiltInSym(g,"load");
    addBuiltInSym(g,"loadfile");
    addBuiltInSym(g,"loadstring");
    addBuiltInSym(g,"module");
    addBuiltInSym(g,"next");
    addBuiltInSym(g,"pairs");
    addBuiltInSym(g,"pcall");
    addBuiltInSym(g,"print");
    addBuiltInSym(g,"rawequal");
    addBuiltInSym(g,"rawget");
    addBuiltInSym(g,"rawset");
    addBuiltInSym(g,"require");
    addBuiltInSym(g,"select");
    addBuiltInSym(g,"setfenv");
    addBuiltInSym(g,"setmetatable");
    addBuiltInSym(g,"tonumber");
    addBuiltInSym(g,"tostring");
    addBuiltInSym(g,"type");
    addBuiltInSym(g,"unpack");
    addBuiltInSym(g,"xpcall");
}

void Module::addBuiltInSym(Global* g, const QByteArray& name)
{
    GlobalSym* sym = new GlobalSym(true);
    sym->d_tok.d_type = Tok_Name;
    sym->d_tok.d_val = Lexer::getSymbol(name);
    g->d_names.insert(sym->d_tok.d_val.constData(),sym);
}

void Module::analyze(SynTree* st)
{
    switch( st->d_tok.d_type )
    {
    case Tok_Invalid:
        Q_ASSERT( !st->d_children.isEmpty() );
        analyze( st->d_children.first() );
        break;
    case SynTree::R_chunk:
        {
            d_topChunk = new Block();
            // d_topChunk->d_outer is 0!
            d_topChunk->d_tok = st->d_tok;
            chunk( st, d_topChunk.data() );
        }
        break;
    default:
        Q_ASSERT( false );
        break;
    }
}

void Module::chunk(SynTree* st, Scope* chunk)
{
    foreach( SynTree* sub, st->d_children )
    {
        Q_ASSERT( sub->d_tok.d_type == SynTree::R_stat || sub->d_tok.d_type == SynTree::R_laststat );
        stat( sub, chunk );
    }
}

void Module::stat(SynTree* sta, Scope* scope)
{
    foreach( SynTree* st, sta->d_children )
    {
        switch( st->d_tok.d_type )
        {
        case SynTree::R_assigOrCall_:
            Q_ASSERT( !st->d_children.isEmpty() && st->d_children.first()->d_tok.d_type == SynTree::R_prefixexp );
            if( st->d_children.size() == 2 )
            {
                // assig
                prefixexp( st->d_children.first(), scope, true );
                Q_ASSERT( st->d_children.last()->d_tok.d_type == SynTree::R_assignment_ );
                assignment( st->d_children.last(), scope );
            }else
                // call
                prefixexp( st->d_children.first(), scope, false );
            break;
        case SynTree::R_gfuncdecl_:
            gfuncdecl(st,scope);
            break;
        case SynTree::R_forstat_:
            forstat(st,scope);
            break;
        case SynTree::R_localdecl_:
            localdecl(st,scope);
            break;
        default:
            foreach( SynTree* sub, st->d_children )
            {
                switch( sub->d_tok.d_type )
                {
                case SynTree::R_exp:
                    exp(sub,scope);
                    break;
                case SynTree::R_block:
                    {
                        Block* b = new Block();
                        b->d_tok = st->d_tok;
                        b->d_outer = scope;
                        scope->d_stats.append(b);
                        Q_ASSERT( !sub->d_children.isEmpty() && sub->d_children.first()->d_tok.d_type == SynTree::R_chunk );
                        chunk( sub->d_children.first(), b );
                    }
                    break;
                default:
                    break;
                }
            }
            break;
        }
    }
}

void Module::localdecl(SynTree* st, Module::Scope* scope)
{
    Q_ASSERT( st->d_children.size() == 2 && st->d_children.first()->d_tok.d_type == Tok_local );
    switch( st->d_children.last()->d_tok.d_type )
    {
    case SynTree::R_lfuncdecl_:
        lfuncdecl(st->d_children.last(),scope);
        break;
    case SynTree::R_lvardecl_:
        lvardecl(st->d_children.last(),scope);
        break;
    default:
        Q_ASSERT( false );
        break;
    }
}

void Module::lvardecl(SynTree* st, Module::Scope* scope)
{
    Q_ASSERT( st->d_children.size() >= 1 );
    Q_ASSERT( st->d_children.first()->d_tok.d_type == SynTree::R_namelist );
    if( st->d_children.size() > 1 )
    {
        Q_ASSERT( st->d_children[1]->d_tok.d_type == Tok_Eq );
        explist( st->d_children.last(), scope );
    }
    namelist( st->d_children.first(), scope );
}

void Module::explist(SynTree* st, Module::Scope* scope)
{
    Q_ASSERT( !st->d_children.isEmpty() );
    foreach( SynTree* sub, st->d_children )
        exp(sub,scope);
}

void Module::exp(SynTree* st, Module::Scope* scope)
{
    // also non-exp are covered here
    foreach( SynTree* sub, st->d_children )
    {
        switch( sub->d_tok.d_type )
        {
        // no, we only call use() for the first ident in a desig chain; there is no way to
        // uniquely dereference .name in Lua, so there is no use to mark it in the code
//        case Tok_Name:
//            use(sub,scope,false);
//            break;
        case SynTree::R_lambdecl_:
            lambdecl(sub,scope);
            break;
        case SynTree::R_prefixexp:
            prefixexp(sub,scope,false);
            break;
        case SynTree::R_exp:
        default:
            exp(sub,scope);
            break;
        }
    }
}

void Module::lfuncdecl(SynTree* st, Module::Scope* scope)
{
    Q_ASSERT( st->d_children.size() == 3 && st->d_children.first()->d_tok.d_type == Tok_function &&
              st->d_children[1]->d_tok.d_type == Tok_Name &&
            st->d_children.last()->d_tok.d_type == SynTree::R_funcbody );
    Function* fun = new Function();
    fun->d_outer = scope;
    fun->d_tok = st->d_children[1]->d_tok;
    fun->d_kind = Function::Local;
    scope->d_names.insert( fun->d_tok.d_val.constData(), fun );
    scope->d_locals.append(fun);
    funcbody(st->d_children.last(),fun);
}

void Module::funcbody(SynTree* st, Module::Function* fun)
{
    Q_ASSERT( st->d_children.size() >= 4 && st->d_children.first()->d_tok.d_type == Tok_Lpar &&
              st->d_children.last()->d_tok.d_type == Tok_end );
    int rpar = 1;
    if( st->d_children[1]->d_tok.d_type == SynTree::R_parlist )
    {
        SynTree* pl = st->d_children[1];
        Q_ASSERT( !pl->d_children.isEmpty() );
        if( pl->d_children.first()->d_tok.d_type == SynTree::R_namelist )
        {
            SynTree* nl = pl->d_children.first();
            fun->d_parCount = nl->d_children.size();
            namelist(nl,fun);
        } // ignore '...'
        rpar++;
    }
    Q_ASSERT( st->d_children[rpar]->d_tok.d_type == Tok_Rpar &&
              st->d_children[rpar+1]->d_tok.d_type == SynTree::R_block );
    SynTree* block = st->d_children[rpar+1];
    Q_ASSERT( !block->d_children.isEmpty() && block->d_children.first()->d_tok.d_type == SynTree::R_chunk );
    chunk( block->d_children.first(), fun );
}

void Module::namelist(SynTree* nl, Module::Scope* scope)
{
    foreach( SynTree* n, nl->d_children )
    {
        Variable* v = new Variable();
        v->d_tok = n->d_tok;
        scope->d_locals.append(v);
        scope->d_names.insert(n->d_tok.d_val.constData(),v);
    }
}

void Module::forstat(SynTree* st, Module::Scope* scope)
{
    Q_ASSERT( st->d_children.size() >= 7 && st->d_children.first()->d_tok.d_type == Tok_for );
    Block* b = new Block();
    b->d_outer = scope;
    b->d_tok = st->d_tok;
    scope->d_stats.append(b);
    foreach( SynTree* sub, st->d_children )
    {
        switch( sub->d_tok.d_type )
        {
        case SynTree::R_exp:
            exp(sub,scope);
            break;
        case SynTree::R_explist:
            explist(sub,scope);
            break;
        case SynTree::R_block:
            Q_ASSERT( !sub->d_children.isEmpty() && sub->d_children.first()->d_tok.d_type == SynTree::R_chunk );
            chunk( sub->d_children.first(), b );
            break;
        case Tok_Name:
            {
                Variable* v = new Variable();
                v->d_tok = sub->d_tok;
                b->d_locals.append(v);
                b->d_names.insert(sub->d_tok.d_val.constData(),v);
            }
            break;
        case Tok_end:
        case Tok_Eq:
        case Tok_in:
        case Tok_do:
        case Tok_for:
            break;
        default:
            qDebug() << "Module::stat::sub" << SynTree::rToStr( sub->d_tok.d_type );
            Q_ASSERT( false );
            break;
        }
    }
}

void Module::gfuncdecl(SynTree* st,Scope* scope)
{
    Q_ASSERT( st->d_children.size() == 3 && st->d_children[1]->d_tok.d_type == SynTree::R_funcname
            && st->d_children.last()->d_tok.d_type == SynTree::R_funcbody );

    Function* fun = new Function();
    fun->d_outer = scope;
    SynTree* names = st->d_children[1];
    if( names->d_children.size() == 1 )
    {
        fun->d_tok = names->d_children.first()->d_tok;
        fun->d_kind = Function::Global;
        // true global function
        if( d_global->d_names.contains(fun->d_tok.d_val.constData()) )
            d_err->warning(Ljas::Errors::Semantics,fun->d_tok.d_sourcePath,fun->d_tok.d_lineNr,fun->d_tok.d_colNr,
                           tr("overwriting existing global variable '%1'").arg(fun->d_tok.d_val.constData()) );
        d_global->d_names.insert( fun->d_tok.d_val.constData(), fun );
        d_nonLocals.append(fun);
    }else
    {
        fun->d_tok = names->d_tok;
        // lambda assigned to table field
        QByteArrayList str;
        foreach( SynTree* name, names->d_children )
        {
            switch(name->d_tok.d_type)
            {
            case Tok_Name:
                str << name->d_tok.d_val;
                break;
            case SynTree::R_desig_:
                Q_ASSERT( name->d_children.size() == 2 && name->d_children.last()->d_tok.d_type == Tok_Name );
                str << name->d_children.last()->d_tok.d_val;
                break;
            }
        }
        fun->d_tok.d_val = str.join('.');
        fun->d_tok.d_type = Tok_Designator;
        fun->d_kind = Function::NonLocal;
        d_nonLocals.append(fun);
    }
    funcbody(st->d_children.last(),fun);
}

void Module::prefixexp(SynTree* st, Module::Scope* scope, bool lhs)
{
    foreach( SynTree* sub, st->d_children )
    {
        if( sub->d_tok.d_type == Tok_Name )
            use(sub,scope,lhs);
        else
            exp(sub,scope);
    }
}

void Module::assignment(SynTree* st, Module::Scope* scope)
{
    Q_ASSERT( st->d_children.size() >= 2 && st->d_children.last()->d_tok.d_type == SynTree::R_explist );
    explist(st->d_children.last(),scope);
    foreach( SynTree* sub, st->d_children )
    {
        if( sub->d_tok.d_type == SynTree::R_prefixexp )
            prefixexp(sub,scope,true);
    }
}

void Module::use(SynTree* st, Module::Scope* scope, bool lhs)
{
    Q_ASSERT( st->d_tok.d_type == Tok_Name );
    Thing* decl = scope->find(st->d_tok.d_val.constData() );
    bool implicitDecl = false;
    if( decl == 0 )
    {
        decl = d_global->find(st->d_tok.d_val.constData());
        if( decl == 0 )
        {
            GlobalSym* sym = new GlobalSym();
            sym->d_tok = st->d_tok;
            d_global->d_names.insert(st->d_tok.d_val.constData(),sym);
            implicitDecl = true;
            d_err->warning(Ljas::Errors::Semantics,st->d_tok.d_sourcePath,st->d_tok.d_lineNr,st->d_tok.d_colNr,
                           tr("implicit global declaration '%1'").arg(st->d_tok.d_val.constData()) );
        }
    }
    if( decl )
    {
        SymbolUse* s = new SymbolUse();
        s->d_tok = st->d_tok;
        s->d_lhs = lhs;
        s->d_implicitDecl = implicitDecl;
        s->d_sym = decl;
        decl->d_uses.append(s);
        scope->d_refs.append(s);
    }
}

void Module::lambdecl(SynTree* st, Module::Scope* scope)
{
    Q_ASSERT( st->d_children.size() == 2 && st->d_children.last()->d_tok.d_type == SynTree::R_funcbody );
    Function* fun = new Function();
    fun->d_outer = scope;
    fun->d_tok = st->d_tok;
    fun->d_kind = Function::NonLocal;
    funcbody(st->d_children.last(),fun);
    d_nonLocals.append(fun);
}

void Module::Global::clear()
{
    d_names.clear(); // actually the only member used in Global
    d_refs.clear();
    d_stats.clear();
    d_locals.clear();
}

Module::Thing*Module::Scope::find(const char* name) const
{
    Names::const_iterator i = d_names.find(name);
    if( i != d_names.end() )
        return i.value().data();
    else if( d_outer )
        return d_outer->find(name);
    else
        return 0;
}
