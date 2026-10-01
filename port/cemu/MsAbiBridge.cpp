// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: Microsoft-ABI thunks for Cemu's x64 recompiler (see MsAbiBridge.h).
//
// Generated code -> host: the generated code calls with the Microsoft convention (arguments in
// RCX, RDX; RDI, RSI and XMM6-XMM15 preserved by the callee). Each thunk saves what the
// Microsoft caller expects to survive and a SysV callee may clobber, moves the integer
// arguments to RDI and RSI, and calls the SysV function. A double argument and the return
// value are in XMM0 or RAX in both conventions. The thunk aligns the stack itself: the generated
// code's call_imm reaches its callee 8 bytes off 16-byte alignment (the enter function leaves RSP
// at 8 mod 16), which upstream's small leaf callees tolerate but a SysV callee need not.
//
// Host -> generated code: PS5_SysV_enterRecompilerCode moves its arguments to RCX and RDX and
// reserves the 32-byte home area. The generated enter function saves every general register,
// a superset of what a SysV caller needs preserved.

#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterInternal.h"
#include "MsAbiBridge.h"

uint32 PPCRecompiler_GetTBL();
uint32 PPCRecompiler_GetTBU();
void* PPCRecompiler_virtualHLE(PPCInterpreter_t* hCPU, uint32 hleFuncId);

extern "C"
{
	void* PS5_MsAbi_enterRecompilerCodeTarget = nullptr;

	// SysV entry points with unmangled names, for the thunks to call.
	double PS5_SysV_fres_espresso(double input) { return fres_espresso(input); }
	double PS5_SysV_frsqrte_espresso(double input) { return frsqrte_espresso(input); }
	uint32 PS5_SysV_PPCRecompiler_GetTBL() { return PPCRecompiler_GetTBL(); }
	uint32 PS5_SysV_PPCRecompiler_GetTBU() { return PPCRecompiler_GetTBU(); }
	void* PS5_SysV_PPCRecompiler_virtualHLE(PPCInterpreter_t* hCPU, uint32 hleFuncId) { return PPCRecompiler_virtualHLE(hCPU, hleFuncId); }
}

#define PS5_MSABI_THUNK(NAME)                                  \
	asm(".text\n"                                              \
		".globl PS5_MsAbi_" #NAME "\n"                         \
		".hidden PS5_MsAbi_" #NAME "\n"                        \
		".type PS5_MsAbi_" #NAME ",@function\n"                \
		".p2align 4\n"                                         \
		"PS5_MsAbi_" #NAME ":\n"                               \
		"	push %rbp\n"                                       \
		"	mov %rsp, %rbp\n"                                  \
		"	and $-16, %rsp\n"                                  \
		"	sub $0xb0, %rsp\n"                                 \
		"	mov %rdi, 0xa0(%rsp)\n"                            \
		"	mov %rsi, 0xa8(%rsp)\n"                            \
		"	movaps %xmm6, 0x00(%rsp)\n"                        \
		"	movaps %xmm7, 0x10(%rsp)\n"                        \
		"	movaps %xmm8, 0x20(%rsp)\n"                        \
		"	movaps %xmm9, 0x30(%rsp)\n"                        \
		"	movaps %xmm10, 0x40(%rsp)\n"                       \
		"	movaps %xmm11, 0x50(%rsp)\n"                       \
		"	movaps %xmm12, 0x60(%rsp)\n"                       \
		"	movaps %xmm13, 0x70(%rsp)\n"                       \
		"	movaps %xmm14, 0x80(%rsp)\n"                       \
		"	movaps %xmm15, 0x90(%rsp)\n"                       \
		"	mov %rcx, %rdi\n"                                  \
		"	mov %rdx, %rsi\n"                                  \
		"	call PS5_SysV_" #NAME "\n"                         \
		"	movaps 0x00(%rsp), %xmm6\n"                        \
		"	movaps 0x10(%rsp), %xmm7\n"                        \
		"	movaps 0x20(%rsp), %xmm8\n"                        \
		"	movaps 0x30(%rsp), %xmm9\n"                        \
		"	movaps 0x40(%rsp), %xmm10\n"                       \
		"	movaps 0x50(%rsp), %xmm11\n"                       \
		"	movaps 0x60(%rsp), %xmm12\n"                       \
		"	movaps 0x70(%rsp), %xmm13\n"                       \
		"	movaps 0x80(%rsp), %xmm14\n"                       \
		"	movaps 0x90(%rsp), %xmm15\n"                       \
		"	mov 0xa0(%rsp), %rdi\n"                            \
		"	mov 0xa8(%rsp), %rsi\n"                            \
		"	mov %rbp, %rsp\n"                                  \
		"	pop %rbp\n"                                        \
		"	ret\n"                                             \
		".size PS5_MsAbi_" #NAME ", .-PS5_MsAbi_" #NAME "\n")

PS5_MSABI_THUNK(fres_espresso);
PS5_MSABI_THUNK(frsqrte_espresso);
PS5_MSABI_THUNK(PPCRecompiler_GetTBL);
PS5_MSABI_THUNK(PPCRecompiler_GetTBU);
PS5_MSABI_THUNK(PPCRecompiler_virtualHLE);

asm(".text\n"
	".globl PS5_SysV_enterRecompilerCode\n"
	".hidden PS5_SysV_enterRecompilerCode\n"
	".type PS5_SysV_enterRecompilerCode,@function\n"
	".p2align 4\n"
	"PS5_SysV_enterRecompilerCode:\n"
	"	sub $0x28, %rsp\n"
	"	mov %rdi, %rcx\n"
	"	mov %rsi, %rdx\n"
	"	call *PS5_MsAbi_enterRecompilerCodeTarget(%rip)\n"
	"	add $0x28, %rsp\n"
	"	ret\n"
	".size PS5_SysV_enterRecompilerCode, .-PS5_SysV_enterRecompilerCode\n");
