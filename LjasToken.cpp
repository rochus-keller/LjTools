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

#include "LjasToken.h"
using namespace Ljas;

bool Token::isValid() const
{
    return d_type != Tok_Eof && d_type != Tok_Invalid;
}

bool Token::isEof() const
{
    return d_type == Tok_Eof;
}

const char*Token::getName() const
{
    return tokenTypeName( d_type );
}

const char*Token::getString() const
{
    return tokenTypeString(d_type);
}

QByteArray Token::getEscapedVal() const
{
    if( d_type == Tok_string )
    {
        QByteArray str = d_val.mid( 1, d_val.size() - 2 ); // get rid of ""

        str.replace("\\\\", "\\" );
        str.replace("\\$", "$");
        str.replace("\\\"", "\"");

        return str;
    }else
        return d_val;
}

