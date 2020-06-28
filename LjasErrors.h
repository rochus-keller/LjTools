#ifndef LJASERRORS_H
#define LJASERRORS_H

/*
* Copyright 2019 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the LjAsm parser library.
*
* The following is the license that applies to this copy of the
* library. For a license to use the library under conditions
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

#include <QObject>
#include <QReadWriteLock>
#include <QHash>
#include <QSet>
#include <QDir>

namespace Ljas
{
    class SynTree;

    class Errors : public QObject
    {
        // class is thread-safe
    public:
        enum Source { Lexer, Syntax, Semantics, Generator, Runtime };
        struct Entry
        {
            quint32 d_line;
            quint16 d_col;
            quint16 d_source;
            QString d_msg;
            QString d_file;
            bool d_isErr;
            Entry():d_isErr(true){}
        };
        typedef QList<Entry> EntryList;

        explicit Errors(QObject *parent = 0, bool threadExclusive = false );

        void error( Source, const SynTree*, const QString& msg );
        void error( Source, const QString& file, int line, int col, const QString& msg );
        void warning( Source, const SynTree*, const QString& msg );
        void warning( Source, const QString& file, int line, int col, const QString& msg );

        bool showWarnings() const;
        void setShowWarnings(bool on);
        bool reportToConsole() const;
        void setReportToConsole(bool on);
        bool record() const;
        void setRecord(bool on);
        void setRoot( const QDir& d ) { d_root = d; }

        quint32 getErrCount() const;
        quint32 getWrnCount() const;
        EntryList getAll() const;
        EntryList getErrors() const;
        EntryList getWarnings() const;
        EntryList getErrors(const QString& file) const;
        EntryList getWarnings(const QString& file) const;
        quint32 getSyntaxErrCount() const { return d_numOfSyntaxErrs; }

        void clear();

        static const char* sourceName(int);
    protected:
        void log( const Entry&, bool isErr );
    private:
        mutable QReadWriteLock d_lock;
        quint32 d_numOfErrs;
        quint32 d_numOfSyntaxErrs;
        quint32 d_numOfWrns;
        EntryList d_entries;
        QDir d_root;
        bool d_showWarnings;
        bool d_threadExclusive;
        bool d_reportToConsole;
        bool d_record;
    };

    inline uint qHash(const Errors::Entry & e, uint seed = 0) {
        return ::qHash(e.d_msg,seed) ^ ::qHash(e.d_line,seed) ^ ::qHash(e.d_col,seed) ^ ::qHash(e.d_source,seed);
    }
}

#endif // LJASERRORS_H
