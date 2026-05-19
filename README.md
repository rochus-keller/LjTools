## About this Project

This is a parser, browser, assembler and test VM for LuaJIT 2.0 and 2.1 bytecode written in C++ and Qt. See http://luajit.org/ for more information about LuaJIT. There is also a validating Lua parser and IDE (see below).

The goal of this project is to better understand how LuaJIT works, and to support the development of alternative front ends which generate LuaJIT bytecode (as it is e.g. done in https://github.com/rochus-keller/Luon or https://github.com/rochus-keller/Oberon).

The viewer can be used to edit and compile Lua source code and display it side by side with its corresponding LuaJIT bytecode. The test VM implements a subset of LuaJIT bytecodes to study and validate their functions (see the examples subfolder).

Here is a Lua BcViewer screenshot:
![LjBcViewer Screenshot](http://software.rochus-keller.ch/LjBcViewer_screenshot_1.png)


The assembler can be used to directly program and test with LuaJIT bytecode. The syntax is defined in LjAsm.ebnf; here is a PDF: http://software.rochus-keller.ch/LjAsm_Syntax.pdf. It slightly abstracts from original LuaJIT bytecode and supports automatic register allocation. Documentation of the syntax is TBD; varargs and for loops are not yet supported.

The editor supports semantic highlighting and navigation (CTRL+Click on ident), and shows a list of cross-references when an ident is selected.

Here is an Asm Editor screenshot:
![LjAsmEditor Screenshot](http://software.rochus-keller.ch/LjAsmEditor_screenshot_1.png)

### Update on May 19, 2026

Finally we have LuaJIT 2.1 support! I’ve been considering this for several years, mostly because LuaJIT 2.0 no longer runs (stably) on current versions of macOS, but I’ve struggled with it because, on the one hand, the 2.1 bytecode is no longer documented and I had to figure everything out through reverse engineering and third-party sources, and, on the other hand, the new bytecode is even platform-dependent. 

Adding LuaJIT 2.1 support to LjTools was thus a non-trivial challenge. Opcode numbers have changed, and on 64 bit systems, two stack slots per call frame instead of one (to hold both the function pointer and the 64-bit frame link) are used (this affects the framesize field in every function prototype and the slot arithmetic for CALL/RET instructions, so the same bytecode binary will not run on both 32-bit and 64-bit targets). At least the bytecode format and instruction encoding are otherwise identical.

Rather than maintaining two separate code generators, LjTools uses LuaJIT 2.0 bytecode as a common intermediate representation. JitComposer always works with the 2.0 opcode numbering internally; this keeps the frontend completely version-agnostic. When serializing to a 2.1 blob, each opcode is remapped through a static translation table. The result is that the same JitComposer source compiles against both LuaJIT 2.0 and 2.1, producing correct bytecode for whichever version it is linked with. The only platform-dependent aspect of the output is the getCallFrameSize(); if you stick with 2.0 or stay on 32 bit systems, you can ignore the latter. 

The repository now contains two sets of JitComposer and JitBytecode: the unmodified original one for backward compatibility, and the new ones suffixed with '2'; for new projects, please use the latter ones. In any case you have to add LuaJitHelper.h/cpp to your project if you use the one or the other. Note that some low-level bytecode debugging features (see below) are only available when using my patched LuaJIT 2.0 version so far.

NOTE that there seems to be an issue in the LuaJIT 2.1 engine which causes some of the example Luon applications to crash. To avoid the crash, please apply [the patch
described in this issue](https://github.com/LuaJIT/LuaJIT/issues/1462) to LuaJIT 2.1 (LuaJIT 2.0 is not affected).

### Lua parser and IDE features

- Project file format: combine Lua modules to a single project
- Implements Lua 5.1; successfully parses all puc-tests
- Validates syntax and some semantics, reports errors and warnings (e.g. implicit global declaration gives a warning)
- Automatic re-parse when edited, navigable issue list, marked issues in code
- Syntax highlighting
- Code navigation; jump to the declaration of an ident (only for locals)
- Mark all idents refering to the same declaration
- Cross-referencing: list all uses of a declaration for easy navigation
- Browsing history, forward and backward navigation- Project file format: combine modules to a single project
- Built-in LuaJIT engine
- Integrated source level debugger with breakpoints, stack trace and locals view
- A stack trace is also shown if TRAP evaluating to true is executed


![Lua IDE Screenshot](http://software.rochus-keller.ch/screenshot_luaide_0.1.png)

### LuaJIT bytecode debugger

The bytecode debugger supports single step debugging of LuaJIT bytecode. It can do step-in, step-over and step-out, as well as stop at configurable breakpoints. All features are accessible by GUI. The debugger is embeddable to other applications, currently by SomLjVirtualMachine (see https://github.com/rochus-keller/som/). The debugger depends on commit ccc257a of https://github.com/rochus-keller/LuaJIT/tree/LjTools for the required bytecode program counter resolution. With each break or step the local variables including unnamed temporary registers are displayed as well as the stack trace. You can double-click on the stack levels to change to the corresponding local variable set.

![LuaJIT Bytecode Debugger Screenshot](http://software.rochus-keller.ch/screenshot_luajit_bytecode_debugger_v0.1.png)


### Binary Versions (outdated)

Here is a precompiled version of LjAsmEditor for Linux i386: http://software.rochus-keller.ch/LjAsmEditor_linux32.tar.gz

Here is a precompiled version of LjBcViewer for Linux i386: http://software.rochus-keller.ch/LjBcViewer_linux32.tar.gz

Both versions require a working Qt 5.x base package and printsupport compatible with i386; LuaJIT is statically linked with the binaries.

### Build Steps

Follow these steps if you want to build LjBcViewer yourself:

1. Make sure a Qt 5.x (libraries and headers) version compatible with your C++ compiler is installed on your system.
1. A compiled version of LuaJIT 2.0 or 2.1 is also required; your distro likely includes a dev package. Alternatively download the source from http://luajit.org/download/LuaJIT-2.0.5.tar.gz and run the Makefile; make sure the resulting libluajit.so/lib is accessible to the linker.
1. Create a directory; let's call it BUILD_DIR
1. Download the source code from https://github.com/rochus-keller/LjTools/archive/master.zip to the BUILD_DIR; rename the subdirectory to "LjTools".
1. Download the GuiTools source code from https://github.com/rochus-keller/GuiTools/archive/master.zip and unpack it to the BUILD_DIR; rename it to "GuiTools". 
1. Goto the BUILD_DIR/LjTools subdirectory and execute `QTDIR/bin/qmake LjBcViewer.pro` (see the Qt documentation concerning QTDIR).
1. Run make; after a couple of seconds you will find the executable in the build directory.

Alternatively you can open LjBcViewer.pro using QtCreator and build it there.

LjAsmEditor.pro is compiled in the same way. The application makes use of a parser generated by Coco/R based on input from EbnfStudio (see https://github.com/rochus-keller/EbnfStudio). There is no other dependency than the Qt Basic library. The repository already contains the generated files. In order to regenerate LjasParser.cpp/h you have to use this version of Coco/R: https://github.com/rochus-keller/Coco.




