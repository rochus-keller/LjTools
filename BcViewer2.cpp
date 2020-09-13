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

#include "BcViewer2.h"
#include "LjDisasm.h"
#include "LuaJitComposer.h"
#include "Engine2.h"
#include <QHeaderView>
#include <QFile>
#include <QTextStream>
#include <QtDebug>
#include <QDir>
using namespace Lua;

#define LINEAR

enum { FuncType = 10, VarsType, CodeType, LineType };

static QString printRowCol( quint32 rowCol )
{
    if( JitComposer::isPacked(rowCol) )
        return QString("%1:%2").arg(JitComposer::unpackRow(rowCol)).arg(JitComposer::unpackCol(rowCol));
    else
        return QString::number(rowCol);
}

BcViewer2::BcViewer2(QWidget *parent) : QTreeWidget(parent),d_lock(false),d_lastWidth(90),d_lastMarker(0)
{
    setHeaderHidden(false);
    setAlternatingRowColors(true);
    setColumnCount(6);
    setExpandsOnDoubleClick(false);
    setHeaderLabels( QStringList() << "what" << "idx" << "lnr/pc" << "lnr/pc/A" << "pars/B" << "frms/C/D");
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0,QHeaderView::Stretch);
    /*
    header()->setSectionResizeMode(1,QHeaderView::Interactive);
    header()->setSectionResizeMode(2,QHeaderView::Interactive);
    header()->setSectionResizeMode(3,QHeaderView::Interactive);
    header()->setSectionResizeMode(4,QHeaderView::Interactive);
    header()->setSectionResizeMode(5,QHeaderView::Interactive);
    */

    connect(this,SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)),this,SLOT(onDoubleClicked(QTreeWidgetItem*,int)));
    //connect(this,SIGNAL(itemSelectionChanged()),this,SLOT(onSelectionChanged()));
}

bool BcViewer2::loadFrom(const QString& path, const QString& source)
{
    Q_ASSERT( !d_lock );
    if( !d_bc.parse(path) )
        return false;

    if( source.isEmpty() )
        d_path = path;
    else
        d_path = source;
    // TEST
//    QFile orig(path);
//    QDir d;
//    d.remove("orig.bin");
//    orig.copy("orig.bin");
//    d_bc.write("generated.bin");

    d_bc.calcVarNames();
    fillTree();

    return true;
}

bool BcViewer2::loadFrom(QIODevice* in, const QString& path)
{
    Q_ASSERT( !d_lock );
    if( !d_bc.parse(in,path) )
        return false;

    d_path = path;
    d_bc.calcVarNames();
    fillTree();

    return true;
}

void BcViewer2::gotoLine(quint32 lnr)
{
    Q_ASSERT( !d_lock );
    d_lock = true;
    Items::const_iterator i = d_items.find(JitComposer::unpackRow2(lnr));
    if( i != d_items.end() )
    {
        QTreeWidgetItem * hit = 0;
        foreach( QTreeWidgetItem * item, i.value() )
        {
            const quint32 cur = item->data(2,Qt::UserRole).toUInt();
            if( lnr == cur )
            {
                hit = item;
                break;
            }
            if( lnr < cur )
            {
                break;
            }
            hit = item;
        }
        if( hit == 0 )
        {
            hit = i.value().first();
        }
        scrollToItem(hit,QAbstractItemView::PositionAtCenter);
        setCurrentItem(hit);
        hit->setSelected(true);
        d_lock = false;
        return;
    }

    if( currentItem() )
    {
        currentItem()->setSelected(false);
        setCurrentItem(0);
    }
    d_lock = false;
}

