#if !defined(AFX_LUATERMINAL_H__E595EEF8_F8CE_454A_B661_7A2451EC2CF4__INCLUDED_)
#define AFX_LUATERMINAL_H__E595EEF8_F8CE_454A_B661_7A2451EC2CF4__INCLUDED_

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

// adopted from NMR Application Framework, see https://github.com/rochus-keller/NAF

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
        void setSpecialInterpreter(bool on) { d_specialInterpreter = on; }
		Terminal2(QWidget*, Engine2 * = 0);
        virtual ~Terminal2();
    public slots:
        void printText(const QString& , bool err = false);
        void onClear();
    private:
        Lua::Engine2* d_lua;
		QTextCursor d_out;
		QString d_line;
		QStringList d_histo;
		QStringList d_next;
        QByteArray d_stdout, d_stderr;
        bool d_specialInterpreter;
	protected:
		void keyPressEvent ( QKeyEvent * e );
        void inputMethodEvent(QInputMethodEvent *);
        QString prompt() const;
		void updateFont( const QFont& );
        void printJitInfo();
        void handleStdoutErr( const QByteArray&, bool err );
    protected slots:
        void onNotify( int messageType, QByteArray val1 = "", int val2 = 0 );
        void handlePaste();
        void handleCopy();
        void handleSelectAll();
        void handleExportPdf();
        void handleSaveAs();
		void handlePrintStack();
		void handleSetFont();
	};
}

#endif // !defined(AFX_LUATERMINAL_H__E595EEF8_F8CE_454A_B661_7A2451EC2CF4__INCLUDED_)
