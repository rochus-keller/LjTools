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
#include <QHeaderView>
#include <QFile>
#include <QTextStream>
#include <QtDebug>
#include <QDir>
using namespace Lua;

#define LINEAR

enum { LnrType = 10 };

static QString printRowCol( quint32 rowCol )
{
    if( JitComposer::isPacked(rowCol) )
        return QString("%1:%2").arg(JitComposer::unpackRow(rowCol)).arg(JitComposer::unpackCol(rowCol));
    else
        return QString::number(rowCol);
}

BcViewer2::BcViewer2(QWidget *parent) : QTreeWidget(parent)
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

bool BcViewer2::loadFrom(const QString& path)
{
    if( !d_bc.parse(path) )
        return false;

    // TEST
//    QFile orig(path);
//    QDir d;
//    d.remove("orig.bin");
//    orig.copy("orig.bin");
//    d_bc.write("generated.bin");

    fillTree();

    return true;
}

bool BcViewer2::loadFrom(QIODevice* in, const QString& path)
{
    if( !d_bc.parse(in,path) )
        return false;

    d_bc.calcVarNames();
    fillTree();

    return true;
}

void BcViewer2::gotoLine(quint32 lnr)
{
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
        scrollToItem(hit);
        setCurrentItem(hit);
        hit->setSelected(true);
        return;
    }

    if( currentItem() )
    {
        currentItem()->setSelected(false);
        setCurrentItem(0);
    }
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

void BcViewer2::onDoubleClicked(QTreeWidgetItem* i, int)
{
    if( i && i->type() == LnrType )
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
    if( !d_bc.isStripped() )
    {
        const quint32 lastline = f.d_firstline+f.d_numline-1;
        if( JitComposer::isPacked(f.d_firstline) )
        {
            fi->setText(2,QString::number(JitComposer::unpackRow(f.d_firstline)));
            fi->setText(3,QString::number(JitComposer::unpackRow(lastline)));
        }else
        {
            fi->setText(2,QString::number(f.d_firstline));
            fi->setText(3,QString::number(lastline));
        }
        fi->setData(2,Qt::UserRole,f.d_firstline);
        d_items[JitComposer::unpackRow2(f.d_firstline)].append(fi);
    }
    if( f.d_flags & 0x02 )
        fi->setText(4,QString("%1+varg").arg(f.d_numparams));
    else
        fi->setText(4,QString::number(f.d_numparams));
    fi->setText(5,QString::number(f.d_framesize));

    QTreeWidgetItem* t;

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
            JitBytecode::Instruction bc = JitBytecode::dissectInstruction(f.d_byteCodes[j]);

            QByteArray warning, mnemonic;
            Ljas::Disasm::OP op;
            Ljas::Disasm::adaptToLjasm(bc, op, warning );
            mnemonic = Ljas::Disasm::s_opName[op];
            ci->setText(0,mnemonic);
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

void BcViewer2::fillTree()
{
    clear();
    d_items.clear();

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
    setColumnWidth(5,90);
    //resizeColumnToContents(3);
    //resizeColumnToContents(4);
    //resizeColumnToContents(5);
}

