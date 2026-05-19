/*
* Copyright 2019, 2020 Rochus Keller <mailto:me@rochus-keller.ch>
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

#include "LuaJitHelper.h"
using namespace Lua;

quint32 JitRowCol::colBitLen = 12;
quint32 JitRowCol::rowColBitLen = 31;

quint32 JitRowCol::packRowCol(quint32 row, quint32 col)
{
    static const quint32 maxRow = ( 1 << (rowColBitLen - colBitLen) ) - 1;
    static const quint32 maxCol = ( 1 << colBitLen ) - 1;
    Q_ASSERT( row <= maxRow && col <= maxCol );
    if( row > maxRow )
        row = maxRow;
    if( col > maxCol )
        col = maxCol;
    return ( row << colBitLen ) | col;
}


bool JitValue::isNumber(const QVariant& v)
{
    switch( v.type() )
    {
    case QVariant::UInt:
    case QVariant::ULongLong:
    case QVariant::Int:
    case QVariant::LongLong:
    case QVariant::Double:
    case QMetaType::Float:
        return true;
    default:
        return false;
    }
}

bool JitValue::isString(const QVariant& v)
{
    return v.type() == QVariant::String || v.type() == QVariant::ByteArray;
}

bool JitValue::isPrimitive(const QVariant& v)
{
    return v.type() == QVariant::Bool || v.isNull();
}

quint8 JitValue::toPrimitive(const QVariant& v)
{
    if( v.isNull() )
        return 0;
    else if( v.type() == QVariant::Bool )
    {
        if( v.toBool() )
            return 2;
        else
            return 1;
    }
    return 0;
}
