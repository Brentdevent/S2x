#pragma once

#include <array>
#include <cstdint>

namespace demonware::marketplace_cwl
{
	inline constexpr std::uint32_t price = 500; // price was verified with YouTube videos showing it off

	struct pack
	{
		const char* tag;
		const char* name;
		std::array<std::uint32_t, 5> items; // emblem, calling card, helmet, charm, camo
	};

	// quartermaster_cwl_menu_uc
	inline constexpr pack packs[]{
		{"CWL_EF", "Echo Fox Pack", {0x200010C, 0x240042B, 0x6632175, 0x7000098, 0x7040004}},
		{"CWL_ENVY", "Team Envy Pack", {0x2000117, 0x240042C, 0x6632181, 0x7000099, 0x704000F}},
		{"CWL_EPSI", "Epsilon Pack", {0x200010D, 0x240042D, 0x6632176, 0x700009A, 0x7040005}},
		{"CWL_EU", "eUnited Pack", {0x200010E, 0x240042E, 0x6632178, 0x700009B, 0x7040006}},
		{"CWL_EVIL", "Evil Geniuses Pack", {0x200010F, 0x240042F, 0x6632179, 0x700009C, 0x7040007}},
		{"CWL_FAZE", "FaZe Clan Pack", {0x2000110, 0x2400430, 0x663217A, 0x700009D, 0x7040008}},
		{"CWL_LUMI", "Luminosity Pack", {0x2000111, 0x2400431, 0x663217B, 0x700009F, 0x7040009}},
		{"CWL_MIND", "Mindfreak Pack", {0x2000112, 0x2400432, 0x663217C, 0x70000A0, 0x704000A}},
		{"CWL_OPT", "OpTic Gaming Pack", {0x2000113, 0x2400433, 0x663217D, 0x70000A1, 0x704000B}},
		{"CWL_RED", "Red Reserve Pack", {0x2000114, 0x2400434, 0x663217E, 0x70000A2, 0x704000C}},
		{"CWL_RISE", "Rise Nation Pack", {0x2000115, 0x2400435, 0x663217F, 0x70000A3, 0x704000D}},
		{"CWL_SPLY", "Splyce Pack", {0x2000116, 0x2400436, 0x6632180, 0x70000A4, 0x704000E}},
		{"CWL_UNI", "Unilad Pack", {0x200011A, 0x2400437, 0x6632184, 0x70000A5, 0x7040012}},
		{"CWL_VITA", "Team Vitality Pack", {0x2000119, 0x2400439, 0x6632183, 0x70000A6, 0x7040011}},
		{"CWL_KALI", "Team Kaliber Pack", {0x2000118, 0x2400438, 0x6632182, 0x700009E, 0x7040010}},
		{"CWL_CWL", "CWL Pack", {0x200012F, 0x240042A, 0x6632177, 0x7000097, 0x7040013}},
	};

	inline const pack* find(const std::uint32_t sku)
	{
		for (const auto& entry : packs)
		{
			if (entry.items[0] == sku)
			{
				return &entry;
			}
		}
		return nullptr;
	}
}
