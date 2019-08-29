/*
 * Copyright 2000-2019 Rochus Keller <mailto:rkeller@nmr.ch>
 *
 * This file is part of the CARA (Computer Aided Resonance Assignment,
 * see <http://cara.nmr.ch/>) NMR Application Framework (NAF) library.
 *
 * The following is the license that applies to this copy of the
 * library. For a license to use the library under conditions
 * other than those described here, please email to rkeller@nmr.ch.
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

#if !defined(AFX_LUATERMINAL_H__E595EEF8_F8CE_454A_B661_7A2451EC2CF4__INCLUDED_)
#define AFX_LUATERMINAL_H__E595EEF8_F8CE_454A_B661_7A2451EC2CF4__INCLUDED_

#include <QTextEdit>
#include <QTextCursor>
#include <LjTools/Engine2.h>

namespace Lua
{
    class Terminal2 : public QTextEdit
	{
        Q_OBJECT
	public:
		void paste();
		void clear();
		Terminal2(QWidget*, Engine2 * = 0);
        void printText(const QString& , bool err = false);
        virtual ~Terminal2();
	private:
        Lua::Engine2* d_lua;
		QTextCursor d_out;
		QString d_line;
		QStringList d_histo;
		QStringList d_next;
	protected:
		void keyPressEvent ( QKeyEvent * e );
        void inputMethodEvent(QInputMethodEvent *);
        QString prompt() const;
		void updateFont( const QFont& );
        void printJitInfo();
    protected slots:
        void onNotify( int messageType, QByteArray val1 = "", int val2 = 0 );
        void handlePaste();
        void handleCopy();
        void handleSelectAll();
        void handleDelete();
        void handleExportPdf();
        void handleSaveAs();
		void handlePrintStack();
		void handleSetFont();
	};
}

#endif // !defined(AFX_LUATERMINAL_H__E595EEF8_F8CE_454A_B661_7A2451EC2CF4__INCLUDED_)
