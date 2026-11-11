// x86-64 backend. Strategy: one 8-byte stack slot per IR local/temp, all values held as raw 64-bit patterns
// (int, bool as 0/1, float as IEEE-754 bits). Internal calling convention: args pushed right-to-left, result in %rax.
// Semantics mirror nova/value.hpp: wrapping integer arithmetic, trapping /0 and %0, INT64_MIN / -1 == wrap, NaN-safe compares.
#include "nova/x86.hpp"
#include <cstring>
#include <map>
#include <sstream>

namespace nova {
namespace {

struct FnEmitter {
    const IRFunction& f;
    size_t index;
    std::ostringstream o;
    std::map<std::string, int> slot;
    int uid = 0;
    std::vector<std::string>& pool;   // program-wide string-literal pool (emitted into .rodata)

    FnEmitter(const IRFunction& fn, size_t idx, std::vector<std::string>& p) : f(fn), index(idx), pool(p) {
        for (auto& l : f.locals) slot.emplace(l.name, static_cast<int>(slot.size()));
        for (auto& p : f.params) slot.emplace(p.first, static_cast<int>(slot.size()));
    }

    std::string mem(const std::string& name) const { return std::to_string(-8 * (slot.at(name) + 1)) + "(%rbp)"; }
    std::string label(const std::string& l) const {
        std::string s = ".L" + std::to_string(index) + "_";
        for (char c : l) s += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        return s;
    }
    static uint64_t bitsOf(const Value& v) {
        if (v.type == Type::Float) { uint64_t b; std::memcpy(&b, &v.f, 8); return b; }
        if (v.type == Type::Bool) return v.b ? 1 : 0;
        return static_cast<uint64_t>(v.i);
    }
    void ins(const std::string& s) { o << "    " << s << "\n"; }
    // load operand's raw bits into a 64-bit GPR
    void ld(const Operand& x, const char* reg) {
        if (x.isConst() && x.type == Type::Str) {
            size_t k = 0;
            while (k < pool.size() && pool[k] != x.name) ++k;
            if (k == pool.size()) pool.push_back(x.name);
            ins("leaq .Lstr" + std::to_string(k) + "(%rip), " + reg);
        } else if (x.isConst()) ins(std::string("movabsq $") + std::to_string(static_cast<int64_t>(bitsOf(x.val))) + ", " + reg);
        else ins("movq " + mem(x.name) + ", " + reg);
    }
    void ldf(const Operand& x, const char* xmm, const char* tmp) { ld(x, tmp); ins(std::string("movq ") + tmp + ", " + xmm); }
    void st(const Operand& dst, const char* reg) { if (dst.isName()) ins(std::string("movq ") + reg + ", " + mem(dst.name)); }

    void setcc(const char* cc, const Operand& dst) { ins(std::string("set") + cc + " %al"); ins("movzbq %al, %rax"); st(dst, "%rax"); }

