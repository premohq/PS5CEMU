// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the console functions the port calls. All are exported by libkernel, libSceVideoOut,
// libSceAudioOut, libScePad, libSceUserService and libSceSystemService; the payload SDK's stub
// libraries name them, and the title converter imports them by NID.

#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

extern "C"
{
	// memory
	int32_t sceKernelReserveVirtualRange(void** address, size_t length, int flags, size_t alignment);
	int32_t sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t length, size_t alignment,
		int memoryType, off_t* physicalOut);
	int32_t sceKernelMapDirectMemory(void** address, size_t length, int protection, int flags, off_t physical,
		size_t alignment);
	int32_t sceKernelReleaseDirectMemory(off_t start, size_t length);
	size_t sceKernelGetDirectMemorySize();
	int32_t sceKernelAvailableDirectMemorySize(off_t searchStart, off_t searchEnd, size_t alignment, off_t* startOut,
		size_t* sizeOut);
	int32_t sceKernelAvailableFlexibleMemorySize(size_t* sizeOut);
	int32_t sceKernelMunmap(void* address, size_t length);
	int32_t sceKernelMprotect(const void* address, size_t length, int protection);

	// JIT: shared memory that may be mapped executable, for a process the HEN has jailbroken
	int32_t sceKernelJitCreateSharedMemory(const char* name, size_t length, int maxProtection, int* handleOut);
	int32_t sceKernelJitMapSharedMemory(int handle, int protection, void** address);

	// files: a folder's entries, as FreeBSD's getdirentries returns them
	int32_t sceKernelGetdents(int fd, char* buffer, int length);

	// time and threads
	int32_t sceKernelUsleep(uint32_t microseconds);
	uint64_t sceKernelGetProcessTime();

	// notifications (the toast in the top right of the screen)
	// the layout the payload SDK's samples use
	struct SceNotificationRequest
	{
		char reserved[45];
		char message[3075];
	};
	int32_t sceKernelSendNotificationRequest(int32_t device, SceNotificationRequest* request, size_t size, int32_t blocking);

	// system service
	int32_t sceSystemServiceHideSplashScreen();
	int32_t sceSystemServiceLoadExec(const char* path, char* const argv[]);
}

namespace ps5
{
	constexpr size_t kPageSize = 0x4000;		// the kernel's page
	constexpr size_t kLargePageSize = 0x200000; // what the kernel maps with 2 MiB pages
	constexpr int kMapFixed = 0x10;				// FreeBSD's MAP_FIXED, as the kernel's map calls take it
	constexpr int kProtRead = 0x1, kProtWrite = 0x2, kProtExec = 0x4;
	// The direct-memory type for CPU memory that ProsperoEden's heap and guest memory use on the
	// console (write-back, CPU coherent; mprotect is allowed on it).
	constexpr int kDirectMemoryTypeCpu = 12;

	// RADV's GPU windows (PS5_Mesa winsys/ps5/radv_ps5_platform.c): 32-bit GPU buffers live in
	// [0x2'0000'0000, 0x3'0000'0000) and other GPU buffers in [0x40'0000'0000, 0x80'0000'0000).
	// CPU mappings must stay out of both.
	constexpr uintptr_t kGpuWindowLow = 0x200000000ull, kGpuWindowLowEnd = 0x300000000ull;
	constexpr uintptr_t kGpuWindowHigh = 0x4000000000ull, kGpuWindowHighEnd = 0x8000000000ull;
	// Where CPU mappings start looking: above the low GPU window, below the high one.
	constexpr uintptr_t kCpuMappingHint = 0x1000000000ull;

	inline bool OverlapsGpuWindows(uintptr_t start, size_t length)
	{
		const uintptr_t end = start + length;
		return (start < kGpuWindowLowEnd && end > kGpuWindowLow) || (start < kGpuWindowHighEnd && end > kGpuWindowHigh);
	}
}
