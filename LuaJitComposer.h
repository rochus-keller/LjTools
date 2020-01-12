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
            uint d_from : 24;
            uint d_slot : 8;
            quint32 d_to;
            void* d_payload;
            Interval(uint from, uint to, void* pl):d_from(from),d_to(to),d_payload(pl),d_slot(0){}
        };
        typedef QList<Interval> Intervals;
        typedef std::bitset<MAX_SLOTS> SlotPool;
        typedef quint8 SlotNr;
        typedef quint8 UvNr;
        typedef qint16 Jump;

        explicit JitComposer(QObject *parent = 0);

        void clear();

        int openFunction(quint8 parCount, const QByteArray& sourceRef, int firstLine = -1, int lastLine = -1 );
        bool closeFunction(quint8 frameSize);

        bool addAbc( JitBytecode::Op, quint8 a, quint8 b, quint8 c, int line = -1 );
        bool addAd(JitBytecode::Op, quint8 a, quint16 d, int line = -1 );
        int getCurPc() const;
        bool patch( quint32 pc, qint16 off );

        bool ADD(SlotNr dst, const QVariant& lhs, SlotNr rhs, int line = -1 );
        bool ADD(SlotNr dst, SlotNr lhs, const QVariant& rhs, int line = -1 );
        bool ADD(SlotNr dst, SlotNr lhs, SlotNr rhs, int line = -1 );
        bool CALL(SlotNr slot, quint8 numOfReturns = 0, quint8 numOfArgs = 0, int line = -1 );
        bool CALLT(SlotNr slot, quint8 numOfArgs = 0, int line = -1 );
        bool CAT(SlotNr dst, SlotNr from, SlotNr to, int line = -1 );
        bool DIV(SlotNr dst, const QVariant& lhs, SlotNr rhs, int line = -1 );
        bool DIV(SlotNr dst, SlotNr lhs, const QVariant& rhs, int line = -1 );
        bool DIV(SlotNr dst, SlotNr lhs, SlotNr rhs, int line = -1 );
        bool FNEW(SlotNr dst, quint16 func, int line = -1 );
        bool FORI(SlotNr base, Jump offset, int line = -1 );
        bool FORL(SlotNr base, Jump offset, int line = -1 );
        bool GGET(SlotNr to, const QByteArray& name, int line  = -1 );
        bool GSET(SlotNr value, const QByteArray& name, int line  = -1 );
        bool ISGE(SlotNr lhs, SlotNr rhs, int line = -1 ); // lhs >= rhs
        bool ISGT(SlotNr lhs, SlotNr rhs, int line = -1 ); // lhs > rhs
        bool ISLE(SlotNr lhs, SlotNr rhs, int line = -1 ); // lhs <= rhs
        bool ISLT(SlotNr lhs, SlotNr rhs, int line = -1 ); // lhs < rhs
        bool ISEQ(SlotNr lhs, const QVariant& rhs, int line = -1 ); // lhs == rhs
        bool ISEQ(SlotNr lhs, SlotNr rhs, int line = -1 );
        bool ISNE(SlotNr lhs, const QVariant& rhs, int line = -1 ); // lhs != rhs
        bool ISNE(SlotNr lhs, SlotNr rhs, int line = -1 );
        bool ISF(SlotNr slot, int line = -1 );
        bool ISFC(SlotNr lhs, SlotNr rhs, int line = -1 );
        bool IST(SlotNr slot, int line = -1 );
        bool ISTC(SlotNr lhs, SlotNr rhs, int line = -1 );
        bool JMP(SlotNr base, Jump offset, int line = -1 );
        bool KNIL(SlotNr base, quint8 len = 1, int line = -1 );
        bool KSET(SlotNr dst, const QVariant& , int line = -1 );
        bool LEN(SlotNr lhs, SlotNr rhs, int line = -1 );
        bool LOOP(SlotNr base, Jump offset, int line = -1 );
        bool MOD(SlotNr dst, const QVariant& lhs, SlotNr rhs, int line = -1 );
        bool MOD(SlotNr dst, SlotNr lhs, const QVariant& rhs, int line = -1 );
        bool MOD(SlotNr dst, SlotNr lhs, SlotNr rhs, int line = -1 );
        bool MOV(SlotNr lhs, SlotNr rhs, int line = -1 );
        bool MUL(SlotNr dst, const QVariant& lhs, SlotNr rhs, int line = -1 );
        bool MUL(SlotNr dst, SlotNr lhs, const QVariant& rhs, int line = -1 );
        bool MUL(SlotNr dst, SlotNr lhs, SlotNr rhs, int line = -1 );
        bool NOT(SlotNr lhs, SlotNr rhs, int line = -1 );
        bool POW(SlotNr dst, SlotNr lhs, SlotNr rhs, int line = -1 );
        bool RET(SlotNr slot, quint8 len, int line = -1 );
        bool RET(int line = -1 );
        bool SUB(SlotNr dst, const QVariant& lhs, SlotNr rhs, int line = -1 );
        bool SUB(SlotNr dst, SlotNr lhs, const QVariant& rhs, int line = -1 );
        bool SUB(SlotNr dst, SlotNr lhs, SlotNr rhs, int line = -1 );
        bool TDUP(SlotNr dst, const QVariant& constTable, int line = -1 );
        bool TGET(SlotNr to, SlotNr table, quint8 index, int line = -1 );
        bool TGET(SlotNr to, SlotNr table, const QVariant&  index, int line = -1 );
        bool TNEW(SlotNr slot, quint16 arrSize = 0, quint8 hashSize = 0, int line = -1 );
        bool TSET(SlotNr value, SlotNr table, quint8 index, int line = -1 );
        bool TSET(SlotNr value, SlotNr table, const QVariant&  index, int line = -1 );
        bool UCLO(SlotNr slot, Jump offset, int line = -1 );
        bool UGET(SlotNr toSlot, UvNr fromUv, int line = -1 );
        bool USET(UvNr toUv, SlotNr rhs, int line = -1 );
        bool USET(UvNr toUv, const QVariant& rhs, int line = -1 );
        bool UNM(SlotNr lhs, SlotNr rhs, int line = -1 );

        void setUpvals( const UpvalList& );
        void setVarNames( const VarNameList& );

        int getLocalSlot( const QByteArray& name );
        int getConstSlot( const QVariant& );

        bool write(QIODevice* out, const QString& path = QString() );
        bool write( const QString& file );
        void setStripped(bool);

        static bool allocateWithLinearScan(SlotPool& pool, Intervals& vars, int len = 1 );
        static int nextFreeSlot(SlotPool& pool, int len = 1 , int startFrom = -1);
        static bool releaseSlot( SlotPool& pool, quint8 slot, int len = 1 );
        static int highestUsedSlot( const SlotPool& pool );
    protected:
        JitBytecode d_bc;
        bool d_hasDebugInfo;
        bool d_stripped;

        struct Func : public JitBytecode::Function
        {
            QHash<QVariant,int> d_gcConst;
            QHash<QVariant,int> d_numConst;
            QHash<QByteArray,int> d_var;
        };

        bool addOpImp( JitBytecode::Op, quint8 a, quint8 b, quint16 cd, int line = -1 );
    };
}

#endif // LUAJITCOMPOSER_H
