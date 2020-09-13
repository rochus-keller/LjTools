#ifndef LUAJITCOMPOSER_H
#define LUAJITCOMPOSER_H

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
#include <LjTools/LuaJitBytecode.h>
#include <bitset>

namespace Lua
{
    class JitComposer : public QObject
    {
    public:
        struct Upval
        {
            quint16 d_uv : 14;
            quint16 d_isLocal : 1;
            quint16 d_isRo : 1;
            QByteArray d_name;
            Upval():d_uv(0),d_isLocal(0),d_isRo(0){}
        };
        typedef QVector<Upval> UpvalList;

        struct VarName
        {
            QByteArray d_name;
            quint32 d_from;
            quint32 d_to;
            VarName():d_from(0),d_to(0){}
        };
        typedef QVector<VarName> VarNameList;

        enum { MAX_SLOTS = 250 };
        struct Interval
        {
            quint32 d_from;
            quint32 d_to;
            void* d_payload;
            quint8 d_slot;
            Interval(quint32 from, quint32 to, void* pl):d_from(from),d_to(to),d_payload(pl),d_slot(0){}
        };
        typedef QList<Interval> Intervals;
        struct SlotPool
        {
            std::bitset<MAX_SLOTS> d_slots;
            QList<quint8> d_callArgs; // stack
            quint8 d_frameSize; // max used slot
            SlotPool():d_frameSize(0){}
        };
        typedef quint8 SlotNr;
        typedef quint8 UvNr;
        typedef qint16 Jump;

        explicit JitComposer(QObject *parent = 0);

        void clear();

        int openFunction(quint8 parCount, const QByteArray& sourceRef, quint32 firstLine = 0, quint32 lastLine = 0 );
        bool closeFunction(quint8 frameSize);

        bool addAbc( JitBytecode::Op, quint8 a, quint8 b, quint8 c, quint32 line = 0 );
        bool addAd(JitBytecode::Op, quint8 a, quint16 d, quint32 line = 0 );
        int getCurPc() const;
        bool patch( quint32 pc, qint16 off ); // pc points to a jump, off is the new offset for the jump
        bool patch( quint32 label ) { return patch( label, getCurPc() - label ); }

