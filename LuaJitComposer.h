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

        explicit JitComposer(QObject *parent = 0);

        void clear();

        int openFunction(quint8 parCount, const QByteArray& sourceRef, int firstLine = -1, int lastLine = -1 );
        bool closeFunction(quint8 frameSize);

        bool addAbc( JitBytecode::Op, quint8 a, quint8 b, quint8 c, int line = -1 );
        bool addAd(JitBytecode::Op, quint8 a, quint16 d, int line = -1 );
        void setUpvals( const UpvalList& );
        void setVarNames( const VarNameList& );

        int getLocalSlot( const QByteArray& name );
        int getConstSlot( const QVariant& );

        bool write(QIODevice* out, const QString& path = QString() );
        bool write( const QString& file );

        static bool allocateWithLinearScan(QBitArray& pool, Intervals& vars, int len );
        static int nextFreeSlot( QBitArray& pool, int len = 1 );
    protected:
        JitBytecode d_bc;

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
