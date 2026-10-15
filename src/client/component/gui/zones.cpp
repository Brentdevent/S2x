#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "component/fastfiles.hpp"
#include "component/scheduler.hpp"
#include "gui.hpp"

#include <utils/string.hpp>

#include <misc/cpp/imgui_stdlib.h>

namespace gui::zones
{
	namespace
	{
		constexpr auto pool_refresh_interval = 500ms;

		std::string zone_filter;
		std::string pool_filter;
		std::string load_zone_name;
		int load_zone_flags = 4;
		bool hide_empty_pools = true;

		int pool_counts[game::ASSET_TYPE_COUNT]{};
		std::chrono::steady_clock::time_point last_pool_refresh{};

		void refresh_pool_counts()
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - last_pool_refresh < pool_refresh_interval)
			{
				return;
			}

			last_pool_refresh = now;
			fastfiles::get_asset_counts(pool_counts);
		}

		void draw_load_zone()
		{
			ImGui::InputText("zone", &load_zone_name);
			ImGui::InputInt("alloc flags", &load_zone_flags);

			if (ImGui::Button("load") && !load_zone_name.empty())
			{
				const auto name = load_zone_name;
				const auto flags = load_zone_flags;

				scheduler::once([name, flags]
				{
					if (fastfiles::try_load_zone(name, flags))
					{
						notification("Zones", utils::string::va("Loading zone '%s'", name.data()));
					}
					else
					{
						notification("Zones", utils::string::va("Zone '%s' could not be found", name.data()));
					}
				}, scheduler::pipeline::main);
			}
		}

		void draw_loaded_zones()
		{
			const auto zones = fastfiles::get_loaded_zones();

			ImGui::Text("loaded zones: %zu", zones.size());
			ImGui::InputText("zone filter", &zone_filter);

			constexpr auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable;

			if (!ImGui::BeginTable("zones", 3, flags, ImVec2(0.f, 250.f)))
			{
				return;
			}

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("index", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("flags", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (const auto& zone : zones)
			{
				if (!contains_lower(zone.name.data(), zone_filter))
				{
					continue;
				}

				ImGui::PushID(zone.index);
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", zone.index);

				ImGui::TableSetColumnIndex(1);
				if (ImGui::Button(zone.name.data()))
				{
					copy_to_clipboard(zone.name);
				}

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("0x%X", zone.flags);

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		void draw_pool_usage()
		{
			refresh_pool_counts();

			ImGui::InputText("type filter", &pool_filter);
			ImGui::Checkbox("hide empty pools", &hide_empty_pools);

			constexpr auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable;

			if (!ImGui::BeginTable("pools", 4, flags))
			{
				return;
			}

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("limit", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("usage", ImGuiTableColumnFlags_WidthFixed, 150.f);
			ImGui::TableHeadersRow();

			auto total = 0;
			for (auto i = 0; i < game::ASSET_TYPE_COUNT; i++)
			{
				const auto type = static_cast<game::XAssetType>(i);
				const auto count = pool_counts[i];
				const auto limit = fastfiles::get_pool_size(type);
				const auto name = game::mp::g_assetNames[i] ? game::mp::g_assetNames[i] : "unknown";

				total += count;

				if ((hide_empty_pools && count == 0) || !contains_lower(name, pool_filter))
				{
					continue;
				}

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(name);

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%i", count);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%i", limit);

				ImGui::TableSetColumnIndex(3);
				const auto usage = limit > 0 ? static_cast<float>(count) / static_cast<float>(limit) : 0.f;
				const auto is_full = limit > 0 && count >= limit;

				if (is_full)
				{
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.2f, 0.2f, 1.f));
				}

				ImGui::ProgressBar(std::min(usage, 1.f), ImVec2(-1.f, 0.f),
					limit > 0 ? utils::string::va("%.1f%%", usage * 100.f) : "n/a");

				if (is_full)
				{
					ImGui::PopStyleColor();
				}
			}

			ImGui::EndTable();
			ImGui::Text("total assets: %i", total);
		}

		void render_window()
		{
			ImGui::SetNextWindowSize(ImVec2(550.f, 700.f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Zones", get_menu_toggle("zones")))
			{
				if (ImGui::CollapsingHeader("Load zone", ImGuiTreeNodeFlags_DefaultOpen))
				{
					draw_load_zone();
				}

				if (ImGui::CollapsingHeader("Loaded zones", ImGuiTreeNodeFlags_DefaultOpen))
				{
					draw_loaded_zones();
				}

				if (ImGui::CollapsingHeader("Pool usage", ImGuiTreeNodeFlags_DefaultOpen))
				{
					draw_pool_usage();
				}
			}

			ImGui::End();
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (!fastfiles::is_supported())
			{
				return;
			}

			register_menu("zones", "Zones & Pools", render_window);
		}
	};
}

REGISTER_COMPONENT(gui::zones::component)
