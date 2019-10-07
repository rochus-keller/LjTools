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

#include "BcViewer.h"
#include <QHeaderView>
#include <QFile>
#include <QTextStream>
#include <QtDebug>
using namespace Lua;

#define LINEAR

enum { LnrType = 10 };

BcViewer::BcViewer(QWidget *parent) : QTreeWidget(parent)
{
    setHeaderHidden(false);
    setAlternatingRowColors(true);
    setColumnCount(6);
    setExpandsOnDoubleClick(false);
    setHeaderLabels( QStringList() << "what" << "idx" << "lnr/pc" << "lnr/pc/A" << "pars/B" << "frms/C/D");
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0,QHeaderView::Stretch);
    header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(4,QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(5,QHeaderView::ResizeToContents);

    connect(this,SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),this,SLOT(onDoubleClicked(QTreeWidgetItem*,int)));
    //connect(this,SIGNAL(itemSelectionChanged()),this,SLOT(onSelectionChanged()));
}

bool BcViewer::loadFrom(const QString& path)
{
    if( !d_bc.parse(path) )
        return false;

    fillTree();

    return true;
}

bool BcViewer::loadFrom(QIODevice* in, const QString& path)
{
    if( !d_bc.parse(in,path) )
        return false;

    fillTree();

    return true;
}

void BcViewer::gotoLine(int lnr)
{
    QList<QTreeWidgetItem *> items = findItems( QString::number(lnr), Qt::MatchExactly | Qt::MatchRecursive, 2 );
    foreach( QTreeWidgetItem * i, items )
    {
        if( i->type() == LnrType )
        {
            scrollToItem(i);
            setCurrentItem(i);
            i->setSelected(true);
            break;
        }
    }
}

bool BcViewer::saveTo(const QString& path)
{
    QFile f(path);
    if( !f.open(QIODevice::WriteOnly) )
    {
        qCritical() << "cannot write to" << path;
        return false;
    }
    QTextStream out(&f);
    out.setCodec("UTF-8");

    for( int i = 0; i < d_bc.getFuncs().size(); i++ )
    {
        if( !writeFunc( out, d_bc.getFuncs()[i].constData() ) )
            return false;
    }

    return true;
}

void BcViewer::onDoubleClicked(QTreeWidgetItem* i, int)
{
    if( i && i->type() == LnrType )
        emit sigGotoLine(i->text(2).toUInt());
}

void BcViewer::onSelectionChanged()
{
    onDoubleClicked(currentItem(),0);
}

