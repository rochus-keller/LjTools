/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Oberon parser/compiler library.
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

#include "LuaProject.h"
#include "LjasErrors.h"
#include "LjasFileCache.h"
#include "LuaModule.h"
#include <QBuffer>
#include <QDir>
#include <QtDebug>
#include <QSettings>
#include <QCoreApplication>
using namespace Lua;

Project::Project(QObject *parent) : QObject(parent),d_dirty(false)
{
    d_err = new Ljas::Errors(this);
    d_err->setRecord(true);
    d_err->setShowWarnings(true);
    d_err->setReportToConsole(false);
    d_fcache = new Ljas::FileCache(this);
    d_suffixes << ".lua";
    d_global = new Module::Global();
}

void Project::clear()
{
    d_err->clear();
    d_filePath.clear();
    FileHash::iterator i;
    for( i = d_files.begin(); i != d_files.end(); ++i )
        delete i.value();
    d_files.clear();
    d_fileOrder.clear();
}

void Project::createNew()
{
    clear();
    d_filePath.clear();
    d_dirty = false;
    emit sigModified(d_dirty);
    emit sigRenamed();
}

bool Project::initializeFromDir(const QDir& dir, bool recursive)
{
    clear();
    d_dirty = false;

    QStringList files = findFiles(dir, recursive);
    foreach( const QString& f, files )
    {
        Module* m = new Module(this);
        m->setCache(d_fcache);
        m->setErrors(d_err);
        d_files.insert(f,m);
        d_fileOrder.append(f);
    }
    emit sigRenamed();
    return true;
}

void Project::setSuffixes(const QStringList& s)
{
    d_suffixes = s;
    touch();
}

void Project::setMain(const Project::ModProc& mp)
{
    d_main = mp;
    touch();
}

QString Project::formatMain() const
{
    if( !d_main.first.isEmpty() && !d_main.second.isEmpty() )
        return d_main.first + "." + d_main.second;
    else if( !d_main.second.isEmpty() )
        return d_main.second;
    else
        return QString();
}

bool Project::addFile(const QString& path)
{
    if( d_files.contains(path) )
        return false;
    Module* m = new Module(this);
    m->setCache(d_fcache);
    m->setErrors(d_err);
    d_files.insert(path,m);
    d_fileOrder.append(path);
    touch();
    return true;
}

bool Project::removeFile(const QString& path)
{
    FileHash::iterator i = d_files.find(path);
    if( i == d_files.end() )
        return false;
    delete i.value();
    d_files.erase(i);
    d_fileOrder.removeAll(path);
    touch();
    return true;
}

static inline bool isHit( Module::Thing* res, quint32 line, quint16 col )
{
    return res->d_tok.d_lineNr == line && res->d_tok.d_colNr <= col &&
                col <= ( res->d_tok.d_colNr + res->d_tok.d_len );
}

Module::Thing* Project::findSymbolBySourcePos(const QString& file, quint32 line, quint16 col) const
{
    Module* m = d_files.value(file);
    if( m == 0 )
        return 0;
    Module::Thing* res = findSymbolBySourcePosImp( m->getTopChunk(), line, col );
    if( res )
        return res;
    for( int i = 0; i < m->getNonLocals().size(); i++ )
    {
        Module::Thing* res = findSymbolBySourcePosImp( m->getNonLocals()[i].data(), line, col );
        if( res )
            return res;
    }
    return 0;
}

QString Project::getWorkingDir(bool resolved) const
{
    if( d_workingDir.isEmpty() )
        return QFileInfo(d_filePath).dir().path();
    else if( !resolved )
        return d_workingDir;
    // else
    QString wd = d_workingDir;
    wd.replace("%PRODIR%", QFileInfo(d_filePath).dir().path() );
    wd.replace("%APPDIR%", QCoreApplication::applicationDirPath() );
    return wd;
}

void Project::setWorkingDir(const QString& wd)
{
    d_workingDir = wd;
    touch();
}

QStringList Project::findFiles(const QDir& dir, bool recursive)
{
    QStringList res;
    QStringList files;

    if( recursive )
    {
        files = dir.entryList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );

        foreach( const QString& f, files )
            res += findFiles( QDir( dir.absoluteFilePath(f) ), recursive );
    }

    QStringList suff = d_suffixes;
    for(int i = 0; i < suff.size(); i++ )
        suff[i] = "*" + suff[i];
    files = dir.entryList( suff, QDir::Files, QDir::Name );
    foreach( const QString& f, files )
    {
        res.append( dir.absoluteFilePath(f) );
    }
    return res;
}