        bool ADD(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line = 0 );
        bool ADD(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line = 0 );
        bool ADD(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool CALL(SlotNr slot, quint8 numOfReturns = 0, quint8 numOfArgs = 0, quint32 line = 0 );
        bool CALLT(SlotNr slot, quint8 numOfArgs = 0, quint32 line = 0 );
        bool CAT(SlotNr dst, SlotNr from, SlotNr to, quint32 line = 0 );
        bool DIV(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line = 0 );
        bool DIV(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line = 0 );
        bool DIV(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool FNEW(SlotNr dst, quint16 func, quint32 line = 0 );
        bool FORI(SlotNr base, Jump offset, quint32 line = 0 );
        bool FORL(SlotNr base, Jump offset, quint32 line = 0 );
        bool GGET(SlotNr to, const QByteArray& name, quint32 line = 0 );
        bool GSET(SlotNr value, const QByteArray& name, quint32 line = 0 );
        bool ISGE(SlotNr lhs, SlotNr rhs, quint32 line = 0 ); // lhs >= rhs
        bool ISGT(SlotNr lhs, SlotNr rhs, quint32 line = 0 ); // lhs > rhs
        bool ISLE(SlotNr lhs, SlotNr rhs, quint32 line = 0 ); // lhs <= rhs
        bool ISLT(SlotNr lhs, SlotNr rhs, quint32 line = 0 ); // lhs < rhs
        bool ISEQ(SlotNr lhs, const QVariant& rhs, quint32 line = 0 ); // lhs == rhs
        bool ISEQ(SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool ISNE(SlotNr lhs, const QVariant& rhs, quint32 line = 0 ); // lhs != rhs
        bool ISNE(SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool ISF(SlotNr slot, quint32 line = 0 );
        bool ISFC(SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool IST(SlotNr slot, quint32 line = 0 );
        bool ISTC(SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool JMP(SlotNr base, Jump offset, quint32 line = 0 );
        bool KNIL(SlotNr base, quint8 len = 1, quint32 line = 0 );
        bool KSET(SlotNr dst, const QVariant& , quint32 line = 0 );
        bool LEN(SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool LOOP(SlotNr base, Jump offset, quint32 line = 0 );
        bool MOD(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line = 0 );
        bool MOD(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line = 0 );
        bool MOD(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool MOV(SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool MUL(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line = 0 );
        bool MUL(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line = 0 );
        bool MUL(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool NOT(SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool POW(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool RET(SlotNr slot, quint8 len, quint32 line = 0 );
        bool RET(quint32 line = 0 );
        bool SUB(SlotNr dst, const QVariant& lhs, SlotNr rhs, quint32 line = 0 );
        bool SUB(SlotNr dst, SlotNr lhs, const QVariant& rhs, quint32 line = 0 );
        bool SUB(SlotNr dst, SlotNr lhs, SlotNr rhs, quint32 line = 0 );
        bool TDUP(SlotNr dst, const QVariant& constTable, quint32 line = 0 );
        bool TGET(SlotNr to, SlotNr table, quint8 index, quint32 line = 0 ); // index is a slot
        bool TGET(SlotNr to, SlotNr table, const QByteArray& name, quint32 line = 0 );
        bool TGETi(SlotNr to, SlotNr table, quint8 index, quint32 line = 0 ); // index is a number
        bool TNEW(SlotNr slot, quint16 arrSize = 0, quint8 hashSize = 0, quint32 line = 0 );
        bool TSET(SlotNr value, SlotNr table, quint8 index, quint32 line = 0 ); // index is a slot
        bool TSETi(SlotNr value, SlotNr table, quint8 index, quint32 line = 0 ); // index is a number
        bool TSET(SlotNr value, SlotNr table, const QByteArray&  index, quint32 line = 0 );
        bool UCLO(SlotNr slot, Jump offset, quint32 line = 0 ); // see note**
        bool UGET(SlotNr toSlot, UvNr fromUv, quint32 line = 0 );
        bool USET(UvNr toUv, SlotNr rhs, quint32 line = 0 );
        bool USET(UvNr toUv, const QVariant& rhs, quint32 line = 0 );
        bool UNM(SlotNr lhs, SlotNr rhs, quint32 line = 0 );

        // **NOTE: UCLO must be emitted whenever a body is left the locals of which are accessed as
        // upvalues; slot was always 0 so far where observed from the LJ compiler; bodies which access
        // upvalues or which are between those and the upvalue source don't emit UCLO.

        void setUpvals( const UpvalList& );
        void setVarNames( const VarNameList& );

        int getLocalSlot( const QByteArray& name );
        int getConstSlot( const QVariant& );

        bool write(QIODevice* out, const QString& path = QString() );
        bool write( const QString& file );
        void setStripped(bool);
        void setUseRowColFormat(bool);

        static bool allocateWithLinearScan(SlotPool& pool, Intervals& vars, int len = 1 );
        static int nextFreeSlot(SlotPool& pool, int len = 1 , bool callArgs = false);
        static bool releaseSlot( SlotPool& pool, quint8 slot, int len = 1 );
        static int highestUsedSlot( const SlotPool& pool );

        enum { ROW_BIT_LEN = 19, COL_BIT_LEN = 32 - ROW_BIT_LEN - 1, MSB = 0x80000000 };
        // supports 524k lines and 4k chars per line
        static bool isPacked( quint32 rowCol ) { return rowCol & MSB; }
        static quint32 unpackCol(quint32 rowCol ) { return rowCol & ( ( 1 << COL_BIT_LEN ) - 1 ); }
        static quint32 unpackCol2(quint32 rowCol ) { return isPacked(rowCol) ? unpackCol(rowCol) : 1; }
        static quint32 unpackRow(quint32 rowCol ) { return ( ( rowCol & ~MSB ) >> COL_BIT_LEN ); }
        static quint32 unpackRow2(quint32 rowCol ) { return isPacked(rowCol) ? unpackRow(rowCol) : rowCol; }
        static quint32 packRowCol(quint32 row, quint32 col );
    protected:
        JitBytecode d_bc;
        bool d_hasDebugInfo;
        bool d_stripped;
        bool d_useRowColFormat;

        struct Func : public JitBytecode::Function
        {
            QHash<QVariant,int> d_gcConst;
            QHash<QVariant,int> d_numConst;
            QHash<QByteArray,int> d_var;
        };

        bool addOpImp( JitBytecode::Op, quint8 a, quint8 b, quint16 cd, quint32 line = 0 );
    };
}

#endif // LUAJITCOMPOSER_H
