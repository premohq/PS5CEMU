// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: Cemu's audio output on the PS5's AudioOut.
//
// Cemu hands over blocks of 16-bit samples (stereo, or 5.1 for the TV if configured). A worker
// thread feeds AudioOut 256-frame stereo grains at 48 kHz; sceAudioOutOutput blocks until the
// previous grain is playing, which paces the thread. Missing samples play as silence, and surround
// streams are mixed down to stereo, the format every PS5 audio output accepts.

#include "PS5AudioAPI.h"

#include <algorithm>
#include <array>

#include "util/helpers/helpers.h"

extern "C"
{
	int sceAudioOutInit(void);
	int sceAudioOutOpen(int userId, int type, int index, uint32_t length, uint32_t frequency, uint32_t format);
	int sceAudioOutClose(int handle);
	int sceAudioOutOutput(int handle, const void* samples);
	int sceAudioOutSetVolume(int handle, int flags, const int* volumes);
}

namespace
{
	constexpr int kUserSystem = 0xff;	 // the port belongs to the system, not to one user
	constexpr int kPortMain = 0;
	constexpr uint32 kGrain = 256;		 // frames per sceAudioOutOutput
	constexpr uint32 kRate = 48000;
	constexpr uint32 kFormatStereoS16 = 1;
	constexpr int kVolumeFlagsLeftRight = 3;
	constexpr int kVolume0dB = 32768;
}

bool PS5AudioAPI::InitializeStatic()
{
	// The launcher may have initialised AudioOut already, which this call then reports as an
	// error; whether a port really opens shows in the constructor.
	static const int result = sceAudioOutInit();
	(void)result;
	return true;
}

std::vector<IAudioAPI::DeviceDescriptionPtr> PS5AudioAPI::GetDevices()
{
	return {std::make_shared<PS5DeviceDescription>()};
}

PS5AudioAPI::PS5AudioAPI(uint32 samplerate, uint32 channels, uint32 samples_per_block, uint32 bits_per_sample)
	: IAudioAPI(samplerate, channels, samples_per_block, bits_per_sample)
{
	if (bits_per_sample != 16 || samplerate != kRate)
		throw std::runtime_error(fmt::format("PS5 AudioOut: unsupported stream ({} Hz, {} bit)", samplerate, bits_per_sample));
	m_port = sceAudioOutOpen(kUserSystem, kPortMain, 0, kGrain, kRate, kFormatStereoS16);
	if (m_port < 0)
		throw std::runtime_error(fmt::format("PS5 AudioOut: sceAudioOutOpen failed ({:#x})", (uint32)m_port));
	const std::array<int, 8> volumes{kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB};
	sceAudioOutSetVolume(m_port, kVolumeFlagsLeftRight, volumes.data());
	m_queueCapacity = (size_t)samples_per_block * channels * kBlockCount;
	m_queue.reserve(m_queueCapacity);
	m_thread = std::thread(&PS5AudioAPI::OutputThread, this);
}

PS5AudioAPI::~PS5AudioAPI()
{
	{
		std::lock_guard lock(m_mutex);
		m_quit = true;
	}
	m_wake.notify_all();
	if (m_thread.joinable())
		m_thread.join();
	if (m_port >= 0)
	{
		sceAudioOutOutput(m_port, nullptr); // wait for the last grain
		sceAudioOutClose(m_port);
	}
}

bool PS5AudioAPI::NeedAdditionalBlocks() const
{
	std::lock_guard lock(m_mutex);
	return m_queue.size() < (size_t)GetAudioDelay() * m_samplesPerBlock * m_channels;
}

bool PS5AudioAPI::FeedBlock(sint16* data)
{
	std::lock_guard lock(m_mutex);
	const size_t count = (size_t)m_samplesPerBlock * m_channels;
	if (m_queue.size() + count > m_queueCapacity)
		return false; // too far ahead: drop it, as the other backends do
	m_queue.insert(m_queue.end(), data, data + count);
	return true;
}

bool PS5AudioAPI::Play()
{
	std::lock_guard lock(m_mutex);
	m_playing = true;
	m_wake.notify_all();
	return true;
}

bool PS5AudioAPI::Stop()
{
	std::lock_guard lock(m_mutex);
	m_playing = false;
	return true;
}

void PS5AudioAPI::OutputThread()
{
	SetThreadName("PS5AudioOut");
	std::array<sint16, kGrain * 2> grain{};
	const uint32 channels = m_channels;
	for (;;)
	{
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [this] { return m_quit || m_playing; });
			if (m_quit)
				return;
			const float volume = std::clamp(m_volume, 0, 100) / 100.0f;
			const size_t available = m_queue.size() / channels;
			const size_t frames = std::min<size_t>(available, kGrain);
			for (size_t frame = 0; frame < frames; frame++)
			{
				const sint16* in = &m_queue[frame * channels];
				float left, right;
				if (channels == 1)
					left = right = in[0];
				else if (channels == 2)
				{
					left = in[0];
					right = in[1];
				}
				else
				{
					// FL FR FC LFE then the surround pairs: centre and surrounds at -3 dB
					float surroundLeft = 0, surroundRight = 0;
					for (uint32 c = 4; c + 1 < channels; c += 2)
					{
						surroundLeft += in[c];
						surroundRight += in[c + 1];
					}
					left = in[0] + 0.707f * (in[2] + surroundLeft);
					right = in[1] + 0.707f * (in[2] + surroundRight);
				}
				grain[frame * 2] = (sint16)std::clamp(left * volume, -32768.0f, 32767.0f);
				grain[frame * 2 + 1] = (sint16)std::clamp(right * volume, -32768.0f, 32767.0f);
			}
			std::fill(grain.begin() + frames * 2, grain.end(), (sint16)0);
			m_queue.erase(m_queue.begin(), m_queue.begin() + frames * channels);
		}
		if (sceAudioOutOutput(m_port, grain.data()) < 0)
		{
			cemuLog_log(LogType::Force, "PS5 AudioOut: output failed, audio stops");
			return;
		}
	}
}
