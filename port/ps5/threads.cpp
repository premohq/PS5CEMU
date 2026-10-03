// SPDX-License-Identifier: GPL-3.0-or-later
#include "threads.h"
#include "log.h"

#include <sys/types.h>
#include <sys/cpuset.h>
#include <pthread.h>
#include <pthread_np.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

extern "C"
{
	int sceKernelGetCurrentCpu();
	// Sony's thread affinity, a mask of the 64 CPUs a thread may run on: what titles use. FreeBSD's
	// pthread_getaffinity_np, with userland's 256-CPU set, is refused to a title.
	void* scePthreadSelf();
	int scePthreadGetaffinity(void* thread, uint64_t* mask);
	int scePthreadSetaffinity(void* thread, uint64_t mask);
}

namespace ps5threads
{
	namespace
	{
		constexpr int kMaxCpus = 64;
		uint64_t s_title = 0;	  // the CPUs the title may run on
		std::vector<int> s_cores; // the cores it has both logical CPUs of, by their first CPU
		bool s_ready = false;
		std::atomic<bool> s_pinning{false};
		std::mutex s_mutex;

		// The calling thread's CPUs: Sony's call, or FreeBSD's when that is refused. 0 when neither
		// tells, with why in error.
		uint64_t ReadAffinity(std::string& error)
		{
			uint64_t mask = 0;
			const int sony = scePthreadGetaffinity(scePthreadSelf(), &mask);
			if (sony == 0 && mask)
				return mask;
			cpuset_t set;
			CPU_ZERO(&set);
			const int bsd = pthread_getaffinity_np(pthread_self(), sizeof(set), &set);
			if (bsd == 0)
			{
				for (int cpu = 0; cpu < kMaxCpus; cpu++)
					if (CPU_ISSET(cpu, &set))
						mask |= 1ull << cpu;
				if (mask)
					return mask;
			}
			error = fmt::format("scePthreadGetaffinity {:#x}, pthread_getaffinity_np {}", (uint32_t)sony, bsd);
			return 0;
		}

		// 0, or the reasons both calls gave
		std::string WriteAffinity(uint64_t mask)
		{
			const int sony = scePthreadSetaffinity(scePthreadSelf(), mask);
			if (sony == 0)
				return {};
			cpuset_t set;
			CPU_ZERO(&set);
			for (int cpu = 0; cpu < kMaxCpus; cpu++)
				if (mask & (1ull << cpu))
					CPU_SET(cpu, &set);
			const int bsd = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
			if (bsd == 0)
				return {};
			return fmt::format("scePthreadSetaffinity {:#x}, pthread_setaffinity_np {}", (uint32_t)sony, bsd);
		}

		std::string List(uint64_t mask)
		{
			std::string cpus;
			for (int cpu = 0; cpu < kMaxCpus; cpu++)
				if (mask & (1ull << cpu))
					cpus += (cpus.empty() ? "" : ",") + std::to_string(cpu);
			return cpus;
		}

		// A thread's CPUs: the emulated Wii U CPU cores (Cemu's scheduler threads, OSSched[core=N]) get
		// a core each, so they keep their caches and do not move; every other thread gets all of the
		// title's CPUs (-1). That includes the GPU thread and the shader and pipeline compilers, which
		// load a game's caches on many threads at once, and undoes the CPUs a thread inherits from the
		// one that started it.
		int SlotOf(const char* name)
		{
			unsigned core = 0;
			if (std::sscanf(name, "OSSched[core=%u]", &core) == 1)
				return (int)core;
			return -1;
		}

		uint64_t CoreMask(int core)
		{
			return ((1ull << core) | (1ull << (core + 1))) & s_title;
		}
	}

	void Initialize()
	{
		std::string error;
		s_title = ReadAffinity(error);
		if (!s_title)
		{
			ps5log::Line("[threads] the title's CPUs could not be read ({}): threads stay where the system puts them", error);
			return;
		}
		for (int cpu = 0; cpu + 1 < kMaxCpus; cpu += 2)
			if (s_title & (3ull << cpu))
				s_cores.push_back(cpu);
		// the first core is left to the system and the rest of the app, when there are enough
		if (s_cores.size() >= 6)
			s_cores.erase(s_cores.begin());
		s_ready = !s_cores.empty();
		ps5log::Line("[threads] {}; the emulated CPU cores get console cores from CPU {} on", Describe(), s_ready ? s_cores.front() : -1);
	}

	std::string Describe()
	{
		int count = 0;
		for (int cpu = 0; cpu < kMaxCpus; cpu++)
			count += (s_title >> cpu) & 1;
		return fmt::format("{} CPUs ({})", count, List(s_title));
	}

	void SetPinning(bool enabled)
	{
		s_pinning = enabled;
		ps5log::Line("[threads] pinning the emulated CPU cores: {}", enabled ? "on (pinCpuThreads)" : "off, the system places every thread");
	}

	void Place(const char* name)
	{
		if (!s_pinning || !s_ready || !name)
			return;
		const int slot = SlotOf(name);
		const uint64_t mask = slot >= 0 ? CoreMask(s_cores[(size_t)slot % s_cores.size()]) : s_title;
		const int was = sceKernelGetCurrentCpu();
		const std::string error = WriteAffinity(mask);
		std::lock_guard lock(s_mutex);
		if (!error.empty())
			ps5log::Line("[threads] {} could not be put on CPUs {} ({}); it stays on {}", name, List(mask), error, was);
		else if (slot >= 0)
			ps5log::Line("[threads] {} on CPUs {} (it was on {})", name, List(mask), was);
	}
}

// Cemu's SetThreadName (util/helpers, patches/cemu).
void PS5Cemu_ThreadNamed(const char* name)
{
	ps5threads::Place(name);
}
