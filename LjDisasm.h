#ifndef LJDISASM_H
#define LJDISASM_H

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

#include <LjTools/LuaJitBytecode.h>

class QIODevice;
class QTextStream;

namespace Ljas
{
    class Disasm
    {
    public:
        enum OP {
            INVALID,
            ISLT, ISGE, ISLE, ISGT,
            ISEQ, ISNE,
            ISTC, ISFC, IST, ISF,
            MOV,
            NOT, UNM,
            LEN, ADD, SUB, MUL, DIV, MOD,
            POW,
            CAT,
            KSET, KNIL,
            UGET, USET,
            UCLO,
            FNEW,
            TNEW, TDUP,
            GGET, GSET,
            TGET, TSET,
            CALL, CALLT, RET,
            FORI, FORL,
            LOOP,
            JMP
        };

        static const char* s_opName[];
        static const char* s_opHelp[];

        static bool disassemble(const Lua::JitBytecode&, QIODevice*, const QString& path = QString(),
                                bool stripped = false, bool alloc = false );
        static bool adaptToLjasm(Lua::JitBytecode::Instruction& bc, OP& op, QByteArray& warning);
        static bool adaptToLjasm(Lua::JitBytecode::Instruction& bc, QByteArray& mnemonic, QByteArray& warning);
        static QByteArray renderArg(const Lua::JitBytecode::Function* f, int type, int value, int pc,
                                    bool stripped = false, bool alt = false);
    protected:
        static bool writeFunc( QTextStream& out, const Lua::JitBytecode::Function*, bool stripped, bool alloc, int indent = 0 );
    private:
        Disasm();

    };
}

#endif // LJDISASM_H
