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

#include "LuaJitBytecode.h"
#include <QtDebug>
#include <QFile>
#include <QtEndian>
#include <lj_bc.h>
using namespace Lua;

// Adapted from LuaJIT 2.0.5 lj_bcread.c

/* Bytecode dump header. */
#define BCDUMP_HEAD1		0x1b
#define BCDUMP_HEAD2		0x4c
#define BCDUMP_HEAD3		0x4a

/* If you perform *any* kind of private modifications to the bytecode itself
** or to the dump format, you *must* set BCDUMP_VERSION to 0x80 or higher.
*/
#define BCDUMP_VERSION		1

/* Compatibility flags. */
#define BCDUMP_F_BE		0x01
#define BCDUMP_F_STRIP		0x02
#define BCDUMP_F_FFI		0x04

#define BCDUMP_F_KNOWN		(BCDUMP_F_FFI*2-1)

/* Flags for prototype. */
#define PROTO_CHILD		0x01	/* Has child prototypes. */
#define PROTO_VARARG		0x02	/* Vararg function. */
#define PROTO_FFI		0x04	/* Uses BC_KCDATA for FFI datatypes. */
#define PROTO_NOJIT		0x08	/* JIT disabled for this function. */
#define PROTO_ILOOP		0x10	/* Patched bytecode with ILOOP etc. */

/* Type codes for the GC constants of a prototype. Plus length for strings. */
enum {
  BCDUMP_KGC_CHILD, BCDUMP_KGC_TAB, BCDUMP_KGC_I64, BCDUMP_KGC_U64,
  BCDUMP_KGC_COMPLEX, BCDUMP_KGC_STR
};

/* Type codes for the keys/values of a constant table. */
enum {
  BCDUMP_KTAB_NIL, BCDUMP_KTAB_FALSE, BCDUMP_KTAB_TRUE,
  BCDUMP_KTAB_INT, BCDUMP_KTAB_NUM, BCDUMP_KTAB_STR
};

/* Fixed internal variable names. */
enum {
  VARNAME_END,
    VARNAME_FOR_IDX,
    VARNAME_FOR_STOP,
    VARNAME_FOR_STEP,
    VARNAME_FOR_GEN,
    VARNAME_FOR_STATE,
    VARNAME_FOR_CTL,
  VARNAME__MAX
};
static const char* s_varname[] = {
    "",
    "(for index)",
    "(for limit)",
    "(for step)",
    "(for generator)",
    "(for state)",
    "(for control)"
};

// helper
union TValue {
    double d;
    struct {
    quint32 lo;
    quint32 hi;
    };
};

typedef quint8 BCReg;
struct _ByteCode
{
    const char* d_op;
    quint8 d_fa;
    quint8 d_fb;
    quint8 d_fcd;
} s_byteCodes[] =
{
#define BCSTRUCT(name, ma, mb, mc, mt) { #name, JitBytecode::Instruction::_##ma, \
    JitBytecode::Instruction::_##mb, JitBytecode::Instruction::_##mc },
BCDEF(BCSTRUCT)
#undef BCENUM
};

const char* JitBytecode::Instruction::s_typeName[] =
{
    "",
    "var",
    "str",
    "num",
    "pri",
    "dst",
    "rbase",
    "cdata",
    "lit",
    "lits",
    "base",
    "uv",
    "jump",
    "func",
    "tab",
};

/* Read ULEB128 value from buffer. */
static quint32 bcread_uleb128(QIODevice* in)
{
    quint32 result = 0;
    int shift = 0;
    while(true)
    {
        quint8 byte;
        if( !in->getChar( (char*)&byte ) )
            break;
        result |= ( byte & 0x7f ) << shift;
        if( ( byte & 0x80 ) == 0 )
            break;
        shift += 7;
    }
    return result;
}

/* Read top 32 bits of 33 bit ULEB128 value from buffer. */
static quint32 bcread_uleb128_33(QIODevice* in)
{
    quint8 byte;
    if( !in->getChar( (char*)&byte ) )
        return 0;
    quint32 result = (byte >> 1);
    if( result >= 0x40 )
    {
        result &= 0x3f;
        int shift = -1;
        while(true)
        {
            if( !in->getChar( (char*)&byte ) )
                break;
            result |= ( byte & 0x7f ) << ( shift += 7 );
            if( ( byte & 0x80 ) == 0 )
                break;
        }
    }
    return result;
}

