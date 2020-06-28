#/*
#* Copyright 2019 Rochus Keller <mailto:me@rochus-keller.ch>
#*
#* This file is part of the Lua parser/code model library.
#*
#* The following is the license that applies to this copy of the
#* library. For a license to use the library under conditions
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

INCLUDEPATH +=  ..

SOURCES += \
	../LjTools/LuaIde.cpp \
    ../GuiTools/CodeEditor.cpp \
    ../LjTools/LuaJitBytecode.cpp \
    ../LjTools/Engine2.cpp \
    ../LjTools/Terminal2.cpp \
    ../LjTools/ExpressionParser.cpp \
    ../LjTools/LuaJitEngine.cpp \
    ../LjTools/LuaJitComposer.cpp \
    ../LjTools/LuaHighlighter.cpp \
    ../LjTools/LjDisasm.cpp \
    ../LjTools/BcViewer2.cpp \
    ../GuiTools/DocSelector.cpp \
    ../GuiTools/DocTabWidget.cpp \
    ../LjTools/BcViewer.cpp \ 
    ../LjTools/LuaProject.cpp

HEADERS  += \
	../LjTools/LuaIde.h \
    ../GuiTools/CodeEditor.h \
    ../LjTools/LuaJitBytecode.h \
    ../LjTools/Engine2.h \
    ../LjTools/Terminal2.h \
    ../LjTools/ExpressionParser.h \
    ../LjTools/LuaJitEngine.h \
    ../LjTools/LuaJitComposer.h \
    ../LjTools/LuaHighlighter.h \
    ../LjTools/LjDisasm.h \
    ../LjTools/BcViewer2.h \
    ../GuiTools/DocSelector.h \
    ../GuiTools/DocTabWidget.h \
    ../LjTools/BcViewer.h \
    ../LjTools/LuaProject.h
