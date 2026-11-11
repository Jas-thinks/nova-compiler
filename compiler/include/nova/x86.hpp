// Native backend: IR -> x86-64 (System V, AT&T syntax) assembly. Every value is a raw 64-bit pattern.
#pragma once
#include <string>
#include "nova/ir.hpp"

namespace nova {
// Emits assembly for the whole program. NOVA function f becomes the symbol nova_fn_f; runtime/nova_rt.c supplies main().
std::string generateX86(const IRProgram& ir);
}