/* Read ULEB128 value. */
static quint32 debug_read_uleb128(const quint8 *p, int& pos )
{
  quint32 v = p[pos++];
  if (v >= 0x80) {
    int sh = 0;
    v &= 0x7f;
    do { v |= ((p[pos] & 0x7f) << (sh += 7)); } while (p[pos++] >= 0x80);
  }
  return v;
}

static inline quint8 readByte(QIODevice* in)
{
    char ch;
    if( in->getChar(&ch) )
        return quint8(ch);
    return 0;
}

static JitBytecode::CodeList readCode( QIODevice* in, bool swap, quint32 len )
{
    static const int codeLen = 4;
    JitBytecode::CodeList res(len);
    for( int i = 0; i < len; i++ )
    {
        const QByteArray buf = in->read(codeLen);
        if( buf.size() < codeLen)
            return res;
        quint32 tmp;
        memcpy( &tmp, buf.constData(), codeLen );
        if( swap )
            res[i] = qbswap(tmp);
        else
            res[i] = tmp;
    }
    return res;
}

static JitBytecode::UpvalList readUpval( QIODevice* in, bool swap, quint32 len )
{
    static const int codeLen = 2;
    JitBytecode::UpvalList res(len);
    for( int i = 0; i < len; i++ )
    {
        const QByteArray buf = in->read(codeLen);
        if( buf.size() < codeLen)
            return res;
        quint16 tmp;
        memcpy( &tmp, buf.constData(), codeLen );
        if( swap )
            res[i] = qbswap(tmp);
        else
            res[i] = tmp;
    }
    return res;
}

/* Read a single constant key/value of a template table. */
static QVariant bcread_ktabk(QIODevice* in )
{
  const quint32 tp = bcread_uleb128(in);
  if (tp >= BCDUMP_KTAB_STR) {
    const quint32 len = tp - BCDUMP_KTAB_STR;
    return in->read(len);
  } else if (tp == BCDUMP_KTAB_INT) {
    return bcread_uleb128(in);
  } else if (tp == BCDUMP_KTAB_NUM) {
      TValue u;
        u.lo = bcread_uleb128(in);
        u.hi = bcread_uleb128(in);
        return u.d;
  } else if ( tp == BCDUMP_KTAB_TRUE )
      return true;
  else if( tp == BCDUMP_KTAB_FALSE )
        return false;
  else
        return QVariant();
}

QVariantList JitBytecode::readObjConsts( Function* f, QIODevice* in, quint32 len )
{
    QVariantList res;
    for( int i = 0; i < len; i++ )
    {
        const quint32 tp = bcread_uleb128(in);
        if( tp >= BCDUMP_KGC_STR )
        {
            quint32 len = tp - BCDUMP_KGC_STR;
            const QByteArray str = in->read(len);
            res.append(str);
        }else if( tp == BCDUMP_KGC_TAB )
        {
            JitBytecode::ConstTable map;
            const quint32 narray = bcread_uleb128(in);
            const quint32 nhash = bcread_uleb128(in);
            if( narray )
            {
                for (int j = 0; j < narray; j++)
                  map.d_array << bcread_ktabk(in);
            }
            if( nhash )
            {
                for ( int j = 0; j < nhash; j++)
                  map.d_hash.insert( bcread_ktabk(in), bcread_ktabk(in) );
            }
            res.append( QVariant::fromValue(map) );
        }else if (tp != BCDUMP_KGC_CHILD) {
            qCritical() << "FFI not supported";
        } else {
            Q_ASSERT(tp == BCDUMP_KGC_CHILD);
            if( d_fstack.isEmpty() )
                error(tr("referencing unknown child function"));
            else
            {
                FuncRef r = d_fstack.back();
                if( r->d_outer != 0 )
                    error(tr("invalid function hierarchy"));
                else
                    r->d_outer = f;
                res.append( QVariant::fromValue(r) );
                d_fstack.pop_back();
            }
        }
    }
    return res;
}

