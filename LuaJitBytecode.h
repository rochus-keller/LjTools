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
        };

        struct Function : public QSharedData
        {
            enum { UvLocalMask = 0x8000, /* Upvalue for local slot. */
                   UvImmutableMask = 0x4000 /* Immutable upvalue. */ // a local seems to be immutable if only initialized but never changed
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

            Function():d_isRoot(false){}

            quint16 getUpval( int i ) const { return d_upvals[i] & ~( UvImmutableMask | UvLocalMask ); }
            bool isLocalUpval( int i ) const { return d_upvals[i] & UvLocalMask; }
            bool isImmutableUpval( int i ) const  { return d_upvals[i] & UvImmutableMask; }
        };
        typedef QExplicitlySharedDataPointer<Function> FuncRef;
        struct ByteCode
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
                _lit,   // literal
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
            quint8 d_a, d_b;
            quint16 d_cd;
            quint8 d_ta, d_tb, d_tcd;
            quint8 d_op;
            ByteCode():d_ta(Unused),d_tb(Unused),d_tcd(Unused),d_a(0),d_b(0),d_cd(0),d_name(""),d_op(0){}
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
        const QList<FuncRef>& getFuncs() const { return d_funcs; }
        Function* getRoot() const;
        static ByteCode dissectByteCode(quint32);
    protected:
        bool parseHeader(QIODevice* );
        bool parseFunction(QIODevice* );
        bool error( const QString& );
        QVariantList readObjConsts(QIODevice* in, quint32 len );
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
