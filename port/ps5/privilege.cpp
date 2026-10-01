// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: getting out of the app sandbox (privilege.h).
//
// The HEN request follows PS5SX2's ProsperoHenJailbreak.cpp (GPL-3.0-or-later), itself ported from
// PS2-Library-Prototype: publish the request atomically (write a staged file, then rename), wait for
// the HEN to remove it, then give the HEN a moment to finish before trusting the credentials.

#include "privilege.h"
#include "kernel.h"
#include "log.h"

#include "elevation.hpp" // ps5-native-app-boilerplate examples/sandbox-elevation

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ps5privilege
{
	namespace
	{
		constexpr char kRequestPath[] = "/download0/etahen_jailbreak";
		constexpr char kStagedPath[] = "/download0/etahen_jailbreak.tmp";
		constexpr int kPollUs = 16667;
		constexpr int kMaxPolls = 300;				 // ~5 s for the HEN to take the request
		constexpr int kMaxPollsAfterConsume = 180;	 // ~3 s for it to finish the jailbreak

		Result s_result;

		bool PublishRequest(std::string& failure)
		{
			unlink(kRequestPath);
			unlink(kStagedPath);
			const int fd = open(kStagedPath, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
			if (fd < 0)
			{
				failure = fmt::format("cannot create the request (errno {})", errno);
				return false;
			}
			fchmod(fd, 0666);
			const std::string request = fmt::format("{{\"PID\":{}}}\n", getpid());
			const bool written = write(fd, request.data(), request.size()) == (ssize_t)request.size() && fsync(fd) == 0;
			close(fd);
			if (!written || rename(kStagedPath, kRequestPath) != 0)
			{
				unlink(kStagedPath);
				failure = fmt::format("cannot publish the request (errno {})", errno);
				return false;
			}
			return true;
		}

		bool HenJailbreak(std::string& detail)
		{
			if (geteuid() == 0)
			{
				detail = "already root";
				return true;
			}
			std::string failure;
			if (!PublishRequest(failure))
			{
				detail = failure;
				return false;
			}
			int polls = 0;
			while (access(kRequestPath, F_OK) == 0)
			{
				if (++polls >= kMaxPolls)
				{
					unlink(kRequestPath);
					detail = "no HEN took the request (is etaHEN loaded, and PPSA99360 in its app jailbreak list?)";
					return false;
				}
				sceKernelUsleep(kPollUs);
			}
			// the HEN removes the request before it is done with the process
			for (int wait = 0; wait < kMaxPollsAfterConsume && geteuid() != 0; wait++)
				sceKernelUsleep(kPollUs);
			detail = fmt::format("HEN took the request after {} ms, euid {}", polls * kPollUs / 1000, geteuid());
			return true;
		}

		bool ProbeJit()
		{
			// one page, left mapped: what the recompiler will ask for
			int handle = -1;
			if (sceKernelJitCreateSharedMemory(nullptr, ps5::kPageSize, ps5::kProtRead | ps5::kProtWrite | ps5::kProtExec, &handle) != 0)
				return false;
			void* address = nullptr;
			return sceKernelJitMapSharedMemory(handle, ps5::kProtRead | ps5::kProtWrite | ps5::kProtExec, &address) == 0 && address;
		}

		bool CanReachData()
		{
			struct stat st{};
			return stat("/data", &st) == 0 && S_ISDIR(st.st_mode) && access("/data", W_OK) == 0;
		}
	}

	Result Acquire()
	{
		Result r;
		std::string henDetail;
		r.jailbroken = HenJailbreak(henDetail);
		// euid may stay 1 even with working credentials: the JIT probe is the ground truth
		r.jit = ProbeJit();
		r.filesystem = CanReachData();
		std::string elevationDetail;
		if (!r.filesystem)
		{
			const auto status = elevation::request(elevation::Capability::filesystem);
			r.filesystem = status == elevation::Status::ok && CanReachData();
			elevationDetail = fmt::format(", elevation helper: {}", (int)status);
		}
		r.summary = fmt::format("HEN: {} ({}); JIT {}; /data {}{}", r.jailbroken ? "ok" : "no", henDetail,
			r.jit ? "available" : "unavailable (interpreter only)", r.filesystem ? "reachable" : "unreachable", elevationDetail);
		s_result = r;
		return r;
	}

	const Result& Current()
	{
		return s_result;
	}
}

bool PS5_JitAvailable()
{
	return ps5privilege::Current().jit;
}