static QVariantList readNumConsts( QIODevice* in, quint32 len )
{
    QVariantList res;
    for ( int i = 0; i < len; i++ )
    {
        const QByteArray ch = in->peek(1);
        const int isnum = !ch.isEmpty() && ( ch[0] & 1 );
        const quint32 lo = bcread_uleb128_33(in);
        if (isnum) {
            TValue u;
            u.lo = lo;
            u.hi = bcread_uleb128(in);

#if 0
            if ((u.hi << 1) < 0xffe00000) {  /* Finite? */  // 1111 1111 1110 0000 0000 0000 0000 0000
                res << u.d;
            } else if (((u.hi & 0x000fffff) | u.lo) != 0) { // 0000 0000 0000 1111 1111 1111 1111 1111
                qDebug() << "nan";
            } else if ((u.hi & 0x80000000) == 0) {          // 1000 0000 0000 0000 0000 0000 0000 0000
                qDebug() << "+inf";
            } else {
                qDebug() << "-inf";
            }
#else
            // const quint32 test =  u.d + 6755399441055744.0;  /* 2^52 + 2^51 */
            //     if op == "TSETM " then kc = kc - 2^52 end ???
            res << u.d;


#endif
        } else {
            res << lo;
        }
    }
    return res;
}

static QVector<quint32> readLineNumbers( QIODevice* in, bool swap, int sizeli, int sizebc, int numline, int firstline )
{
    if( sizeli == 0 )
        return QVector<quint32>();

    const QByteArray buf = in->read(sizeli);
    // buf contains a line number per bytecode encoded in 1, 2 or 4 bytes depending on line count,
    // and then other stuff
    if( buf.isEmpty() )
        return QVector<quint32>();
    if( buf.size() < sizeli )
    {
        qCritical() << "chunk too short";
        return QVector<quint32>();
    }

    QVector<quint32> lines( sizebc ); // empty or one line nr per byteCodes entry

    if( numline < 256 )
    {
        // 1 byte per number
        for( int i = 0; i < sizebc; i++ )
            lines[i] = quint8(buf[i]) + firstline;
    }else if( numline < 65536 )
    {
        // 2 bytes per number
        int j = 0;
        quint16 tmp;
        for( int i = 0; i < sizebc; i++, j += 2 )
        {
            memcpy( &tmp, buf.constData()+j, 2 );
            if( swap )
                tmp = qbswap(tmp);
            lines[i] = tmp + firstline;
        }
    }else
    {
        // 4 bytes per number
        int j = 0;
        quint32 tmp;
        for( int i = 0; i < sizebc; i++, j += 4 )
        {
            memcpy( &tmp, buf.constData()+j, 4 );
            if( swap )
                tmp = qbswap(tmp);
            lines[i] = tmp + firstline;
        }
    }
    return lines;
}

static void readNames(QIODevice* in, int len, int sizeuv, QByteArrayList& ups, QList<JitBytecode::Function::Var>& vars )
{
    if( len == 0 )
        return;
    const QByteArray tmp = in->read(len);
    int pos = 0;
    // the upvalue part is just a sequence of zero terminated strings
    for( int i = 0; i < sizeuv; i++ )
    {
        int old = pos;
        pos = tmp.indexOf(char(0),pos);
        if( pos == -1 )
        {
            qCritical() << "invalid upval debug info";
            return;
        }
        ups.append( tmp.mid(old,pos-old));
        pos++;
    }
    // the var part is a sequence of records terminated by zero
    // each record is a sequence of a zero terminated string or a VARNAME, and then two uleb128 numbers
    quint32 lastpc = 0;
    while( true )
    {
        if( tmp.isEmpty() || tmp[pos] == 0 )
            break;
        JitBytecode::Function::Var var;
        if( tmp[pos] > VARNAME__MAX )
        {
            int old = pos;
            pos = tmp.indexOf(char(0),pos);
            if( pos == -1 )
            {
                qCritical() << "invalid upval debug info";
                return;
            }
            var.d_name = tmp.mid(old,pos-old);
        }else
            var.d_name = s_varname[quint8(tmp[pos])];
        pos++;
        // TODO: is this correct? there seems to be an n:m relation between names and slot numbers
        lastpc = var.d_startpc = lastpc + debug_read_uleb128( (const quint8*)tmp.constData(), pos );
        var.d_endpc = var.d_startpc + debug_read_uleb128( (const quint8*)tmp.constData(), pos );
        vars.append( var );
    }
}