void BcViewer2::gotoFuncPc(quint32 func, quint32 pc, bool center, bool setMarker)
{
    QTreeWidgetItem* found = findItem(func,pc);
    if( found )
    {
        setCurrentItem(found);
        scrollToItem(found, center ? QTreeWidget::PositionAtCenter : QTreeWidget::EnsureVisible );
        if( setMarker )
        {
            clearMarker();
            const quint32 l = Engine2::packDeflinePc( found->data(1,Qt::UserRole).toUInt(),
                                                      found->data(0,Qt::UserRole).toUInt()+1 );
            if( d_breakPoints.contains(l) )
                found->setIcon(0, QPixmap(":/images/break-marker.png"));
            else
                found->setIcon(0, QPixmap(":/images/marker.png"));
            d_lastMarker = found;
        }
    }
}

void BcViewer2::clearMarker()
{
    if( d_lastMarker )
    {
        const quint32 l = Engine2::packDeflinePc( d_lastMarker->data(1,Qt::UserRole).toUInt(),
                                                  d_lastMarker->data(0,Qt::UserRole).toUInt()+1 );
        if( d_breakPoints.contains(l) )
            d_lastMarker->setIcon(0, QPixmap(":/images/breakpoint.png"));
        else
            d_lastMarker->setIcon(0,QIcon() );
    }
    d_lastMarker = 0;
}

bool BcViewer2::saveTo(const QString& path, bool stripped)
{
    QFile f(path);
    if( !f.open(QIODevice::WriteOnly) )
    {
        qCritical() << "cannot write to" << path;
        return false;
    }
    return Ljas::Disasm::disassemble( d_bc, &f, QString(), stripped );
}

void BcViewer2::clear()
{
    Q_ASSERT( !d_lock );
    d_items.clear();
    d_funcs.clear();
    d_lastMarker = 0;
    QTreeWidget::clear();
}

bool BcViewer2::addBreakPoint(quint32 l)
{
    if( d_breakPoints.contains(l) )
        return false;
    QPair<quint32, quint16> rc = Engine2::unpackDeflinePc(l);

    QTreeWidgetItem* f = findItem(rc.first,rc.second);
    if( f == 0 )
        return false;
    d_breakPoints.insert(l);
    f->setIcon(0,QPixmap(":/images/breakpoint.png") );
    return true;
}

bool BcViewer2::removeBreakPoint(quint32 l)
{
    QSet<quint32>::iterator it = d_breakPoints.find(l);
    if( it == d_breakPoints.end() )
        return false;

    QPair<quint32, quint16> rc = Engine2::unpackDeflinePc(l);

    QTreeWidgetItem* i = findItem(rc.first,rc.second);
    if( i == 0 )
        return false;
    d_breakPoints.erase(it);
    i->setIcon(0,QIcon() );
    return true;
}

bool BcViewer2::toggleBreakPoint(Breakpoint* out)
{
    QTreeWidgetItem* cur = currentItem();
    if( cur == 0 || cur->type() != LineType )
        return false;
    const quint16 pc = cur->data(0,Qt::UserRole).toUInt();
    QTreeWidgetItem* p = cur->parent();
    Q_ASSERT(p);
    p = p->parent();
    Q_ASSERT(p && p->type() == FuncType );
    const quint32 def = p->data(0,Qt::UserRole).toUInt();
    const quint32 l = Engine2::packDeflinePc(def,pc+1);
    if( d_breakPoints.contains(l) )
    {
        if( out )
        {
            out->d_linePc = l;
            out->d_on = false;
        }
        return removeBreakPoint(l);
    }else
    {
        if( out )
        {
            out->d_linePc = l;
            out->d_on = true;
        }
        return addBreakPoint(l);
    }
}

void BcViewer2::clearBreakPoints()
{
    QSet<quint32> tmp = d_breakPoints;
    foreach( quint32 l, tmp )
        removeBreakPoint(l);
}

void BcViewer2::onDoubleClicked(QTreeWidgetItem* i, int)
{
    if( i && ( i->type() == FuncType || i->type() == LineType ) )
        emit sigGotoLine(i->data(2,Qt::UserRole).toUInt());
}

void BcViewer2::onSelectionChanged()
{
    onDoubleClicked(currentItem(),0);
}

