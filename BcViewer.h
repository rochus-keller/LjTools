#ifndef BCVIEWER_H
#define BCVIEWER_H

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
    class BcViewer : public QTreeWidget
    {
        Q_OBJECT
    public:
        explicit BcViewer(QWidget *parent = 0);
        bool loadFrom( const QString& );
        bool loadFrom( QIODevice*, const QString& path = QString() );
        void gotoLine(int);
        bool saveTo( const QString& );
    signals:
        void sigGotoLine(int lnr);
    protected slots:
        void onDoubleClicked(QTreeWidgetItem*,int);
        void onSelectionChanged();
    protected:
        QTreeWidgetItem* addFunc( const JitBytecode::Function*, QTreeWidgetItem* p = 0 );
        void fillTree();
    private:
        JitBytecode d_bc;
    };
}

#endif // BCVIEWER_H