JitBytecode::JitBytecode(QObject *parent) : QObject(parent)
{
    //for( int i = 0; i < BC__MAX; i++ )
    //   qDebug() << QString("OP_%1, ").arg(s_byteCodes[i].d_op).toUtf8().constData();
}

bool JitBytecode::parse(const QString& file)
{
    QFile in(file);
    if( !in.open(QIODevice::ReadOnly) )
        return error( tr("cannot open file for reading: %1").arg(file) );
    return parse(&in);
}

bool JitBytecode::parse(QIODevice* in, const QString& path)
{
    Q_ASSERT( in != 0 );
    d_name.clear();
    d_funcs.clear();
    d_fstack.clear();
    d_flags = 0;

    if( !parseHeader(in) )
        return false;

    if( d_name.isEmpty() )
        d_name = path;

    while( !in->atEnd() )
    {
        if( !parseFunction(in) )
            break; // eof
    }
    if( getRoot() )
        getRoot()->d_isRoot = true;
    return true;
}

JitBytecode::Function* JitBytecode::getRoot() const
{
    if( d_fstack.size() == 1 )
        return d_fstack.first().data();
    else
        return 0;
}

bool JitBytecode::isStripped() const
{
    return d_flags & BCDUMP_F_STRIP;
}

JitBytecode::Instruction JitBytecode::dissectInstruction(quint32 i)
{
    Instruction res;
    const int op = bc_op(i);
    if( op >= 0 && op < BC__MAX )
    {
        const _ByteCode& bc = s_byteCodes[op];
        res.d_name = bc.d_op;
        res.d_op = op;
        res.d_ta = bc.d_fa;
        res.d_tb = bc.d_fb;
        res.d_tcd = bc.d_fcd;
        if( bc.d_fa != Instruction::Unused )
            res.d_a = bc_a(i);
        if( bc.d_fb != Instruction::Unused )
        {
            res.d_b = bc_b(i);
            if( bc.d_fcd != Instruction::Unused )
                res.d_cd = bc_c(i);
        }else if( bc.d_fcd != Instruction::Unused )
            res.d_cd = (i) >>16;
    }else
        res.d_name = "???";
    return res;
}

JitBytecode::Format JitBytecode::formatFromOp(quint8 op)
{
    if( op < BC__MAX && s_byteCodes[op].d_fb != Instruction::Unused )
        return ABC;
    else
        return AD;
}

JitBytecode::Instruction::FieldType JitBytecode::typeCdFromOp(quint8 op)
{
    if( op < BC__MAX )
        return (Instruction::FieldType)s_byteCodes[op].d_fcd;
    else
        return Instruction::Unused;
}

JitBytecode::Instruction::FieldType JitBytecode::typeBFromOp(quint8 op)
{
    if( op < BC__MAX )
        return (Instruction::FieldType)s_byteCodes[op].d_fb;
    else
        return Instruction::Unused;
}

bool JitBytecode::parseHeader(QIODevice* in)
{
    QByteArray buf = in->read(4);
    if( buf.size() < 4 )
        return error("file too short, invalid header");

    if( buf[0] != BCDUMP_HEAD1 || buf[1] != BCDUMP_HEAD2 || buf[2] != BCDUMP_HEAD3 )
        return error("invalid header format");

    if( buf[3] != BCDUMP_VERSION )
        return error("wrong version");

    d_flags = bcread_uleb128(in);

    if ((d_flags & ~(BCDUMP_F_KNOWN)) != 0)
        return error("unknown dump");
    if ((d_flags & BCDUMP_F_FFI))
        return error("FFI dumps not supported");

    if( (d_flags & BCDUMP_F_STRIP) == 0 )
    {
        const quint32 len = bcread_uleb128(in);
        d_name = in->read(len); // "@test.lua"
    }

    return true;
}

