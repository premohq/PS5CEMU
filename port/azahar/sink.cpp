// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Azahar's sound on the PS5's AudioOut (AudioCore::CreatePS5Sink, which the PS5's
// entry in Azahar's sink list names). As Cemu's PS5AudioAPI: a thread asks Azahar for 256-frame
// stereo grains at 48 kHz, and sceAudioOutOutput, blocking until the previous grain plays, paces
// it. Azahar stretches and resamples the 3DS's sound to that rate itself.

#include "audio_core/sink.h"
#include "../ps5/log.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

extern "C"
{
	int sceAudioOutInit(void);
	int sceAudioOutOpen(int userId, int type, int index, uint32_t length, uint32_t frequency, uint32_t format);
	int sceAudioOutClose(int handle);
	int sceAudioOutOutput(int handle, const void* samples);
	int sceAudioOutSetVolume(int handle, int flags, const int* volumes);
}

namespace AudioCore
{
	namespace
	{
		constexpr int kUserSystem = 0xff; // the port belongs to the system, not to one user
		constexpr int kPortMain = 0;
		constexpr uint32_t kGrain = 256; // frames per sceAudioOutOutput
		constexpr uint32_t kRate = 48000;
		constexpr uint32_t kFormatStereoS16 = 1;
		constexpr int kVolumeFlagsLeftRight = 3;
		constexpr int kVolume0dB = 32768;

		class PS5Sink final : public Sink
		{
		public:
			PS5Sink()
			{
				sceAudioOutInit(); // an error when the launcher or Cemu has already: the port says
				m_port = sceAudioOutOpen(kUserSystem, kPortMain, 0, kGrain, kRate, kFormatStereoS16);
				if (m_port < 0)
				{
					ps5log::Line("[azahar] AudioOut did not open ({:#x}): no sound", (uint32_t)m_port);
					return;
				}
				const std::array<int, 8> volumes{kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB};
				sceAudioOutSetVolume(m_port, kVolumeFlagsLeftRight, volumes.data());
				m_thread = std::thread(&PS5Sink::Output, this);
			}

			~PS5Sink() override
			{
				m_quit = true;
				if (m_thread.joinable())
					m_thread.join();
				if (m_port >= 0)
				{
					sceAudioOutOutput(m_port, nullptr); // wait for the last grain
					sceAudioOutClose(m_port);
				}
			}

			unsigned int GetNativeSampleRate() const override
			{
				return kRate;
			}

			void SetCallback(std::function<void(s16*, std::size_t)> callback) override
			{
				std::lock_guard lock(m_mutex);
				m_callback = std::move(callback);
			}

		private:
			void Output()
			{
				std::array<s16, kGrain * 2> grain{};
				while (!m_quit)
				{
					{
						std::lock_guard lock(m_mutex);
						if (m_callback)
							m_callback(grain.data(), kGrain);
						else
							grain.fill(0);
					}
					if (sceAudioOutOutput(m_port, grain.data()) < 0)
					{
						ps5log::Line("[azahar] AudioOut output failed: the sound stops");
						return;
					}
				}
			}

			int m_port = -1;
			std::thread m_thread;
			std::atomic_bool m_quit = false;
			std::mutex m_mutex;
			std::function<void(s16*, std::size_t)> m_callback;
		};
	}

	std::unique_ptr<Sink> CreatePS5Sink(std::string_view)
	{
		return std::make_unique<PS5Sink>();
	}
}
