// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: Cemu's fibers on the PS5.
//
// Cemu runs every emulated PPC thread on a fiber and switches fibers on each guest thread switch.
// The Unix implementation uses ucontext, whose swapcontext saves and restores the signal mask
// with a system call on every switch. This one switches stacks directly: the callee-saved
// registers of the SysV ABI and the SSE/x87 control words go on the leaving fiber's stack, which
// is all a function call may expect to survive.

#include "util/Fiber/Fiber.h"

#include <atomic>
#include <cstdlib>

namespace
{
	thread_local Fiber* sCurrentFiber{};

	constexpr size_t kStackSize = 2 * 1024 * 1024;

	struct FiberContext
	{
		void* stackPointer; // the saved RSP of a fiber that is not running
	};
}

extern "C"
{
	// Saves the current context, stores its stack pointer in *from and resumes the one at to.
	void PS5Cemu_FiberSwitch(void** from, void* to);
	// First code a new fiber runs: R12 holds the entry point and R13 its parameter.
	void PS5Cemu_FiberStart();
}

asm(".text\n"
	".globl PS5Cemu_FiberSwitch\n"
	".hidden PS5Cemu_FiberSwitch\n"
	".type PS5Cemu_FiberSwitch,@function\n"
	".p2align 4\n"
	"PS5Cemu_FiberSwitch:\n"
	"	push %rbp\n"
	"	push %rbx\n"
	"	push %r12\n"
	"	push %r13\n"
	"	push %r14\n"
	"	push %r15\n"
	"	sub $8, %rsp\n"
	"	stmxcsr (%rsp)\n"
	"	fnstcw 4(%rsp)\n"
	"	mov %rsp, (%rdi)\n"
	"	mov %rsi, %rsp\n"
	"	ldmxcsr (%rsp)\n"
	"	fldcw 4(%rsp)\n"
	"	add $8, %rsp\n"
	"	pop %r15\n"
	"	pop %r14\n"
	"	pop %r13\n"
	"	pop %r12\n"
	"	pop %rbx\n"
	"	pop %rbp\n"
	"	ret\n"
	".size PS5Cemu_FiberSwitch, .-PS5Cemu_FiberSwitch\n"
	"\n"
	".globl PS5Cemu_FiberStart\n"
	".hidden PS5Cemu_FiberStart\n"
	".type PS5Cemu_FiberStart,@function\n"
	".p2align 4\n"
	"PS5Cemu_FiberStart:\n"
	"	mov %r13, %rdi\n"
	"	call *%r12\n"
	"	ud2\n" // a fiber's entry point never returns
	".size PS5Cemu_FiberStart, .-PS5Cemu_FiberStart\n");

Fiber::Fiber(void (*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
	auto* ctx = new FiberContext();
	m_stackPtr = aligned_alloc(64, kStackSize);
	cemu_assert(m_stackPtr);

	// The initial frame PS5Cemu_FiberSwitch pops, laid out so that FiberStart's call sees a
	// 16-byte aligned stack: the return address sits at 8 mod 16.
	uint64* top = reinterpret_cast<uint64*>(reinterpret_cast<uint8*>(m_stackPtr) + kStackSize);
	uint64* frame = top - 10;
	uint32 mxcsr;
	uint16 fpuControl;
	asm volatile("stmxcsr %0" : "=m"(mxcsr));
	asm volatile("fnstcw %0" : "=m"(fpuControl));
	frame[0] = (uint64)mxcsr | ((uint64)fpuControl << 32);
	frame[1] = 0;							   // r15
	frame[2] = 0;							   // r14
	frame[3] = (uint64)userParam;			   // r13
	frame[4] = (uint64)FiberEntryPoint;	   // r12
	frame[5] = 0;							   // rbx
	frame[6] = 0;							   // rbp
	frame[7] = (uint64)&PS5Cemu_FiberStart; // return address
	frame[8] = 0;
	frame[9] = 0;
	ctx->stackPointer = frame;
	m_implData = ctx;
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
	// the thread's own stack: its context is saved the first time it switches away
	m_implData = new FiberContext();
	m_stackPtr = nullptr;
}

Fiber::~Fiber()
{
	if (m_stackPtr)
		free(m_stackPtr);
	delete static_cast<FiberContext*>(m_implData);
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	cemu_assert_debug(sCurrentFiber == nullptr);
	sCurrentFiber = new Fiber(privateData);
	return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
	Fiber* leavingFiber = sCurrentFiber;
	sCurrentFiber = &targetFiber;
	std::atomic_thread_fence(std::memory_order_seq_cst);
	PS5Cemu_FiberSwitch(&static_cast<FiberContext*>(leavingFiber->m_implData)->stackPointer,
		static_cast<FiberContext*>(targetFiber.m_implData)->stackPointer);
	std::atomic_thread_fence(std::memory_order_seq_cst);
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}
