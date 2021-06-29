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

#include "LuaJitBytecode.h"
#include <QtDebug>
#include <QFile>
#include <QtEndian>
#include <lj_bc.h>
#include <QBuffer>
#include "StreamSpy.h"
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
    lua_Number d;
    struct {
    quint32 lo;
    quint32 hi;
    };
};

typedef uint8_t BCReg;
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

/* Add ULEB128 value to buffer. */
static void bcwrite_uleb128(QIODevice* out, uint32_t v)
{
  for (; v >= 0x80; v >>= 7)
    out->putChar( (char)((v & 0x7f) | 0x80) );
  out->putChar( v );
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

static inline quint8 readByte(QIODevice* in)
{
    char ch;
    if( in->getChar(&ch) )
        return quint8(ch);
    return 0;
}

static inline void writeByte(QIODevice* out, quint8 b)
{
    out->putChar((char)b);
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
  //else
  Q_ASSERT( tp == BCDUMP_KTAB_NIL );
  return QVariant();
}

JitBytecode::VariantList JitBytecode::readObjConsts( Function* f, QIODevice* in, quint32 len )
{
    VariantList res(len);
    for( int i = 0; i < len; i++ )
    {
        const quint32 tp = bcread_uleb128(in);
        if( tp >= BCDUMP_KGC_STR )
        {
            quint32 len = tp - BCDUMP_KGC_STR;
            const QByteArray str = in->read(len);
            res[i] = str;
        }else if( tp == BCDUMP_KGC_TAB )
        {
            ConstTable tbl;
            const quint32 narray = bcread_uleb128(in);
            const quint32 nhash = bcread_uleb128(in);
            if( narray )
            {
                for (int j = 0; j < narray; j++)
                  tbl.d_array << bcread_ktabk(in);
                // first item is always nil
                tbl.d_array.pop_front();
            }
            if( nhash )
            {
                for ( int j = 0; j < nhash; j++)
                  tbl.d_hash.insert( bcread_ktabk(in), bcread_ktabk(in) );
            }
            res[i] = QVariant::fromValue(tbl);
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
                res[i] = QVariant::fromValue(r);
                d_fstack.pop_back();
            }
        }
    }
    return res;
}

static JitBytecode::VariantList readNumConsts( QIODevice* in, quint32 len )
{
    JitBytecode::VariantList res(len);
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
            res[i] = u.d;


#endif
        } else {
            res[i] = lo;
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

static QByteArray readNames(QIODevice* in, int len, int sizeuv, QByteArrayList& ups, QList<JitBytecode::Function::Var>& vars )
{
    if( len == 0 )
        return QByteArray();
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
            return QByteArray();
        }
        ups.append( tmp.mid(old,pos-old));
        pos++;
    }
    const QByteArray rawVars = tmp.mid(pos);
    // interpreted from debug_varname in lj_debug.c
    // the var part is a sequence of records terminated by zero
    // each record is a sequence of a zero terminated string or a VARNAME, and then two uleb128 numbers
    quint32 lastpc = 0;
    while( true )
    {
        if( tmp.size() <= pos || tmp[pos] == 0 )
            break;
        JitBytecode::Function::Var var;
        if( tmp[pos] > VARNAME__MAX )
        {
            int old = pos;
            pos = tmp.indexOf(char(0),pos);
            if( pos == -1 )
            {
                qCritical() << "invalid upval debug info";
                return QByteArray();
            }
            var.d_name = tmp.mid(old,pos-old);
        }else
            var.d_name = s_varname[quint8(tmp[pos])];
        pos++;
        // there is an n:m relation between names and slot numbers
        lastpc = var.d_startpc = lastpc + debug_read_uleb128( (const quint8*)tmp.constData(), pos );
        var.d_endpc = var.d_startpc + debug_read_uleb128( (const quint8*)tmp.constData(), pos );
        vars.append( var );
    }
    return rawVars;
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