Module::Thing*Project::findSymbolBySourcePosImp(Module::Thing* node, quint32 line, quint16 col) const
{
    if( node == 0 )
        return 0;
    if( node->d_tok.d_lineNr > line )
        return 0;
    if( isHit( node, line, col ) )
        return node;
    if( !node->isScope() )
        return 0;
    // else
    Module::Scope* scope = static_cast<Module::Scope*>(node);
    foreach( const Module::Ref<Module::Thing>& n, scope->d_locals )
    {
        // TODO: binary search falls nötig
        Module::Thing* res = findSymbolBySourcePosImp( n.data(), line, col );
        if( res )
            return res;
    }
    foreach( const Module::Ref<Module::Block>& n, scope->d_stats )
    {
        // TODO: binary search falls nötig
        Module::Thing* res = findSymbolBySourcePosImp( n.data(), line, col );
        if( res )
            return res;
    }
    foreach( const Module::Ref<Module::SymbolUse>& n, scope->d_refs )
    {
        // TODO: binary search falls nötig
        Module::Thing* res = findSymbolBySourcePosImp( n.data(), line, col );
        if( res )
            return res;
    }
    return 0;
}

void Project::touch()
{
    if( !d_dirty )
    {
        d_dirty = true;
        emit sigModified(d_dirty);
    }
}

bool Project::recompile()
{
    d_err->clear();
    d_global->clear();
    Module::initBuiltIns(d_global.data());
    foreach( const QByteArray& name, d_addBuiltIns )
        Module::addBuiltInSym( d_global.data(), name );
    FileHash::const_iterator i;
    for( i = d_files.begin(); i != d_files.end(); ++i )
    {
        i.value()->setGlobal(d_global.data());
        i.value()->parse( i.key(), false );
    }
    emit sigRecompiled();
    return true;
}

bool Project::save()
{
    if( d_filePath.isEmpty() )
        return false;

    QDir dir = QFileInfo(d_filePath).dir();

    QSettings out(d_filePath,QSettings::IniFormat);
    if( !out.isWritable() )
        return false;

    out.setValue("Suffixes", d_suffixes );
    out.setValue("MainModule", d_main.first );
    out.setValue("MainProc", d_main.second );
    out.setValue("WorkingDir", d_workingDir );

    out.beginWriteArray("Modules", d_fileOrder.size() );
    FileHash::const_iterator i;
    for( int i = 0; i < d_fileOrder.size(); i++ )
    {
        const QString absPath = d_fileOrder[i];
        const QString relPath = dir.relativeFilePath( absPath );
        out.setArrayIndex(i);
        out.setValue("AbsPath", absPath );
        out.setValue("RelPath", relPath );
    }
    out.endArray();

    d_dirty = false;
    emit sigModified(d_dirty);
    return true;
}

bool Project::loadFrom(const QString& filePath)
{
    clear();

    d_filePath = filePath;

    QDir dir = QFileInfo(d_filePath).dir();

    QSettings in(d_filePath,QSettings::IniFormat);

    d_suffixes = in.value("Suffixes").toStringList();
    d_main.first = in.value("MainModule").toByteArray();
    d_main.second = in.value("MainProc").toByteArray();
    d_workingDir = in.value("WorkingDir").toString();

    const int count = in.beginReadArray("Modules");

    for( int i = 0; i < count; i++ )
    {
        in.setArrayIndex(i);
        QString absPath = in.value("AbsPath").toString();
        const QString relPath = in.value("RelPath").toString();
        if( QFileInfo(absPath).exists() )
        {
            Module* m = new Module(this);
            m->setCache(d_fcache);
            m->setErrors(d_err);
            d_files.insert( absPath, m );
        }else
        {
            absPath = dir.absoluteFilePath(relPath);
            if( QFileInfo(absPath).exists() )
            {
                Module* m = new Module(this);
                m->setCache(d_fcache);
                m->setErrors(d_err);
                d_files.insert( absPath, m );
            }else
                qCritical() << "Could not open module" << relPath;
        }
        d_fileOrder.append(absPath);
    }

    in.endArray();

    d_dirty = false;
    emit sigModified(d_dirty);
    emit sigRenamed();
    return true;
}

bool Project::saveTo(const QString& filePath)
{
    d_filePath = filePath;
    const bool res = save();
    emit sigRenamed();
    return res;
}