    void bin(const IRInstr& i) {
        const Operand& a = i.a; const Operand& b = i.b;
        if (a.type == Type::Str) {
            ld(a, "%rdi"); ld(b, "%rsi");
            ins(i.bop == BinOp::Add ? "call nova_rt_str_cat" : "call nova_rt_str_eq");
            if (i.bop == BinOp::Ne) ins("xorq $1, %rax");
            st(i.dst, "%rax");
            return;
        }
        if (a.type == Type::Float) {
            ldf(a, "%xmm0", "%rax"); ldf(b, "%xmm1", "%rcx");
            switch (i.bop) {
                case BinOp::Add: ins("addsd %xmm1, %xmm0"); break;
                case BinOp::Sub: ins("subsd %xmm1, %xmm0"); break;
                case BinOp::Mul: ins("mulsd %xmm1, %xmm0"); break;
                case BinOp::Div: ins("divsd %xmm1, %xmm0"); break;
                case BinOp::Gt: ins("ucomisd %xmm1, %xmm0"); setcc("a", i.dst); return;
                case BinOp::Ge: ins("ucomisd %xmm1, %xmm0"); setcc("ae", i.dst); return;
                case BinOp::Lt: ins("ucomisd %xmm0, %xmm1"); setcc("a", i.dst); return;
                case BinOp::Le: ins("ucomisd %xmm0, %xmm1"); setcc("ae", i.dst); return;
                case BinOp::Eq: ins("ucomisd %xmm1, %xmm0"); ins("sete %al"); ins("setnp %cl"); ins("andb %cl, %al"); ins("movzbq %al, %rax"); st(i.dst, "%rax"); return;
                case BinOp::Ne: ins("ucomisd %xmm1, %xmm0"); ins("setne %al"); ins("setp %cl"); ins("orb %cl, %al"); ins("movzbq %al, %rax"); st(i.dst, "%rax"); return;
                default: break;
            }
            ins("movq %xmm0, %rax"); st(i.dst, "%rax");
            return;
        }
        ld(a, "%rax"); ld(b, "%rcx");
        switch (i.bop) {
            case BinOp::Add: ins("addq %rcx, %rax"); break;
            case BinOp::Sub: ins("subq %rcx, %rax"); break;
            case BinOp::Mul: ins("imulq %rcx, %rax"); break;
            case BinOp::Div: case BinOp::Mod: {
                bool div = i.bop == BinOp::Div;
                int n = uid++;
                std::string ok = ".Lok" + std::to_string(index) + "_" + std::to_string(n), neg1 = ".Lm1_" + std::to_string(index) + "_" + std::to_string(n), done = ".Ldn" + std::to_string(index) + "_" + std::to_string(n);
                ins("testq %rcx, %rcx"); ins("jne " + ok);
                ins(div ? "call nova_rt_div_zero" : "call nova_rt_mod_zero");
                o << ok << ":\n";
                ins("cmpq $-1, %rcx"); ins("je " + neg1);
                ins("cqto"); ins("idivq %rcx"); if (!div) ins("movq %rdx, %rax");
                ins("jmp " + done);
                o << neg1 << ":\n";            // x / -1 == wrap(-x), x % -1 == 0 (avoids the INT64_MIN idiv trap)
                if (div) ins("negq %rax"); else ins("xorl %eax, %eax");
                o << done << ":\n";
                break;
            }
            case BinOp::Lt: ins("cmpq %rcx, %rax"); setcc("l", i.dst); return;
            case BinOp::Le: ins("cmpq %rcx, %rax"); setcc("le", i.dst); return;
            case BinOp::Gt: ins("cmpq %rcx, %rax"); setcc("g", i.dst); return;
            case BinOp::Ge: ins("cmpq %rcx, %rax"); setcc("ge", i.dst); return;
            case BinOp::Eq: ins("cmpq %rcx, %rax"); setcc("e", i.dst); return;
            case BinOp::Ne: ins("cmpq %rcx, %rax"); setcc("ne", i.dst); return;
        }
        st(i.dst, "%rax");
    }

    void bounds() {   // %rax = object, %rcx = index; traps unless 0 <= index < len (unsigned compare covers negatives)
        std::string ok = ".Lbd" + std::to_string(index) + "_" + std::to_string(uid++);
        ins("cmpq (%rax), %rcx"); ins("jb " + ok); ins("call nova_rt_oob"); o << ok << ":\n";
    }
    void intrinsic(const IRInstr& i) {
        if (i.callee == "__newarr") { ld(i.args[0], "%rdi"); ins("call nova_rt_newarr"); st(i.dst, "%rax"); }
        else if (i.callee == "__aload") { ld(i.args[0], "%rax"); ld(i.args[1], "%rcx"); bounds(); ins("movq 8(%rax,%rcx,8), %rax"); st(i.dst, "%rax"); }
        else if (i.callee == "__astore") { ld(i.args[0], "%rax"); ld(i.args[1], "%rcx"); ld(i.args[2], "%rdx"); bounds(); ins("movq %rdx, 8(%rax,%rcx,8)"); }
        else { ld(i.args[0], "%rax"); ins("movq (%rax), %rax"); st(i.dst, "%rax"); }   // __len
    }