bool JitBytecode::write(QIODevice* out, const QString& path)
{
    if( d_fstack.size() != 1 )
        return false;
    writeHeader(out);
    writeFunction(out,d_fstack.first().data());
    out->putChar(0);
    return true;
}

bool JitBytecode::write(const QString& file)
{
    QFile out(file);
    if( !out.open(QIODevice::WriteOnly|QIODevice::Unbuffered) )
        return error( tr("cannot open file for writing: %1").arg(file) );
//    OutStreamSpy spy(&out);
//    return write(&spy);
    return write(&out);
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

void JitBytecode::calcVarNames()
{
    for( int i = 0; i < d_funcs.size(); i++ )
    {
        d_funcs[i]->calcVarNames();
    }
}

void JitBytecode::clear()
{
    d_funcs.clear();
    d_fstack.clear();
    d_name.clear();
    d_flags = 0;
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

JitBytecode::Op JitBytecode::opFromBc(quint32 i)
{
    return (Op) bc_op(i);
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

JitBytecode::Instruction::FieldType JitBytecode::typeAFromOp(quint8 op)
{
    if( op < BC__MAX )
        return (Instruction::FieldType)s_byteCodes[op].d_fa;
    else
        return Instruction::Unused;
}

bool JitBytecode::parseHeader(QIODevice* in)
{
    const QByteArray buf = in->read(4);
    const QString err = checkFileHeader(buf);
    if( !err.isEmpty() )
        return error(err);

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

bool JitBytecode::writeHeader(QIODevice* out)
{
    writeByte(out,BCDUMP_HEAD1);
    writeByte(out,BCDUMP_HEAD2);
    writeByte(out,BCDUMP_HEAD3);
    writeByte(out,BCDUMP_VERSION);
    writeByte( out, ( isStripped() ? BCDUMP_F_STRIP : 0 ) +
               ( QSysInfo::ByteOrder == QSysInfo::BigEndian ? BCDUMP_F_BE : 0 ) );
    if( !isStripped() )
    {
        const QByteArray name = d_name.toUtf8();
        bcwrite_uleb128(out, name.size() );
        out->write( name );
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

static void writeUpval( QIODevice* out, const JitBytecode::UpvalList& l )
{
    static const int codeLen = 2;

    char buf[2];
    foreach( quint16 uv, l )
    {
        memcpy( buf, &uv, codeLen );
        out->write(buf,codeLen);
    }
}

bool JitBytecode::writeFunction(QIODevice* out, JitBytecode::Function* f)
{
    for( int i = f->d_constObjs.size() - 1; i >= 0; i-- )
    {
        const QVariant&v = f->d_constObjs[i];
        if( v.canConvert<FuncRef>() )
        {
            f->d_flags |= PROTO_CHILD;
            writeFunction(out,v.value<FuncRef>().data() );
        }
    }

    QBuffer tmp;
    tmp.open(QIODevice::WriteOnly);

    /* Write prototype header. */
    writeByte(&tmp,f->d_flags & (PROTO_CHILD|PROTO_VARARG|PROTO_FFI));
    writeByte(&tmp,f->d_numparams);
    writeByte(&tmp,f->d_framesize);

    writeByte(&tmp,f->d_upvals.size());
    // sizekgc
    bcwrite_uleb128(&tmp, f->d_constObjs.size() );
    // sizekn
    bcwrite_uleb128(&tmp, f->d_constNums.size() );
    // sizebc
    bcwrite_uleb128(&tmp, f->d_byteCodes.size() );

    QByteArray dbgInfo;
    if( !isStripped() )
    {
        dbgInfo = writeDbgInfo(f);
        bcwrite_uleb128(&tmp,dbgInfo.size());
        if( !dbgInfo.isEmpty() )
        {
            bcwrite_uleb128(&tmp, f->d_firstline);
            bcwrite_uleb128(&tmp, f->d_numline);
        }
    }

    writeByteCodes(&tmp,f->d_byteCodes);
    writeUpval(&tmp,f->d_upvals);

    writeObjConsts( &tmp, f->d_constObjs );
    writeNumConsts( &tmp, f->d_constNums );

    // LineNumbers, Names
    tmp.write(dbgInfo);

    tmp.close();
    bcwrite_uleb128(out,tmp.data().size());
    out->write(tmp.data());

    return true;
}

QByteArray JitBytecode::writeDbgInfo(JitBytecode::Function* f)
{
    QBuffer buf;
    buf.open(QIODevice::WriteOnly|QIODevice::Unbuffered);

    char b[4];
    if( f->d_numline < 256 )
    {
        // 1 byte per number
        quint8 tmp;
        quint32 len;
        for( int i = 0; i < f->d_lines.size(); i++ )
        {
            len = f->d_lines[i] == 0 ? 0 : f->d_lines[i] - f->d_firstline;
            if( len >= 256 )
                qWarning() << "1 byte line number overflow at" << f->d_sourceFile << f->d_lines[i] << len;
            tmp = len;
            writeByte( &buf, tmp );
        }
    }else if( f->d_numline < 65536 )
    {
        // 2 bytes per number
        quint16 tmp;
        quint32 len;
        for( int i = 0; i < f->d_lines.size(); i++ )
        {
            len = f->d_lines[i] == 0 ? 0 : f->d_lines[i] - f->d_firstline;
            if( len >= 65536 )
                qWarning() << "2 byte line number overflow at" << f->d_sourceFile << f->d_lines[i] << len;
            tmp = len;
            memcpy( b, &tmp, sizeof(tmp) );
            buf.write(b,sizeof(tmp));
        }
    }else
    {
        // 4 bytes per number
        quint32 tmp;
        for( int i = 0; i < f->d_lines.size(); i++ )
        {
            tmp = f->d_lines[i] == 0 ? 0 : f->d_lines[i] - f->d_firstline;
            memcpy( b, &tmp, sizeof(tmp) );
            buf.write(b,sizeof(tmp));
        }
    }

    // the upvalue part is just a sequence of zero terminated strings
    for( int i = 0; i < f->d_upNames.size(); i++ )
    {
        buf.write(f->d_upNames[i]);
        buf.putChar(0);
    }

    quint32 lastpc = 0;
    for( int i = 0; i < f->d_vars.size(); i++ )
    {
        if( f->d_vars[i].d_name.startsWith('(') )
        {
            int code = -1;
            for( int j = 1; j < VARNAME__MAX; j++ )
            {
                if( s_varname[j] == f->d_vars[i].d_name )
                {
                    code = j;
                    break;
                }
            }
            Q_ASSERT(code > 0);
            writeByte(&buf,code);
        }else
        {
            buf.write(f->d_vars[i].d_name);
            buf.putChar(0);
        }
        bcwrite_uleb128(&buf, f->d_vars[i].d_startpc - lastpc );
        lastpc = f->d_vars[i].d_startpc;
        bcwrite_uleb128(&buf, f->d_vars[i].d_endpc - f->d_vars[i].d_startpc );
    }
    buf.putChar(0);

    buf.close();
    return buf.data();
}

static int32_t lj_num2bit(lua_Number n)
{
  TValue o;
  o.d = n + 6755399441055744.0;  /* 2^52 + 2^51 */
  return (int32_t)o.lo;
}

bool JitBytecode::writeNumConsts(QIODevice* out, const VariantList& l)
{
    // adopted from LuaJIT lj_bcwrite
    for( int i = 0; i < l.size(); i++ )
    {
        /* Write a 33 bit ULEB128 for the int (lsb=0) or loword (lsb=1). */
        /* Narrow number constants to integers. */
        TValue o;
        int32_t k;
        bool isInt = false;
        if( l[i].type() == QVariant::Int )
        {
            k = l[i].toInt();
            isInt = true;
        }else
        {
            o.d = l[i].toDouble();
            const lua_Number num = o.d;
            k = lj_num2bit(num);
            isInt = ( num == (lua_Number)k );
        }
        if (isInt) {  /* -0 is never a constant. */
            QBuffer tmp;
            tmp.open(QIODevice::WriteOnly|QIODevice::Unbuffered);
            bcwrite_uleb128(&tmp, 2*(uint32_t)k | ((uint32_t)k & 0x80000000u));
            tmp.close();
            if (k < 0) {
                char *p = tmp.buffer().data() + tmp.buffer().size() - 1;
                *p = (*p & 7) | ((k>>27) & 0x18);
            }
            out->write(tmp.buffer());
            continue;
        }
        QBuffer tmp;
        tmp.open(QIODevice::WriteOnly|QIODevice::Unbuffered);
        bcwrite_uleb128(&tmp, 1+(2*o.lo | (o.lo & 0x80000000u)));
        if (o.lo >= 0x80000000u) {
            char *p = tmp.buffer().data() + tmp.buffer().size() - 1;
            *p = (*p & 7) | ((o.lo>>27) & 0x18);
        }
        out->write(tmp.buffer());
        bcwrite_uleb128(out, o.hi);
    }
    return true;
}

static void bcwrite_ktabk(QIODevice* out, const QVariant& v, bool narrow )
{
    if( v.type() == QVariant::ByteArray )
    {
        const QByteArray str = v.toByteArray();
        bcwrite_uleb128(out,BCDUMP_KGC_STR + str.size() );
        out->write(str);
    }else if( v.type() == QVariant::Int )
    {
        writeByte(out, BCDUMP_KTAB_INT);
        bcwrite_uleb128(out, v.toInt());
    }else if( v.type() == QVariant::Bool )
    {
        if( v.toBool() )
            writeByte(out, BCDUMP_KTAB_TRUE);
        else
            writeByte(out, BCDUMP_KTAB_FALSE );
    }else if( v.isNull() )
        writeByte(out,BCDUMP_KTAB_NIL);
    else
    {
        if( narrow )
        {
            /* Narrow number constants to integers. */
            lua_Number num = v.toDouble();
            int32_t k = lj_num2bit(num);
            if (num == (lua_Number)k) {  /* -0 is never a constant. */
                writeByte(out, BCDUMP_KTAB_INT);
                bcwrite_uleb128(out, k);
                return;
            }
        }
        TValue o;
        o.d = v.toDouble();
        writeByte(out, BCDUMP_KTAB_NUM);
        bcwrite_uleb128(out, o.lo);
        bcwrite_uleb128(out, o.hi);
    }
}

static QByteArray unescape( QByteArray str )
{
    str.replace("\\\\", "\\" );
    str.replace("\\n", "\n" );
    str.replace("\\a", "\a" );
    str.replace("\\b", "\b" );
    str.replace("\\f", "\f" );
    str.replace("\\r", "\r" );
    str.replace("\\t", "\t" );
    str.replace("\\v", "\v" );
    str.replace("\\\"", "\"" );
    str.replace("\\'", "'" );
    return str;
}

bool JitBytecode::writeObjConsts(QIODevice* out, const VariantList& l)
{
    for( int i = 0; i < l.size(); i++ )
    {
        const QVariant& v = l[i];
        if( isString(v) )
        {
            const QByteArray str = unescape(v.toByteArray());
            bcwrite_uleb128(out,BCDUMP_KGC_STR + str.size() );
            out->write(str);
        }else if( v.canConvert<FuncRef>())
            bcwrite_uleb128(out,BCDUMP_KGC_CHILD);
        else if( v.canConvert<ConstTable>() )
        {
            ConstTable t = v.value<ConstTable>();
            bcwrite_uleb128(out,BCDUMP_KGC_TAB);
            if( !t.d_array.isEmpty() )
                bcwrite_uleb128(out,t.d_array.size()+1);
            else
                bcwrite_uleb128(out,0);
            bcwrite_uleb128(out,t.d_hash.size());
            if( !t.d_array.isEmpty() )
            {
                bcwrite_ktabk(out,QVariant(),true); // the first element is always null
                for( int i = 0; i < t.d_array.size(); i++ )
                    bcwrite_ktabk(out,t.d_array[i],true);
            }
            QHash<QVariant,QVariant>::const_iterator i;
            for( i = t.d_hash.begin(); i != t.d_hash.end(); ++i )
            {
                bcwrite_ktabk(out,i.key(),false);
                bcwrite_ktabk(out,i.value(),true);
            }
        }
    }
    return true;
}

bool JitBytecode::writeByteCodes(QIODevice* out, const JitBytecode::CodeList& l)
{
    char buf[4];
    for( int i = 0; i < l.size(); i++ )
    {
        const quint32 tmp = l[i];
        ::memcpy( buf, &tmp, 4 );
        out->write(buf,4);
    }
    return true;
}

bool JitBytecode::error(const QString& msg)
{
    qCritical() << msg;
    return false;
}

void JitBytecode::setStripped(bool on)
{
    if( on )
        d_flags = BCDUMP_F_STRIP;
    else
        d_flags = 0;
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
    for( int i = 0; i < d_array.size(); i++ )
        res.insert( i+1, d_array[i] ); // start index by 1 not 0
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
    case QMetaType::Float:
        return true;
    default:
        return false;
    }
}

bool JitBytecode::isString(const QVariant& v)
{
    return v.type() == QVariant::String || v.type() == QVariant::ByteArray;
}

bool JitBytecode::isPrimitive(const QVariant& v)
{
    return v.type() == QVariant::Bool || v.isNull();
}

quint8 JitBytecode::toPrimitive(const QVariant& v)
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

const char*JitBytecode::nameOfOp(int op)
{
    if( op < OP_ISLT || op >= OP_INVALID )
        return "???";
    const _ByteCode& bc = s_byteCodes[op];
    return bc.d_op;
}

QString JitBytecode::checkFileHeader(const QByteArray& buf)
{
    if( buf.size() < 4 )
        return "file too short, invalid header";

    if( buf[0] != char(BCDUMP_HEAD1) || buf[1] != char(BCDUMP_HEAD2) || buf[2] != char(BCDUMP_HEAD3) )
        return "invalid header format";

    if( buf[3] != char(BCDUMP_VERSION) )
        return "wrong version";
    return QString();
}

const JitBytecode::Function::Var* JitBytecode::Function::findVar(int pc, int slot, int* idx) const
{
    // lj_parse.c analysis:
    // At pc == 0 is BC_FUNCV, thus
    pc += 1;
    // then parse_chunk starts with ++ls->level syntactic level; pc is still 1 at start of first local
    // locals are parsed and at least KNIL is emitted for the consecutive batch of locals increasing pc to 2
    // then var_add is called and the startpc of each local is set to current pc, i.e. 2!!!
    // thus startpc points to the bytecode instruction after the first appearance of the var!!!
    pc += 1;

    // verified that the following behaves exactly like lj_debug.c debug_varname (if 'pc < v.d_endpc')

    if( idx )
        *idx = 0;
    foreach( const Var& v, d_vars )
    {
        if( idx )
            *idx += 1;
        if( v.d_startpc > pc)
            break;
        if( pc <= v.d_endpc && slot-- == 0 ) // original is '<'
        {
            return &v;
        }
    }
    return 0;
}

QByteArray JitBytecode::Function::getVarName(int pc, int slot, int* idx) const
{
    const Var* v = findVar(pc,slot,idx);
    if( v )
        return v->d_name;
    else
        return QByteArray();
}

void JitBytecode::Function::calcVarNames() const
{
    if( !d_varNames.isEmpty() )
        return;

    // Bruteforce approach (sorry)
    QVector<QByteArray> names(d_framesize);
    for( int pc = 0; pc < d_byteCodes.size(); pc++ )
    {
        for( int slot = 0; slot < d_framesize; slot++ )
        {
            const QByteArray name = getVarName(pc,slot);
            if( name.isEmpty() )
                continue;
            QByteArray& slotName = names[slot];
            if( slotName.isEmpty() || slotName.startsWith('(') )
                slotName = name;
        }
    }
    d_varNames = names.toList();
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


