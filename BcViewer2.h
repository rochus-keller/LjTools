#ifndef BCVIEWER2_H
#define BCVIEWER2_H

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

#include <QTreeWidget>
#include "LuaJitBytecode.h"

class QTextStream;

namespace Lua
{
    class BcViewer2 : public QTreeWidget
    {
        Q_OBJECT
    public:
        explicit BcViewer2(QWidget *parent = 0);
        bool loadFrom( const QString&, const QString& source = QString() );
        bool loadFrom( QIODevice*, const QString& path = QString() );
        void gotoLine(quint32);
        void gotoFuncPc(quint32 func, quint32 pc, bool center, bool setMarker); // pc is one-based
        void clearMarker();
        bool saveTo( const QString&, bool stripped = false );
        void clear();
        void setLastWidth(int w) { d_lastWidth = w; }
        const QString& getPath() const { return d_path; }

        bool addBreakPoint( quint32 );
        bool removeBreakPoint( quint32 );
        struct Breakpoint
        {
            quint32 d_linePc;
            bool d_on;
        };
        bool toggleBreakPoint(Breakpoint* out = 0); // current line
        void clearBreakPoints();
        const QSet<quint32>& getBreakPoints() const { return d_breakPoints; }
    signals:
        void sigGotoLine(quint32 lnr);
    protected slots:
        void onDoubleClicked(QTreeWidgetItem*,int);
        void onSelectionChanged();
    protected:
        QTreeWidgetItem* addFunc( const JitBytecode::Function*, QTreeWidgetItem* p = 0 );
        QTreeWidgetItem* findItem( quint32 func, quint16 pc ) const;
        void fillTree();
    private:
        QString d_path;
        JitBytecode d_bc;
        typedef QMap<quint32,QList<QTreeWidgetItem*> > Items;
        Items d_items;
        QHash<quint32,QTreeWidgetItem*> d_funcs;
        QTreeWidgetItem* d_lastMarker;
        QSet<quint32> d_breakPoints;
        int d_lastWidth;
        bool d_lock;
    };
}

#endif // BCVIEWER2_H
