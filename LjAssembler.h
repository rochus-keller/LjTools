#ifndef LJASSEMBLER_H
#define LJASSEMBLER_H

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

#include <QObject>
#include <LjTools/LjasSynTree.h>
#include <LjTools/LuaJitComposer.h>

namespace Ljas
{
    class Errors;
    class Assembler : public QObject // for tr
    {
    protected:
        struct Const;
        struct Var;
        struct Func;
        struct Arr;
    public:
        struct Named
        {
            QByteArray d_name;
            virtual ~Named() {}
            virtual bool isConst() const { return false; }
            virtual bool isVar() const { return false; }
            virtual bool isFunc() const { return false; }
            virtual bool isArr() const { return false; }
            Const* toConst() { return isConst() ? static_cast<Const*>(this) : 0; }
            Var* toVar() { return isVar() ? static_cast<Var*>(this) : 0; }
            Func* toFunc() { return isFunc() ? static_cast<Func*>(this) : 0; }
            Arr* toArr() { return isArr() ? static_cast<Arr*>(this) : 0; }
        };

        Assembler(Errors*);
        bool process( SynTree*, const QByteArray& sourceRef = QByteArray() );
        const QByteArray& getBc() const { return d_bc; }

    protected:
        struct Const : public Named
        {
            QVariant d_val;
            virtual bool isConst() const { return true; }
        };
        struct Var : public Named
        {
            uint d_from : 24;
            uint d_n : 8;   // number of consecutive slots required
            uint d_to : 23; // active range in bytecode list
            uint d_uv : 1; // used as upvalue
            uint d_slot : 8;
            Var* d_next;
            Var* d_prev;
            virtual bool isVar() const { return true; }
            Var():d_from(0),d_to(0),d_slot(0),d_next(0),d_prev(0),d_uv(0),d_n(1){}
            bool isUnused() const;
            bool isFixed() const;
            QPair<int,int> bounds() const; // from to of all n
        };
        typedef QList<Var*> VarList;
        struct Arr : public Named
        {
            QList<Var*> d_elems; // owned
            ~Arr();
            virtual bool isArr() const { return true; }
        };
        struct Func : public Named
        {
            typedef QMap<QByteArray,Named*> Names; // Map to sort alphabetically
            Names d_names; // owned
            QList<Var*> d_params; // not owned
            Func* d_outer;
            SynTree* d_st;
            Func():d_outer(0),d_st(0) {}
            ~Func();
            virtual bool isFunc() const { return true; }
            Named* findAll(const QByteArray& name , bool* isLocal = 0) const;
            Named* findLocal(const QByteArray& name ) const;
        };
        typedef QHash<QByteArray,quint32> Labels;
        struct Stmt
        {
            uint d_op : 8;
            uint d_pc : 24; // position in statement list
            QVariantList d_vals;
            SynTree* d_st;
            Stmt():d_op(Lua::JitBytecode::OP_INVALID),d_pc(0),d_st(0){}
            void registerRange( Var* );
        };
        typedef QList<Stmt> Stmts;

        bool processFunc( SynTree*, Func* outer = 0 );
        bool processParams(SynTree*, Func* me );
        bool processConsts( SynTree*, Func* me );
        bool processConst( SynTree*, Const* c, bool allowTable );
        bool processVars( SynTree*, Func* me );
        bool processTable( SynTree*, Const* c );
        bool processStat( SynTree*, Stmts&, Func* );
        bool fetchV(SynTree*, Stmt&, Func* , int count = 1);
        bool fetchU( SynTree*, Stmt&, Func* );
        bool fetchN(SynTree*, Stmt&);
        bool fetchS(SynTree*, Stmt&);
        bool fetchP(SynTree*, Stmt&);
        bool fetchVc( SynTree*, Stmt&, Func* );
        bool fetchC( SynTree*, Stmt&, Func* );
        bool fetchF( SynTree*, Stmt&, Func* );
        bool fetchVcsnp( SynTree*, Stmt&, Func* );
        bool fetchCsnp( SynTree*, Stmt&, Func* );
        bool fetchVcn( SynTree*, Stmt&, Func* );
        bool checkJumps( Stmts&, const Labels& );
        bool allocateRegisters3(Func* me );
        bool checkSlotOrder(const Stmts& stmts);
        Named* derefDesig( SynTree*, Func*, bool onlyLocalVars = true );
        static SynTree* findFirstChild(const SynTree*, int type , int startWith = 0);
        static SynTree* flatten( SynTree*, int stopAt = 0 );
        bool error( SynTree*, const QString& );
        static bool sortVars1( Var* lhs, Var* rhs );
        static bool checkSlotOrder( const Var*, int n );
        static Var* toVar( const QVariant& );
        static void findOverlaps( VarList&, Var* header );
        static void resolveOverlaps( const VarList& );
    private:
        Errors* d_errs;
        Lua::JitComposer d_comp;
        QByteArray d_bc;
        QByteArray d_ref;
    };
}

Q_DECLARE_METATYPE( Ljas::Assembler::Named* )

#endif // LJASSEMBLER_H
