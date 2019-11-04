#/*
#* Copyright 2019 Rochus Keller <mailto:me@rochus-keller.ch>
#*
#* This file is part of the LuaJIT BC Viewer application.
#*
#* The following is the license that applies to this copy of the
#* application. For a license to use the application under conditions
#* other than those described here, please email to me@rochus-keller.ch.
#*
#* GNU General Public License Usage
#* This file may be used under the terms of the GNU General Public
#* License (GPL) versions 2.0 or 3.0 as published by the Free Software
#* Foundation and appearing in the file LICENSE.GPL included in
#* the packaging of this file. Please review the following information
#* to ensure GNU General Public Licensing requirements will be met:
#* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
#* http://www.gnu.org/copyleft/gpl.html.
#*/

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

TARGET = LjBcViewer
TEMPLATE = app

INCLUDEPATH += .. ./luajit-2.0

SOURCES += LjBcViewerMain.cpp \
    ../GuiTools/CodeEditor.cpp \
    LuaHighlighter.cpp \
    LuaJitBytecode.cpp \
    Engine2.cpp \
    Terminal2.cpp \
    ExpressionParser.cpp \
    BcViewer.cpp \
    LuaJitEngine.cpp \
    LuaJitComposer.cpp \
    LjDisasm.cpp

HEADERS  += LjBcViewerMain.h \
    ../GuiTools/CodeEditor.h \
    LuaHighlighter.h \
    LuaJitBytecode.h \
    Engine2.h \
    Terminal2.h \
    ExpressionParser.h \
    BcViewer.h \
    LuaJitEngine.h \
    LuaJitComposer.h \
    LjDisasm.h \
    StreamSpy.h

include( ../LuaJit/LuaJit.pri ){
    LIBS += -ldl
} else {
    LIBS += -lluajit
}

include( ../GuiTools/Menu.pri )

CONFIG(debug, debug|release) {
        DEFINES += _DEBUG
}

!win32 {
    QMAKE_CXXFLAGS += -Wno-reorder -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
}
