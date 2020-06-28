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

#include "LjasErrors.h"
#include "LjasSynTree.h"
#include <QtDebug>
#include <QFileInfo>
using namespace Ljas;

Errors::Errors(QObject *parent, bool threadExclusive) :
    QObject(parent),
    d_numOfErrs(0),d_numOfWrns(0),d_showWarnings(true),d_threadExclusive(threadExclusive),
    d_reportToConsole(false),d_record(false),d_numOfSyntaxErrs(0)
{

}

void Errors::error(Errors::Source s, const SynTree* st, const QString& msg)
{
    Q_ASSERT( st != 0 );
    error( s, st->d_tok.d_sourcePath, st->d_tok.d_lineNr, st->d_tok.d_colNr, msg );
}

void Errors::error(Errors::Source s, const QString& file, int line, int col, const QString& msg)
{
    if( !d_threadExclusive ) d_lock.lockForWrite();
    Entry e;
    e.d_col = col;
    e.d_line = line;
    e.d_msg = msg;
    e.d_source = s;
    e.d_file = file;
    e.d_isErr = true;
    if( d_record )
        d_entries.append(e);
    if( d_reportToConsole )
        log(e,true);
    if( !d_reportToConsole )
    {

        d_numOfErrs++;
        if( s == Syntax )
            d_numOfSyntaxErrs++;
    }
    if( !d_threadExclusive ) d_lock.unlock();
}

void Errors::warning(Errors::Source s, const SynTree* st, const QString& msg)
{
    Q_ASSERT( st != 0 );
    warning( s, st->d_tok.d_sourcePath, st->d_tok.d_lineNr, st->d_tok.d_colNr, msg );
}

void Errors::warning(Errors::Source s, const QString& file, int line, int col, const QString& msg)
{
    if( !d_threadExclusive ) d_lock.lockForWrite();
    if( d_showWarnings )
    {
        Entry e;
        e.d_col = col;
        e.d_line = line;
        e.d_msg = msg;
        e.d_source = s;
        e.d_file = file;
        e.d_isErr = false;
        if( d_record )
            d_entries.append(e);
        if( d_reportToConsole )
            log(e,false);
    }
    d_numOfWrns++;
    if( !d_threadExclusive ) d_lock.unlock();
}

bool Errors::showWarnings() const
{
    if( !d_threadExclusive ) d_lock.lockForRead();
    const bool res = d_showWarnings;
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

void Errors::setShowWarnings(bool on)
{
    if( !d_threadExclusive ) d_lock.lockForWrite();
    d_showWarnings = on;
    if( !d_threadExclusive ) d_lock.unlock();
}

bool Errors::reportToConsole() const
{
    if( !d_threadExclusive ) d_lock.lockForRead();
    const bool res = d_reportToConsole;
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

void Errors::setReportToConsole(bool on)
{
    if( !d_threadExclusive ) d_lock.lockForWrite();
    d_reportToConsole = on;
    if( !d_threadExclusive ) d_lock.unlock();
}

bool Errors::record() const
{
    if( !d_threadExclusive ) d_lock.lockForRead();
    const bool res = d_record;
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

void Errors::setRecord(bool on)
{
    if( !d_threadExclusive ) d_lock.lockForWrite();
    d_record = on;
    if( !d_threadExclusive ) d_lock.unlock();
}

quint32 Errors::getErrCount() const
{
    if( !d_threadExclusive ) d_lock.lockForRead();
    const quint32 res = d_numOfErrs;
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

quint32 Errors::getWrnCount() const
{
    if( !d_threadExclusive ) d_lock.lockForRead();
    const quint32 res = d_numOfWrns;
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

Errors::EntryList Errors::getAll() const
{
    EntryList res;
    if( !d_threadExclusive ) d_lock.lockForRead();
    res = d_entries;
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

Errors::EntryList Errors::getErrors() const
{
    EntryList res;
    if( !d_threadExclusive ) d_lock.lockForRead();
    foreach( const Entry& e, d_entries )
    {
        if( e.d_isErr )
            res.append(e);
    }
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

Errors::EntryList Errors::getWarnings() const
{
    EntryList res;
    if( !d_threadExclusive ) d_lock.lockForRead();
    foreach( const Entry& e, d_entries )
    {
        if( !e.d_isErr )
            res.append(e);
    }
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

Errors::EntryList Errors::getErrors(const QString& file) const
{
    EntryList res;
    if( !d_threadExclusive ) d_lock.lockForRead();
    foreach( const Entry& e, d_entries )
    {
        if( e.d_isErr && e.d_file == file )
            res.append(e);
    }
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

Errors::EntryList Errors::getWarnings(const QString& file) const
{
    EntryList res;
    if( !d_threadExclusive ) d_lock.lockForRead();
    foreach( const Entry& e, d_entries )
    {
        if( !e.d_isErr && e.d_file == file )
            res.append(e);
    }
    if( !d_threadExclusive ) d_lock.unlock();
    return res;
}

void Errors::clear()
{
    if( !d_threadExclusive ) d_lock.lockForWrite();
    d_numOfErrs = 0;
    d_numOfWrns = 0;
    d_numOfSyntaxErrs = 0;
    d_entries.clear();
    if( !d_threadExclusive ) d_lock.unlock();
}

const char* Errors::sourceName(int s)
{
    switch(s)
    {
    case Lexer:
        return "Lexer";
    case Syntax:
        return "Syntax";
    case Semantics:
        return "Semantics";
    default:
        return "";
    }
}

void Errors::log(const Errors::Entry& e, bool isErr)
{
    if( isErr )
        qCritical() << d_root.relativeFilePath(e.d_file) << ":" << e.d_line << ":" << e.d_col << ": error:" <<
                       e.d_msg.toUtf8().data();
    else
        qWarning() << d_root.relativeFilePath(e.d_file) << ":" << e.d_line << ":" << e.d_col << ": warning:" <<
                      e.d_msg.toUtf8().data();

}
