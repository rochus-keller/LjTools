#ifndef LUAPROJECT_H
#define LUAPROJECT_H

/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Lua parser/compiler library.
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
#include <QStringList>
#include <QHash>
#include <LjTools/LuaModule.h>

class QDir;

namespace Ljas
{
    class Errors;
    class FileCache;
}
namespace Lua
{
    // adapted from Oberon
    class Project : public QObject
    {
        Q_OBJECT
    public:
        typedef QHash<QString,Module*> FileHash;
        typedef QPair<QByteArray,QByteArray> ModProc; // module.procedure or just procedure

        explicit Project(QObject *parent = 0);
        void clear();

        bool loadFrom( const QString& filePath );
        void createNew();
        bool recompile();
        bool save();
        bool saveTo(const QString& filePath );
        bool initializeFromDir( const QDir&, bool recursive = false );
        bool initializeFromFiles( const QStringList&, const QByteArray& run = QByteArray(), bool useRequire = true );
        void setSuffixes( const QStringList& ); // Form: ".suffix"
        const QStringList& getSuffixes() const { return d_suffixes; }
        void setMain( const ModProc& );
        const ModProc& getMain() const { return d_main; }
        QString formatMain() const;
        bool addFile( const QString& );
        bool removeFile( const QString& );
        const QString& getFilePath() const { return d_filePath; }
        const FileHash& getFiles() const { return d_files; }
        const QStringList& getFileOrder() const { return d_fileOrder; }
        bool isDirty() const { return d_dirty; }
        Module::Thing* findSymbolBySourcePos(const QString& file, quint32 line, quint16 col ) const;
        QString getWorkingDir(bool resolved = false) const;
        void setWorkingDir( const QString& );
        void addBuiltIn( const QByteArray& name ) { d_addBuiltIns.append(name); }
        bool useRequire() const { return d_useRequire; }

        Ljas::Errors* getErrs() const { return d_err; }
        Ljas::FileCache* getFc() const { return d_fcache; }
    signals:
        void sigModified(bool);
        void sigRenamed();
        void sigRecompiled();
    protected:
        QStringList findFiles(const QDir& , bool recursive = false);
        Module::Thing* findSymbolBySourcePosImp(Module::Thing*, quint32 line, quint16 col ) const;
        void touch();
    private:
        Ljas::Errors* d_err;
        Ljas::FileCache* d_fcache;
        FileHash d_files;
        QStringList d_fileOrder;
        QString d_filePath;
        QStringList d_suffixes;
        QString d_workingDir;
        ModProc d_main;
        Module::Ref<Module::Global> d_global;
        QByteArrayList d_addBuiltIns;
        bool d_dirty;
        bool d_useRequire;
    };
}

#endif // LUAPROJECT_H
