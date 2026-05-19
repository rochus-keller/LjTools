#ifndef LUAJITHELPER_H
#define LUAJITHELPER_H

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

#include <QVariant>

namespace Lua
{
class JitRowCol
{
public:

    // rowBitLen = 19 and colBitLen = 12 supports 524k lines and 4k chars per line with an 31 rowColBitLen
    static quint32 unpackCol(quint32 rowCol ) { return rowCol & ( ( 1 << colBitLen ) - 1 ); }
    static quint32 unpackRow(quint32 rowCol ) { return ( rowCol >> colBitLen ); }
    static quint32 packRowCol(quint32 row, quint32 col );
    static quint32 colBitLen; // defaults to 12
    static quint32 rowColBitLen; // defaults to 31; with 31 we can use unmodified LuaJIT
    static bool isRowCol() { return colBitLen != 0; }

};

class JitValue
{
public:
    static bool isNumber( const QVariant& );
    static bool isString( const QVariant& );
    static bool isPrimitive( const QVariant& );
    static quint8 toPrimitive( const QVariant& );
};


}

#endif // LUAJITHELPER_H
