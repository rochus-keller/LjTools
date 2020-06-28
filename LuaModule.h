#ifndef LUAMODULE_H
#define LUAMODULE_H

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

#include <QObject>
#include <QSharedData>
#include <LjTools/LuaSynTree.h>

namespace Ljas
{
class Errors;
class FileCache;
}

namespace Lua
{
    class Module : public QObject
    {
    public:
        struct SymbolUse;
        struct Scope;

        template <class T>
        struct Ref : public QExplicitlySharedDataPointer<T>
        {
            Ref(T* t = 0):QExplicitlySharedDataPointer<T>(t) {}
            bool isNull() const { return QExplicitlySharedDataPointer<T>::constData() == 0; }
        };

        struct Thing : public QSharedData
        {
            enum Tag { T_Thing, T_Variable, T_Block, T_Function, T_Global, T_GlobalSym, T_SymbolUse };
            static const char* s_tagName[];
            Token d_tok;
            QList< Ref<SymbolUse> > d_uses;
            virtual int getTag() const { return T_Thing; }
            virtual bool isScope() const { return false; }
            virtual bool isLhsUse() const { return false; }
            virtual bool isImplicitDecl() const { return false; }
        };

        struct Variable : public Thing
        {
            // Declaration
            Scope* d_owner;
            Variable():d_owner(0) {}
            int getTag() const { return T_Variable; }
        };

        struct SymbolUse : public Thing
        {
            Thing* d_sym;
            bool d_lhs;
            bool d_implicitDecl;
            SymbolUse():d_sym(0),d_implicitDecl(false),d_lhs(false){}
            int getTag() const { return T_SymbolUse; }
            bool isLhsUse() const { return d_lhs; }
            bool isImplicitDecl() const { return d_implicitDecl; }
        };

        struct Block;

        struct Scope : public Thing
        {
            Scope* d_outer;
            typedef QHash<const char*,Ref<Thing> > Names;
            Names d_names;
            QList< Ref<Thing> > d_locals; // vars and funcs
            QList< Ref<Block> > d_stats;
            QList< Ref<SymbolUse> > d_refs;
            Scope():d_outer(0){}
            Thing* find( const char* name ) const;
            bool isScope() const { return true; }
        };

        struct Block : public Scope
        {
            int getTag() const { return T_Block; }
        };

        struct Function : public Scope
        {
            enum Kind { Local, NonLocal, Global };
            quint16 d_parCount; // param vars are in d_locals
            quint8 d_kind;
            Function():d_parCount(0),d_kind(Local){}
            int getTag() const { return T_Function; }
        };

        struct Global : public Scope
        {
            void clear();
            int getTag() const { return T_Global; }
        };

        struct GlobalSym : public Thing
        {
            bool d_builtIn;
            GlobalSym(bool builtIn = false):d_builtIn(builtIn){}
            int getTag() const { return T_GlobalSym; }
        };


        explicit Module(QObject *parent = 0);
        void setErrors(Ljas::Errors* p) { d_err = p; }
        void setCache(Ljas::FileCache* p) { d_fcache = p; }

        bool parse( const QString& path, bool clearGlobal = true );
        Block* getTopChunk() const { return d_topChunk.data(); }
        const QList< Ref<Function> >& getNonLocals() const { return d_nonLocals; }
        const QString& getPath() const { return d_path; }

        Global* getGlobal() const { return d_global.data(); }
        void setGlobal(Global* g) { d_global = g; }
        static void initBuiltIns(Global*);
        static void addBuiltInSym( Global*, const QByteArray& );
    protected:
        void analyze( SynTree* );
        void chunk(SynTree*, Scope* );
        void stat( SynTree*, Scope* );
        void localdecl(SynTree*,Scope*);
        void lvardecl(SynTree*,Scope*);
        void explist(SynTree*,Scope*);
        void exp(SynTree*,Scope*);
        void lfuncdecl(SynTree*,Scope*);
        void funcbody(SynTree*,Function*);
        void namelist(SynTree*,Scope*);
        void forstat(SynTree*,Scope*);
        void gfuncdecl(SynTree*,Scope*);
        void prefixexp(SynTree*,Scope*,bool lhs);
        void assignment(SynTree*,Scope*);
        void use(SynTree*,Scope*,bool lhs);
        void lambdecl(SynTree*,Scope*);
    private:
        QString d_path;
        Ljas::Errors* d_err;
        Ljas::FileCache* d_fcache;
        Ref<Global> d_global;
        Ref<Block> d_topChunk;
        QList< Ref<Function> > d_nonLocals;
    };
}

#endif // LUAMODULE_H
