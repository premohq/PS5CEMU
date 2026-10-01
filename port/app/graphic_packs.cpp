// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: one game's graphic packs for the launcher (emulator.h).
//
// The packs are the ones Cemu loaded at start (GraphicPack2::LoadAll), and the choices are saved
// as Cemu's Graphic Packs window saves them (GraphicPacksWindow2::SaveStateToConfig), so this file
// keeps Cemu's MPL-2.0 licence. Changes apply to the next game started, as on the desktop.

#include "emulator.h"
#include "../ps5/log.h"

#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/CemuConfig.h"

namespace ps5emu
{
	namespace
	{
		using PackPtr = std::shared_ptr<GraphicPack2>;

		// A game's packs in the order the launcher lists them: by their path in the pack tree.
		std::vector<PackPtr> PacksOf(uint64_t titleId)
		{
			std::vector<PackPtr> packs;
			for (const auto& pack : GraphicPack2::GetGraphicPacks())
				if (pack->ContainsTitleId(titleId))
					packs.push_back(pack);
			std::sort(packs.begin(), packs.end(), [](const PackPtr& a, const PackPtr& b) {
				return boost::ilexicographical_compare(a->GetVirtualPath(), b->GetVirtualPath());
			});
			return packs;
		}

		// As GraphicPacksWindow2::SaveStateToConfig.
		void SaveState()
		{
			auto& data = GetConfigHandle().data();
			data.graphic_pack_entries.clear();
			for (const auto& pack : GraphicPack2::GetGraphicPacks())
			{
				const auto filename = _utf8ToPath(pack->GetNormalizedPathString());
				if (pack->IsEnabled())
				{
					auto& entry = data.graphic_pack_entries[filename];
					for (const auto& preset : pack->GetActivePresets())
						entry.try_emplace(preset->category, preset->name);
				}
				else if (pack->IsDefaultEnabled())
					data.graphic_pack_entries[filename].try_emplace("_disabled", "false");
			}
			GetConfigHandle().Save();
		}

		// The preset categories with something to choose, in the pack's order.
		std::vector<std::pair<std::string, std::vector<GraphicPack2::PresetPtr>>> Choices(const GraphicPack2& pack)
		{
			std::vector<std::string> order;
			auto categorized = pack.GetCategorizedPresets(order);
			std::vector<std::pair<std::string, std::vector<GraphicPack2::PresetPtr>>> choices;
			for (const auto& category : order)
			{
				std::vector<GraphicPack2::PresetPtr> visible;
				for (const auto& preset : categorized[category])
					if (pack.IsPresetVisible(preset))
						visible.push_back(preset);
				if (visible.size() > 1)
					choices.emplace_back(category, std::move(visible));
			}
			return choices;
		}
	}

	std::vector<GraphicPackInfo> ListGraphicPacks(uint64_t titleId)
	{
		std::vector<GraphicPackInfo> result;
		for (const auto& pack : PacksOf(titleId))
		{
			GraphicPackInfo info;
			// "Game name/Graphics/Resolution": the last part names the pack, the middle its kind
			std::vector<std::string> parts;
			boost::split(parts, pack->GetVirtualPath(), boost::is_any_of("/"));
			info.name = pack->HasName() ? pack->GetName() : parts.back();
			if (parts.size() > 2)
				info.category = boost::join(std::vector<std::string>(parts.begin() + 1, parts.end() - 1), " / ");
			info.description = pack->GetDescription();
			info.enabled = pack->IsEnabled();
			const auto choices = Choices(*pack);
			info.hasPresets = !choices.empty();
			for (const auto& [category, presets] : choices)
			{
				if (!info.preset.empty())
					info.preset += ", ";
				const std::string active = pack->GetActivePreset(category);
				info.preset += category.empty() ? active : category + ": " + active;
			}
			result.push_back(std::move(info));
		}
		return result;
	}

	int EnabledGraphicPackCount(uint64_t titleId)
	{
		int count = 0;
		for (const auto& pack : PacksOf(titleId))
			count += pack->IsEnabled();
		return count;
	}

	bool ToggleGraphicPack(uint64_t titleId, size_t index)
	{
		const auto packs = PacksOf(titleId);
		if (index >= packs.size())
			return false;
		const auto& pack = packs[index];
		pack->SetEnabled(!pack->IsEnabled());
		SaveState();
		ps5log::Line("[packs] {} {}", pack->GetVirtualPath(), pack->IsEnabled() ? "on" : "off");
		return pack->IsEnabled();
	}

	void CycleGraphicPackPreset(uint64_t titleId, size_t index, int delta)
	{
		const auto packs = PacksOf(titleId);
		if (index >= packs.size())
			return;
		const auto& pack = packs[index];
		const auto choices = Choices(*pack);
		if (choices.empty())
			return;
		const auto& [category, presets] = choices.front();
		const std::string active = pack->GetActivePreset(category);
		int position = 0;
		for (size_t i = 0; i < presets.size(); i++)
			if (presets[i]->name == active)
				position = (int)i;
		position = (position + delta + (int)presets.size()) % (int)presets.size();
		pack->SetActivePreset(category, presets[position]->name);
		SaveState();
		ps5log::Line("[packs] {}: preset {}", pack->GetVirtualPath(), presets[position]->name);
	}
}