QTreeWidgetItem* BcViewer::addFunc(const JitBytecode::Function* fp, QTreeWidgetItem* p)
{
    QFont bold = font();
    bold.setBold(true);
    QFont ul = font();
    ul.setUnderline(true);

    const JitBytecode::Function& f = *fp;
    QTreeWidgetItem* fi = p != 0 ? new QTreeWidgetItem(p,LnrType) : new QTreeWidgetItem(this,LnrType);
#ifdef LINEAR
    QString top;
    if( f.d_isRoot )
        top = " top";
    fi->setText(0,tr("Function %1%2").arg(f.d_id).arg(top));
#else
    fi->setText(0,tr("Function"));
#endif
    fi->setText(1,QString::number(f.d_id));
    fi->setFont(0,bold);
    fi->setText(2,QString::number(f.d_firstline));
    fi->setText(3,QString::number(f.d_firstline+f.d_numline));
    if( f.d_flags & 0x02 )
        fi->setText(4,QString("%1+varg").arg(f.d_numparams));
    else
        fi->setText(4,QString::number(f.d_numparams));
    fi->setText(5,QString::number(f.d_framesize));

    QTreeWidgetItem* t;

    if( false ) // f.d_flags )
    {
        t = new QTreeWidgetItem(fi);
        QStringList str;
        if( f.d_flags & 0x01 )
            str << "subfuncs";
        if( f.d_flags & 0x02 )
            str << "varargs";
        if( f.d_flags & 0x04 )
            str << "uses ffi";
        if( f.d_flags & 0x08 )
            str << "no jit";
        if( f.d_flags & 0x10 )
            str << "patched";
        t->setText(0,str.join(", "));
    }


    if( ! f.d_constObjs.isEmpty() )
    {
        t = new QTreeWidgetItem(fi);
        t->setText(0,tr("Const GC"));
        t->setFont(0,ul);
        for( int j = 0; j < f.d_constObjs.size(); j++ )
        {
            QTreeWidgetItem* ci = 0;
            if( f.d_constObjs[j].canConvert<JitBytecode::FuncRef>() )
            {
                JitBytecode::FuncRef fp = f.d_constObjs[j].value<JitBytecode::FuncRef>();
#ifdef LINEAR
                ci = new QTreeWidgetItem(t,LnrType);
                ci->setText(0,tr("function %1").arg(fp->d_id));
                ci->setText(2,QString::number(fp->d_firstline));
                ci->setText(3,QString::number(fp->d_firstline+fp->d_numline));
#else
                ci = addFunc(fp,t);
#endif
            }else if( f.d_constObjs[j].canConvert<JitBytecode::ConstTable>() )
            {
                ci = new QTreeWidgetItem(t);
                ci->setText(0,tr("table"));
            }else
            {
                ci = new QTreeWidgetItem(t);
                ci->setText(0,QString("'%1'").arg(f.d_constObjs[j].toString()));
            }
            ci->setText(1,QString::number(j));
        }
    }

    if( ! f.d_constNums.isEmpty() )
    {
        t = new QTreeWidgetItem(fi);
        t->setText(0,tr("Const Number"));
        t->setFont(0,ul);
        for( int j = 0; j < f.d_constNums.size(); j++ )
        {
            QTreeWidgetItem* ci = new QTreeWidgetItem(t);
            ci->setText(0,f.d_constNums[j].toString());
            ci->setText(1,QString::number(j));
        }
    }

    if( ! f.d_upvals.isEmpty() )
    {
        t = new QTreeWidgetItem(fi);
        t->setText(0,tr("Upvals"));
        t->setFont(0,ul);
        for( int j = 0; j < f.d_upvals.size(); j++ )
        {
            QTreeWidgetItem* ci = new QTreeWidgetItem(t);
            const quint16 up = f.getUpval(j);
            // an upvalue points into the upvalue or the var list of the function where FNEW is executed
            QString options;
            if( f.isLocalUpval(j) )
                options += "loc ";
            if( f.isImmutableUpval(j) )
                options += "ro";
            if( !f.d_upNames.isEmpty() )
            {
                Q_ASSERT( f.d_upNames.size() == f.d_upvals.size() );
                ci->setText(0,QString("%1 (%2) %3").arg(f.d_upNames[j].constData()).arg(up).arg(options));
            }else
                ci->setText(0,QString("%1 %2").arg(up).arg(options));
            ci->setText(1,QString::number(j));
        }
    }

    if( ! f.d_vars.isEmpty() )
    {
        t = new QTreeWidgetItem(fi);
        t->setText(0,tr("Vars"));
        t->setFont(0,ul);
        for( int j = 0; j < f.d_vars.size(); j++ )
        {
            QTreeWidgetItem* ci = new QTreeWidgetItem(t);
            ci->setText(0,f.d_vars[j].d_name.constData());
            ci->setText(1,QString::number(j));
            ci->setText(2,QString::number(f.d_vars[j].d_startpc));
            ci->setText(3,QString::number(f.d_vars[j].d_endpc));
        }
    }

    if( ! f.d_byteCodes.isEmpty() )
    {
        t = new QTreeWidgetItem(fi);
        t->setText(0,tr("Code"));
        t->setFont(0,ul);
        for( int j = 0; j < f.d_byteCodes.size(); j++ )
        {
            QTreeWidgetItem* ci = new QTreeWidgetItem(t,LnrType);
            const JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f.d_byteCodes[j]);
            ci->setText(0,bc.d_name);
            ci->setText(1,QString::number(j));
            if( !f.d_lines.isEmpty() )
            {
                Q_ASSERT( f.d_byteCodes.size() == f.d_lines.size() );
                ci->setText(2,QString::number(f.d_lines[j]));
            }
            if( bc.d_ta != JitBytecode::Instruction::Unused )
                ci->setText(3,QString("%1(%2)").arg(JitBytecode::Instruction::s_typeName[bc.d_ta]).arg(bc.d_a));
            if( bc.d_tb != JitBytecode::Instruction::Unused )
                ci->setText(4,QString("%1(%2)").arg(JitBytecode::Instruction::s_typeName[bc.d_tb]).arg(bc.d_b));
            if( bc.d_tcd != JitBytecode::Instruction::Unused )
                ci->setText(5,QString("%1(%2)").arg(JitBytecode::Instruction::s_typeName[bc.d_tcd]).arg( bc.getCd() ));
        }
    }
    return fi;
}

static inline QByteArray ws(int level)
{
    return QByteArray(level,'\t');
}
static inline QByteArray reg(int i )
{
    return QByteArray::number(i); // QString("R(%1)").arg(i).toUtf8();
}
static inline QByteArray up(int i )
{
    return QByteArray::number(i); // QString("U(%1)").arg(i).toUtf8();
}

static QByteArray tostring(const QVariant& v)
{
    if( JitBytecode::isString( v ) )
        return "\"" + v.toByteArray() + "\"";
    else if( JitBytecode::isNumber( v ) )
        return QByteArray::number( v.toDouble() );
    else
        return v.toByteArray();
}

