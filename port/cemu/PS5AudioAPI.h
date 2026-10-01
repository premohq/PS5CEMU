// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: Cemu's audio output on the PS5's AudioOut.

#pragma once

#include "IAudioAPI.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

class PS5AudioAPI : public IAudioAPI
{
public:
	class PS5DeviceDescription : public DeviceDescription
	{
	public:
		PS5DeviceDescription() : DeviceDescription(L"PS5 audio output") {}
		std::wstring GetIdentifier() const override { return kDeviceId; }
	};

	// the one device: the console's main audio output
	static constexpr const wchar_t* kDeviceId = L"ps5-audioout";

	PS5AudioAPI(uint32 samplerate, uint32 channels, uint32 samples_per_block, uint32 bits_per_sample);
	~PS5AudioAPI() override;

	AudioAPI GetType() const override { return PS5AudioOut; }
	bool NeedAdditionalBlocks() const override;
	bool FeedBlock(sint16* data) override;
	bool Play() override;
	bool Stop() override;

	static std::vector<DeviceDescriptionPtr> GetDevices();
	static bool InitializeStatic();

private:
	void OutputThread();

	int m_port = -1;
	std::thread m_thread;
	bool m_quit = false;

	mutable std::mutex m_mutex;
	std::condition_variable m_wake;
	std::vector<sint16> m_queue; // interleaved samples at the stream's channel count
	size_t m_queueCapacity = 0;
};