    void emit() {
        std::string sym = "nova_fn_" + f.name;
        o << "    .globl " << sym << "\n    .type " << sym << ", @function\n" << sym << ":\n";
        ins("pushq %rbp"); ins("movq %rsp, %rbp");
        int frame = static_cast<int>(slot.size()) * 8; frame = (frame + 15) & ~15;
        if (frame) ins("subq $" + std::to_string(frame) + ", %rsp");
        for (auto& kv : slot) ins("movq $0, " + mem(kv.first));
        for (size_t k = 0; k < f.params.size(); ++k) { ins("movq " + std::to_string(16 + 8 * k) + "(%rbp), %rax"); ins("movq %rax, " + mem(f.params[k].first)); }
        for (auto& i : f.code) {
            switch (i.kind) {
                case IRInstr::Copy: ld(i.a, "%rax"); st(i.dst, "%rax"); break;
                case IRInstr::Bin: bin(i); break;
                case IRInstr::Un:
                    ld(i.a, "%rax");
                    switch (i.uop) {
                        case UnOp::Neg:
                            if (i.a.type == Type::Float) ins("btcq $63, %rax"); else ins("negq %rax");
                            break;
                        case UnOp::Not: ins("xorq $1, %rax"); break;
                        case UnOp::ToFloat: ins("cvtsi2sdq %rax, %xmm0"); ins("movq %xmm0, %rax"); break;
                        case UnOp::ToInt: ins("movq %rax, %xmm0"); ins("call nova_rt_f2i"); break;   // saturating, NaN -> 0
                    }
                    st(i.dst, "%rax"); break;
                case IRInstr::Label: o << label(i.target) << ":\n"; break;
                case IRInstr::Jump: ins("jmp " + label(i.target)); break;
                case IRInstr::JumpIfFalse: ld(i.a, "%rax"); ins("testq %rax, %rax"); ins("je " + label(i.target)); break;
                case IRInstr::JumpIfTrue: ld(i.a, "%rax"); ins("testq %rax, %rax"); ins("jne " + label(i.target)); break;
                case IRInstr::Call:
                {
                    if (isIntrinsic(i.callee)) { intrinsic(i); break; }
                    size_t pad = (i.args.size() % 2) * 8;          // keep %rsp 16-byte aligned at the call (SysV requirement for libc calls in callees)
                    if (pad) ins("subq $8, %rsp");
                    for (size_t k = i.args.size(); k-- > 0;) { ld(i.args[k], "%rax"); ins("pushq %rax"); }
                    ins("call nova_fn_" + i.callee);
                    if (!i.args.empty() || pad) ins("addq $" + std::to_string(8 * i.args.size() + pad) + ", %rsp");
                    st(i.dst, "%rax"); break;
                }
                case IRInstr::Return:
                    if (!i.a.isNone()) ld(i.a, "%rax");
                    ins("leave"); ins("ret"); break;
                case IRInstr::Print:
                    if (i.a.type == Type::Float) { ldf(i.a, "%xmm0", "%rax"); ins("call nova_rt_print_float"); }
                    else { ld(i.a, "%rdi"); ins(i.a.type == Type::Str ? "call nova_rt_print_str" : i.a.type == Type::Bool ? "call nova_rt_print_bool" : "call nova_rt_print_int"); }
                    break;
            }
        }
        ins("xorl %eax, %eax"); ins("leave"); ins("ret");
        o << "    .size " << sym << ", .-" << sym << "\n\n";
    }
};
} // namespace

std::string generateX86(const IRProgram& ir) {
    std::ostringstream out;
    out << "    .text\n";
    std::vector<std::string> pool;
    for (size_t k = 0; k < ir.functions.size(); ++k) { FnEmitter e(ir.functions[k], k, pool); e.emit(); out << e.o.str(); }
    out << "    .section .rodata\n    .balign 8\n";                 // string objects: { int64 length; bytes }, same layout as heap strings
    for (size_t k = 0; k < pool.size(); ++k) {
        out << ".Lstr" << k << ":\n    .quad " << pool[k].size() << "\n";
        for (unsigned char c : pool[k]) out << "    .byte " << static_cast<int>(c) << "\n";
        out << "    .balign 8\n";
    }
    out << "    .section .note.GNU-stack,\"\",@progbits\n";
    return out.str();
}

} // namespace nova