bool BcViewer::writeFunc(QTextStream& out, const JitBytecode::Function* f, int level)
{
    out << ws(level) << "function F" << f->d_id;
    if( !d_bc.isStripped() )
        out << "\t-- lines "<< f->d_firstline << " to " << f->d_firstline + f->d_numline;
    out << endl;
    level++;
    for( int j = 0; j < f->d_vars.size(); j++ )
    {
        if( !f->d_vars[j].d_name.startsWith('('))
            out << ws(level) << "name " << f->d_vars[j].d_name << " " << reg(j) << endl;
    }
    for( int j = 0; j < f->d_upNames.size(); j++ )
    {
        out << ws(level) << "name " << f->d_upNames[j] << " " << up(j) << endl;
    }
    if( !f->d_upvals.isEmpty() )
    {
        out << ws(level) << "upvals ";
        for( int j = 0; j < f->d_upvals.size(); j++ )
        {
            if( j != 0 )
                out << " ";
            out << ( f->isLocalUpval(j) ? ":" : "" ) << f->getUpval(j);
        }
        out << endl;
    }
    out << ws(level-1) << "begin" << endl;

    for( int j = 0; j < f->d_byteCodes.size(); j++ )
    {
        const JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f->d_byteCodes[j]);
        out << ws(level) << bc.d_name;
        const QByteArray a = renderArg(f,bc.d_ta,bc.d_a);
        const QByteArray b = renderArg(f,bc.d_tb,bc.d_b);
        const QByteArray c = renderArg(f,bc.d_tcd,bc.getCd());
        if( !a.isEmpty() )
            out << "\t" << a;
        if( !b.isEmpty() )
            out << "\t" << b;
        if( !c.isEmpty() )
            out << "\t" << c;
        out << endl;
    }

    level--;
    out << ws(level) << "end" << endl << endl;
    return true;
}

static inline QByteArray getPriConst(int i)
{
    switch(i)
    {
    case 1:
        return "false";
    case 2:
        return "true";
    default:
        return "nil";
    }
}

QByteArray BcViewer::renderArg(const JitBytecode::Function* f, int t, int v)
{
    switch( t )
    {
    case JitBytecode::Instruction::Unused:
        return QByteArray();
    case JitBytecode::Instruction::_var:
    case JitBytecode::Instruction::_dst:
    case JitBytecode::Instruction::_base:
    case JitBytecode::Instruction::_rbase:
        return v < f->d_vars.size() && !f->d_vars[v].d_name.startsWith('(') ? f->d_vars[v].d_name : reg(v);
    case JitBytecode::Instruction::_str:
        return tostring( f->d_constObjs[ f->d_constObjs.size() - v - 1] );
    case JitBytecode::Instruction::_num:
        return QByteArray::number( f->d_constNums[v].toDouble() );
    case JitBytecode::Instruction::_pri:
        return getPriConst(v);
    case JitBytecode::Instruction::_cdata:
        break; // ??
    case JitBytecode::Instruction::_lit:
    case JitBytecode::Instruction::_lits:
    case JitBytecode::Instruction::_jump:
        return QByteArray::number(v);
    case JitBytecode::Instruction::_uv:
        return v < f->d_upNames.size() ? f->d_upNames[v] : up(v);
    case JitBytecode::Instruction::_func:
        {
            JitBytecode::FuncRef fr = f->d_constObjs[ f->d_constObjs.size() - v - 1 ].value<JitBytecode::FuncRef>();
            if( fr.data() != 0 )
                return QString("F%1").arg(fr->d_id).toUtf8();
        }
        break;
    case JitBytecode::Instruction::_tab:
        if( f->d_constObjs[ f->d_constObjs.size() - v - 1 ].canConvert<JitBytecode::ConstTable>() )
        {
            QByteArray str;
            QTextStream out(&str);
            JitBytecode::ConstTable t = f->d_constObjs[ f->d_constObjs.size() - v - 1 ].value<JitBytecode::ConstTable>();
            if( !t.d_array.isEmpty() )
            {
                out << "[ ";
                for( int i = 1; i < t.d_array.size(); i++ ) // index starts with one, zero is empty
                {
                    if( i != 1 )
                        out << " ";
                    out << tostring( t.d_array[i] );
                }
                out << " ]";
            }
            if( !t.d_hash.isEmpty() )
            {
                out << "{ ";
                QHash<QVariant,QVariant>::const_iterator i;
                int n = 0;
                for( i = t.d_hash.begin(); i != t.d_hash.end(); ++i, n++ )
                {
                    if( n != 0 )
                        out << " ";
                    out << tostring( i.key() ) << " = " << tostring( i.value() );
                }
                out << " }";
            }
            out.flush();
            return str;
        }
        return "???";
    }
    return QByteArray();
}

void BcViewer::fillTree()
{
    clear();

#ifdef LINEAR
    for( int i = 0; i < d_bc.getFuncs().size(); i++ )
        addFunc( d_bc.getFuncs()[i].data() );
#else
    const JitBytecode::Function* root = bc.getRoot();
    if( root )
        addFunc( root );
#endif

    expandAll();
    resizeColumnToContents(1);
    resizeColumnToContents(2);
    resizeColumnToContents(3);
    resizeColumnToContents(4);
    resizeColumnToContents(5);
}

