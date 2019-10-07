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
        explicit JitComposer(QObject *parent = 0);

        void clear();

        bool openFunction(quint8 parCount, const QByteArray& sourceRef, int firstLine = -1, int lastLine = -1 );
        bool closeFunction();

        bool addOp( JitBytecode::Op, quint8 a, quint8 b, int cd, int line = -1 );
        bool addOp( JitBytecode::Op, quint8 a, int d, int line = -1 );
#if 0
        bool addAD( JitBytecode::Op, quint8 a, quint32 d, int line = -1 );
        bool addAD( JitBytecode::Op, quint8 a, double d, int line = -1 );
        bool addAD( JitBytecode::Op, quint8 a, const QByteArray& d, int line = -1 );
#endif
        int getLocalSlot( const QByteArray& name );
        int getConstSlot( const QVariant& );

        struct Function : public QSharedData // only public because of Q_DECLARE_METATYPE
        {
            Function():d_frameSize(0),d_numOfParams(0),d_firstLine(0),d_lastLine(0) {}

            QHash<QVariant,int> d_gcConst;
            QHash<QVariant,int> d_numConst;
            QHash<QByteArray,int> d_var;
            QByteArray d_sourceRef; // File path in top function, whatever in subordinate functions
            quint8 d_numOfParams,d_frameSize;
            int d_firstLine, d_lastLine;

            QList<quint32> d_byteCodes;
            QList<quint32> d_lines;
        };
        typedef QExplicitlySharedDataPointer<Function> FuncRef;

    protected:
        QList<FuncRef> d_funcStack;
        FuncRef d_top;
    };
}
Q_DECLARE_METATYPE(Lua::JitComposer::FuncRef)

#endif // LUAJITCOMPOSER_H
