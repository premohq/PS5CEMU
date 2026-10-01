// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: system information for Cemu's overlay (CPU and RAM usage) on the PS5.

#include "util/SystemInfo/SystemInfo.h"
#include "../ps5/kernel.h"

uint64 QueryRamUsage()
{
	// direct memory in use: everything but the largest free block (a lower bound on the free memory)
	off_t start = 0;
	size_t largestFree = 0;
	const size_t total = sceKernelGetDirectMemorySize();
	if (sceKernelAvailableDirectMemorySize(0, (off_t)total, ps5::kPageSize, &start, &largestFree) != 0)
		return 0;
	return total - largestFree;
}

void QueryProcTime(uint64& out_now, uint64& out_user, uint64& out_kernel)
{
	// the process time in microseconds; the console does not split user and kernel time
	out_now = sceKernelGetProcessTime();
	out_user = out_now;
	out_kernel = 0;
}

void QueryCoreTimes(uint32 count, std::vector<ProcessorTime>& out)
{
	for (auto& time : out)
		time = {};
}
