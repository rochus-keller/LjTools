This is a parser, browser and test VM for LuaJIT 2.0 bytecode written in C++ and Qt. See http://luajit.org/ for more information about LuaJIT. 

The goal of this project is to better understand how LuaJIT works. The viewer can be used to edit and compile Lua code and display it side by side with its corresponding LuaJIT bytecode. The test VM implements a subset of LuaJIT bytecodes to study and validate their functions (see the examples subfolder).


![LjBcViewer Screenshot](http://software.rochus-keller.info/LjBcViewer_screenshot_1.png)


### Build Steps

Follow these steps if you want to build LjBcViewer yourself:

1. Make sure a Qt 5.x (libraries and headers) version compatible with your C++ compiler is installed on your system.
1. A compiled version of LuaJIT 2.0 is also required; your distro likely includes a dev package. Alternatively download the source from http://luajit.org/download/LuaJIT-2.0.5.tar.gz and run the Makefile; make sure the resulting libluajit.so/lib is accessible to the linker.
1. Create a directory; let's call it BUILD_DIR
1. Download the source code from https://github.com/rochus-keller/LjTools/archive/master.zip to the BUILD_DIR; rename the subdirectory to "LjTools".
1. Download the GuiTools source code from https://github.com/rochus-keller/GuiTools/archive/master.zip and unpack it to the BUILD_DIR; rename it to "GuiTools". 
1. Goto the BUILD_DIR/LjTools subdirectory and execute `QTDIR/bin/qmake LjBcViewer.pro` (see the Qt documentation concerning QTDIR).
1. Run make; after a couple of seconds you will find the executable in the build directory.

Alternatively you can open LjBcViewer.pro using QtCreator and build it there.

The repository includes the original LuaJIT 2.0.5 headers for convenience.



