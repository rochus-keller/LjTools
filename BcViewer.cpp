#include "BcViewer.h"
#include <QHeaderView>
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
    JitBytecode bc;
    const bool res = bc.parse(path);
    if( !res )
        return false;

    clear();

#ifdef LINEAR
    for( int i = 0; i < bc.getFuncs().size(); i++ )
        addFunc( bc.getFuncs()[i].data() );
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


    return res;
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
            const JitBytecode::ByteCode bc = JitBytecode::dissectByteCode(f.d_byteCodes[j]);
            ci->setText(0,bc.d_name);
            ci->setText(1,QString::number(j));
            if( !f.d_lines.isEmpty() )
            {
                Q_ASSERT( f.d_byteCodes.size() == f.d_lines.size() );
                ci->setText(2,QString::number(f.d_lines[j]));
            }
            if( bc.d_ta != JitBytecode::ByteCode::Unused )
                ci->setText(3,QString("%1(%2)").arg(JitBytecode::ByteCode::s_typeName[bc.d_ta]).arg(bc.d_a));
            if( bc.d_tb != JitBytecode::ByteCode::Unused )
                ci->setText(4,QString("%1(%2)").arg(JitBytecode::ByteCode::s_typeName[bc.d_tb]).arg(bc.d_b));
            if( bc.d_tcd != JitBytecode::ByteCode::Unused )
                ci->setText(5,QString("%1(%2)").arg(JitBytecode::ByteCode::s_typeName[bc.d_tcd]).arg( bc.getCd() ));
        }
    }
    return fi;
}

