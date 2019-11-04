#ifndef LUAJITBYTECODE_H
#define LUAJITBYTECODE_H

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

#include <QObject>
#include <QVector>
#include <QHash>
#include <QVariant>
#include <QSharedData>

class QIODevice;

namespace Lua
{
    class JitBytecode : public QObject
    {
    public:
        typedef QVector<quint32> CodeList;
        typedef QVector<quint16> UpvalList;

        struct ConstTable
        {
            QHash<QVariant,QVariant> d_hash;
            QVector<QVariant> d_array;
            QHash<QVariant,QVariant> merged() const;
        };

        struct Function : public QSharedData
        {
            enum { UvLocalMask = 0x8000, /* Upvalue for local slot. */
                   UvImmutableMask = 0x4000, /* Immutable upvalue. */ // a local seems to be immutable if only initialized but never changed
                   FuHasSubFus = 0x1,   // has subfunctions
                   FuVarargs = 0x2,     // has varargs
                 };
            QString d_sourceFile;
            quint32 d_id; // index in bytecode file
            quint8 d_flags;
            quint8 d_numparams;
            quint8 d_framesize;
            bool d_isRoot;
            quint32 d_firstline;
            quint32 d_numline;
            CodeList d_byteCodes;
            UpvalList d_upvals;
            QVariantList d_constObjs;
            QVariantList d_constNums;
            QVector<quint32> d_lines;
            QByteArrayList d_upNames;
            struct Var
            {
                quint32 d_startpc; // First point where the local variable is active
                quint32 d_endpc;   // First point where the local variable is dead
                QByteArray d_name;
            };
            QList<Var> d_vars;
            mutable QByteArrayList d_varNames; // fill by calcVarNames
            Function* d_outer;

            Function():d_isRoot(false),d_outer(0){}

            const Var* findVar( int pc, int slot, int* idx = 0 ) const;
            QByteArray getVarName( int pc, int slot, int* idx = 0 ) const;
            void calcVarNames() const;
            bool isStripped() const { return d_lines.isEmpty() && d_upNames.isEmpty() && d_vars.isEmpty(); }
            quint16 getUpval( int i ) const { return d_upvals[i] & ~( UvImmutableMask | UvLocalMask ); }
            bool isLocalUpval( int i ) const { return d_upvals[i] & UvLocalMask; }
            bool isImmutableUpval( int i ) const  { return d_upvals[i] & UvImmutableMask; }
            QPair<quint8,Function*> getFuncSlotFromUpval(quint8) const;
        };
        typedef QExplicitlySharedDataPointer<Function> FuncRef;

        enum Op { OP_ISLT, OP_ISGE, OP_ISLE, OP_ISGT, OP_ISEQV, OP_ISNEV, OP_ISEQS, OP_ISNES, OP_ISEQN,
                  OP_ISNEN, OP_ISEQP, OP_ISNEP, OP_ISTC, OP_ISFC, OP_IST, OP_ISF, OP_MOV, OP_NOT, OP_UNM,
                  OP_LEN, OP_ADDVN, OP_SUBVN, OP_MULVN, OP_DIVVN, OP_MODVN, OP_ADDNV, OP_SUBNV, OP_MULNV,
                  OP_DIVNV, OP_MODNV, OP_ADDVV, OP_SUBVV, OP_MULVV, OP_DIVVV, OP_MODVV, OP_POW, OP_CAT,
                  OP_KSTR, OP_KCDATA, OP_KSHORT, OP_KNUM, OP_KPRI, OP_KNIL, OP_UGET, OP_USETV, OP_USETS,
                  OP_USETN, OP_USETP, OP_UCLO, OP_FNEW, OP_TNEW, OP_TDUP, OP_GGET, OP_GSET, OP_TGETV,
                  OP_TGETS, OP_TGETB, OP_TSETV, OP_TSETS, OP_TSETB, OP_TSETM, OP_CALLM, OP_CALL, OP_CALLMT,
                  OP_CALLT, OP_ITERC, OP_ITERN, OP_VARG, OP_ISNEXT, OP_RETM, OP_RET, OP_RET0, OP_RET1,
                  OP_FORI, OP_JFORI, OP_FORL, OP_IFORL, OP_JFORL, OP_ITERL, OP_IITERL, OP_JITERL, OP_LOOP,
                  OP_ILOOP, OP_JLOOP, OP_JMP, // FUNCF ff are not supported here
                  OP_INVALID = 255
                };

        struct Instruction
        {
            enum FieldType {
                Unused,
                ____ = Unused,
                _var,   // variable slot
                _str,   // string constant, negated index into constant table
                _num,   // number constant, index into constant table
                _pri,   // primitive type (0 = nil, 1 = false, 2 = true)
                _dst,   // variable slot number, used as a destination
                _rbase, // base slot number, read-only
                _cdata, // cdata constant, negated index into constant table
                _lit,   // unsigned literal
                _lits,  // signed literal
                _base,  // base slot number, read-write
                _uv,    // upvalue number
                _jump,  // branch target, relative to next instruction, biased with 0x8000
                _func,  // function prototype, negated index into constant table
                _tab,   // template table, negated index into constant table
                // note: negated means index 0 points to the last entry!!!
                Max
            };
            static const char* s_typeName[];
            const char* d_name;
            quint16 d_a, d_b; // quint16 instead of quint8 so that Disasm can reuse the fields for numbers
            quint16 d_cd;
            quint8 d_ta, d_tb, d_tcd;
            quint8 d_op; // enum Op
            Instruction():d_ta(Unused),d_tb(Unused),d_tcd(Unused),d_a(0),d_b(0),d_cd(0),d_name(""),d_op(0){}
            int getCd() const {
                switch( d_tcd )
                {
                case _lits:
                    return qint16(d_cd);
                case _jump:
                    return d_cd - 0x8000;
                default:
                    return d_cd;
                }
            }
        };

        explicit JitBytecode(QObject *parent = 0);
        bool parse( const QString& file );
        bool parse(QIODevice* in , const QString& path = QString());
        bool write(QIODevice* out, const QString& path = QString() );
        bool write( const QString& file );
        const QList<FuncRef>& getFuncs() const { return d_funcs; }
        Function* getRoot() const;
        bool isStripped() const;
        static Instruction dissectInstruction(quint32);
        enum Format { ABC, AD };
        static Format formatFromOp(quint8);
        static Instruction::FieldType typeCdFromOp(quint8);
        static Instruction::FieldType typeBFromOp(quint8);
        static bool isNumber( const QVariant& );
        static bool isString( const QVariant& );
        static bool isPrimitive( const QVariant& );
        static const char* nameOfOp(int op );
    protected:
        bool parseHeader(QIODevice* );
        bool writeHeader(QIODevice* );
        bool parseFunction(QIODevice* );
        bool writeFunction(QIODevice*, Function*);
        QByteArray writeDbgInfo(Function*);
        bool writeNumConsts(QIODevice*, const QVariantList& );
        bool writeObjConsts(QIODevice*, const QVariantList& );
        bool writeByteCodes(QIODevice*, const CodeList& );
        bool error( const QString& );
        QVariantList readObjConsts( Function* f, QIODevice* in, quint32 len );
    private:
        QString d_name;
        QList<FuncRef> d_funcs;
        QList<FuncRef> d_fstack;
        quint8 d_flags;
    };
}

Q_DECLARE_METATYPE(Lua::JitBytecode::ConstTable)
Q_DECLARE_METATYPE(Lua::JitBytecode::FuncRef)
uint qHash(const QVariant& v, uint seed = 0);

#endif // LUAJITBYTECODE_H