QTreeWidgetItem* BcViewer2::addFunc(const JitBytecode::Function* fp, QTreeWidgetItem* p)
{
    QFont bold = font();
    bold.setBold(true);
    QFont ul = font();
    ul.setUnderline(true);

    const JitBytecode::Function& f = *fp;
    QTreeWidgetItem* fi = p != 0 ? new QTreeWidgetItem(p,FuncType) : new QTreeWidgetItem(this,FuncType);
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
    if( !d_bc.isStripped() )
    {
        const quint32 line = JitComposer::unpackRow2(f.d_firstline);
        fi->setText(2,QString::number(line));
        fi->setText(3,QString::number(JitComposer::unpackRow2(f.lastLine())));
        fi->setData(2,Qt::UserRole,f.d_firstline);
        fi->setData(0,Qt::UserRole,line);
        d_items[line].append(fi);
        d_funcs[line] = fi;
    }
    if( f.d_flags & 0x02 )
        fi->setText(4,QString("%1+varg").arg(f.d_numparams));
    else
        fi->setText(4,QString::number(f.d_numparams));
    fi->setText(5,QString::number(f.d_framesize));

    QTreeWidgetItem* t;

    if( ! f.d_upvals.isEmpty() )
    {
        t = new QTreeWidgetItem(fi, VarsType );
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
            if( j < f.d_upNames.size() )
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
        t = new QTreeWidgetItem(fi, VarsType);
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
        t = new QTreeWidgetItem(fi, CodeType );
        t->setText(0,tr("Code"));
        t->setFont(0,ul);
        for( int j = 0; j < f.d_byteCodes.size(); j++ )
        {
            QTreeWidgetItem* ci = new QTreeWidgetItem(t, LineType);
            JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f.d_byteCodes[j]);

            QByteArray warning, mnemonic;
            Ljas::Disasm::OP op;
            Ljas::Disasm::adaptToLjasm(bc, op, warning );
            mnemonic = Ljas::Disasm::s_opName[op];
            ci->setText(0,mnemonic);
            ci->setData(0,Qt::UserRole, j );
            ci->setData(1,Qt::UserRole,JitComposer::unpackRow2(f.d_firstline));
            ci->setToolTip(0, Ljas::Disasm::s_opHelp[op]);
            ci->setText(1,QString::number(j));
            if( !f.d_lines.isEmpty() )
            {
                Q_ASSERT( f.d_byteCodes.size() == f.d_lines.size() );
                ci->setText(2,printRowCol(f.d_lines[j]));
                ci->setData(2,Qt::UserRole,f.d_lines[j] );
                d_items[JitComposer::unpackRow2(f.d_lines[j])].append(ci);
            }
            ci->setText(3, Ljas::Disasm::renderArg(&f,bc.d_ta, bc.d_a, j, false, true ) );
            ci->setToolTip(3, ci->text(3) );
            ci->setText(4, Ljas::Disasm::renderArg(&f,bc.d_tb, bc.d_b, j, false, true ) );
            ci->setToolTip(4, ci->text(4) );
            ci->setText(5, Ljas::Disasm::renderArg(&f,bc.d_tcd, bc.getCd(), j, false, true ) );
            ci->setToolTip(5, ci->text(5) );
        }
    }
    return fi;
}

QTreeWidgetItem*BcViewer2::findItem(quint32 func, quint16 pc) const
{
    QTreeWidgetItem* item = d_funcs.value(JitComposer::unpackRow2(func));
    if( item == 0 )
        return 0;
    QTreeWidgetItem* found = 0;
    for(int i = 0; i < item->childCount(); i++ )
    {
        if( item->child(i)->type() == CodeType )
        {
            pc--;
            if( pc < item->child(i)->childCount() )
                found = item->child(i)->child(pc);
            break;
        }
    }
    return found;
}

void BcViewer2::fillTree()
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
    setColumnWidth(3,70);
    setColumnWidth(4,60);
    setColumnWidth(5,d_lastWidth);
    //resizeColumnToContents(3);
    //resizeColumnToContents(4);
    //resizeColumnToContents(5);
}