bool JitBytecode::parseFunction(QIODevice* in )
{
    /* Read length. */
    quint32 len = bcread_uleb128(in);
    if (!len)
        return false;  /* EOF */

    FuncRef fr( new Function() );
    Function& f = *fr.data();
    f.d_sourceFile = d_name;
    f.d_id = d_funcs.size();
    /* Read prototype header. */
    f.d_flags = readByte(in);
    f.d_numparams = readByte(in);
    f.d_framesize = readByte(in);
    const quint8 sizeuv = readByte(in);
    const quint32 sizekgc = bcread_uleb128(in);
    const quint32 sizekn = bcread_uleb128(in);
    const quint32 sizebc = bcread_uleb128(in);

    const quint32 sizedbg = (d_flags & BCDUMP_F_STRIP) ? 0: bcread_uleb128(in);
    f.d_firstline = sizedbg ? bcread_uleb128(in) : 0;
    f.d_numline = sizedbg ? bcread_uleb128(in) : 0;

    const bool swap = ( d_flags & BCDUMP_F_BE ) != ( QSysInfo::ByteOrder == QSysInfo::BigEndian );
    f.d_byteCodes = readCode(in, swap, sizebc);
    // Note: original prefixes bc with BC_FUNCV or BC_FUNCF and framesize, depending on flags PROTO_VARARG

    f.d_upvals = readUpval( in, swap, sizeuv );

    f.d_constObjs = readObjConsts( fr.data(), in, sizekgc );
    f.d_constNums = readNumConsts( in, sizekn );

    const quint32 sizeli = sizebc << (f.d_numline < 256 ? 0 : ( f.d_numline < 65536 ? 1 : 2 ) );
    f.d_lines = readLineNumbers( in, swap, sizedbg ? sizeli : 0, sizebc, f.d_numline, f.d_firstline ); // empty or one line nr per byteCodes entry

    readNames( in, sizedbg ? sizedbg - sizeli : 0, sizeuv, f.d_upNames, f.d_vars );

    d_funcs.append(fr);
    d_fstack.push_back(fr);
    return true;
}

bool JitBytecode::error(const QString& msg)
{
    qCritical() << msg;
    return false;
}

uint qHash(const QVariant& v, uint seed)
{
    switch( v.type() )
    {
    case QVariant::ByteArray:
        return qHash( v.toByteArray(), seed );
    case QVariant::Bool:
    case QVariant::UInt:
    case QVariant::ULongLong:
    case QVariant::Int:
    case QVariant::LongLong:
    case QVariant::Double:
        return qHash( v.toDouble() );
    default:
        break;
    }
    const QVariant::DataPtr& d = v.data_ptr();
    if( d.is_shared )
        return qHash( d.data.shared, seed );
    if( d.is_null )
        return qHash( 0, seed );
    return qHash( d.data.ull, seed );
}

QHash<QVariant, QVariant> JitBytecode::ConstTable::merged() const
{
    QHash<QVariant,QVariant> res = d_hash;
    for( int i = 1; i < d_array.size(); i++ ) // LJ includes index 0 with nil value in BCDUMP_KGC_TAB
        res.insert( i, d_array[i] );
    return res;
}

bool JitBytecode::isNumber(const QVariant& v)
{
    switch( v.type() )
    {
    case QVariant::UInt:
    case QVariant::ULongLong:
    case QVariant::Int:
    case QVariant::LongLong:
    case QVariant::Double:
        return true;
    default:
        return false;
    }
}

bool JitBytecode::isString(const QVariant& v)
{
    return v.type() == QVariant::String || v.type() == QVariant::ByteArray;
}

QPair<quint8, JitBytecode::Function*> JitBytecode::Function::getFuncSlotFromUpval(quint8 upval) const
{
    if( d_outer == 0 )
        return qMakePair(quint8(0),(Function*)0);
    if( isLocalUpval( upval ) )
        return qMakePair( getUpval(upval), d_outer );
    else
        return d_outer->getFuncSlotFromUpval(getUpval(upval));
}
