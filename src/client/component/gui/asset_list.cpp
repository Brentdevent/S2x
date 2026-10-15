#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "component/fastfiles.hpp"
#include "gui.hpp"

#include <utils/string.hpp>

#include <misc/cpp/imgui_stdlib.h>

namespace gui::asset_list
{
	namespace
	{
		constexpr auto max_zone_slots = 0x421;
		constexpr auto max_table_columns = 512;

		struct asset_row
		{
			const game::XAssetEntry* entry;
			const char* name;
		};

		struct text_viewer
		{
			std::string title;
			std::string text;
			bool open;
		};

		struct table_viewer
		{
			std::string title;
			int column_count;
			std::vector<std::string> cells;
			bool open;
		};

		bool shown_assets[game::ASSET_TYPE_COUNT]{};
		std::string asset_type_filter;
		std::string assets_name_filter[game::ASSET_TYPE_COUNT];
		std::string assets_value_filter[game::ASSET_TYPE_COUNT];
		std::string zone_name_filter[game::ASSET_TYPE_COUNT];
		bool default_only[game::ASSET_TYPE_COUNT]{};
		bool show_asset_zone = true;

		std::vector<text_viewer> text_viewers;
		std::vector<table_viewer> table_viewers;

		const char* get_type_name(const int type)
		{
			const auto name = game::mp::g_assetNames[type];
			return name ? name : "unknown";
		}

		std::string get_zone_label(const unsigned int zone_index)
		{
			return zone_index ? fastfiles::get_zone_name(zone_index) : "default";
		}

		bool is_zone_shown(const game::XAssetType type, const unsigned int zone_index)
		{
			if (zone_name_filter[type].empty())
			{
				return true;
			}

			static std::array<std::string, max_zone_slots> zone_names{};
			static auto last_frame = -1;

			if (last_frame != ImGui::GetFrameCount())
			{
				last_frame = ImGui::GetFrameCount();
				for (auto i = 0u; i < zone_names.size(); i++)
				{
					zone_names[i] = get_zone_label(i);
				}
			}

			return zone_index < zone_names.size() && contains_lower(zone_names[zone_index].data(), zone_name_filter[type]);
		}

		bool has_viewer(const game::XAssetType type)
		{
			return type == game::ASSET_TYPE_RAWFILE || type == game::ASSET_TYPE_STRINGTABLE;
		}

		void open_rawfile(const char* name, const game::XAssetHeader header)
		{
			const auto* rawfile = header.rawfile;
			if (!rawfile)
			{
				return;
			}

			const auto length = game::DB_GetRawFileLen(rawfile);
			std::string buffer(std::max(length, 0), '\0');
			if (length > 0)
			{
				game::DB_GetRawBuffer(rawfile, buffer.data(), length);
				buffer.resize(strnlen(buffer.data(), buffer.size()));
			}

			text_viewers.emplace_back(name, std::move(buffer), true);
		}

		void open_stringtable(const char* name, const game::XAssetHeader header)
		{
			const auto* table = header.stringTable;
			if (!table || table->columnCount <= 0 || table->rowCount < 0)
			{
				return;
			}

			table_viewer viewer{};
			viewer.title = name;
			viewer.column_count = table->columnCount;
			viewer.open = true;

			const auto count = table->columnCount * table->rowCount;
			viewer.cells.reserve(count);
			for (auto i = 0; i < count; i++)
			{
				const auto* value = table->values ? table->values[i].string : nullptr;
				viewer.cells.emplace_back(value ? value : "");
			}

			table_viewers.emplace_back(std::move(viewer));
		}

		void open_viewer(const game::XAssetType type, const char* name, const game::XAssetHeader header)
		{
			if (type == game::ASSET_TYPE_RAWFILE)
			{
				open_rawfile(name, header);
			}
			else if (type == game::ASSET_TYPE_STRINGTABLE)
			{
				open_stringtable(name, header);
			}
		}

		void draw_viewers()
		{
			for (auto i = text_viewers.begin(); i != text_viewers.end();)
			{
				ImGui::SetNextWindowSize(ImVec2(600.f, 500.f), ImGuiCond_FirstUseEver);
				if (ImGui::Begin(utils::string::va("%s##rawfile", i->title.data()), &i->open))
				{
					if (ImGui::Button("copy"))
					{
						copy_to_clipboard(i->text);
					}

					ImGui::InputTextMultiline("##text", &i->text, ImVec2(-1.f, -1.f), ImGuiInputTextFlags_ReadOnly);
				}

				ImGui::End();
				i = i->open ? i + 1 : text_viewers.erase(i);
			}

			for (auto i = table_viewers.begin(); i != table_viewers.end();)
			{
				ImGui::SetNextWindowSize(ImVec2(700.f, 500.f), ImGuiCond_FirstUseEver);
				if (ImGui::Begin(utils::string::va("%s##stringtable", i->title.data()), &i->open))
				{
					const auto column_count = std::min(i->column_count, max_table_columns);
					constexpr auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
						ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

					if (ImGui::BeginTable("cells", column_count, flags))
					{
						const auto row_count = static_cast<int>(i->cells.size()) / i->column_count;

						ImGuiListClipper clipper;
						clipper.Begin(row_count);
						while (clipper.Step())
						{
							for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
							{
								ImGui::TableNextRow();
								for (auto column = 0; column < column_count; column++)
								{
									ImGui::TableSetColumnIndex(column);
									ImGui::TextUnformatted(i->cells[row * i->column_count + column].data());
								}
							}
						}

						ImGui::EndTable();
					}
				}

				ImGui::End();
				i = i->open ? i + 1 : table_viewers.erase(i);
			}
		}

		bool is_row_shown(const game::XAssetType type, const game::XAssetEntry* entry, const char* name)
		{
			if (!name || !*name)
			{
				return false;
			}

			if (default_only[type] && entry->zoneIndex != 0)
			{
				return false;
			}

			if (!contains_lower(name, assets_name_filter[type]) || !is_zone_shown(type, entry->zoneIndex))
			{
				return false;
			}

			if (type == game::ASSET_TYPE_LOCALIZE)
			{
				const auto* localize = entry->asset.header.localize;
				return localize && contains_lower(localize->value, assets_value_filter[type]);
			}

			return true;
		}

		void draw_table_row(const game::XAssetType type, const asset_row& row, const int id)
		{
			const auto* entry = row.entry;
			const auto is_default = entry->zoneIndex == 0;

			if (is_default)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.43f, 0.15f, 0.15f, 1.f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.26f, 0.26f, 1.f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.98f, 0.06f, 0.06f, 1.f));
			}

			ImGui::PushID(id);
			ImGui::TableNextRow();

			auto column = 0;
			if (has_viewer(type))
			{
				ImGui::TableSetColumnIndex(column++);
				if (ImGui::Button("view"))
				{
					open_viewer(type, row.name, entry->asset.header);
				}
			}

			if (show_asset_zone)
			{
				ImGui::TableSetColumnIndex(column++);
				ImGui::TextUnformatted(get_zone_label(entry->zoneIndex).data());
			}

			ImGui::TableSetColumnIndex(column++);
			if (ImGui::Button(row.name))
			{
				copy_to_clipboard(row.name);
			}

			if (type == game::ASSET_TYPE_LOCALIZE)
			{
				ImGui::TableSetColumnIndex(column++);
				const auto* value = entry->asset.header.localize->value;
				if (ImGui::Button(value ? value : ""))
				{
					copy_to_clipboard(value ? value : "");
				}
			}

			ImGui::PopID();

			if (is_default)
			{
				ImGui::PopStyleColor(3);
			}
		}

		void draw_asset_type_list()
		{
			ImGui::Checkbox("show asset zone", &show_asset_zone);
			ImGui::InputText("asset type", &asset_type_filter);
			ImGui::BeginChild("asset type list");

			for (auto i = 0; i < game::ASSET_TYPE_COUNT; i++)
			{
				const auto name = get_type_name(i);
				if (contains_lower(name, asset_type_filter))
				{
					ImGui::Checkbox(name, &shown_assets[i]);
				}
			}

			ImGui::EndChild();
		}

		void draw_asset_list(const game::XAssetType type)
		{
			if (!shown_assets[type])
			{
				return;
			}

			const auto& io = ImGui::GetIO();
			ImGui::SetNextWindowSizeConstraints(ImVec2(500.f, 500.f), io.DisplaySize);

			if (!ImGui::Begin(get_type_name(type), &shown_assets[type]))
			{
				ImGui::End();
				return;
			}

			std::vector<asset_row> rows;
			auto total = 0;

			fastfiles::enum_asset_entries(type, [&](const game::XAssetEntry* entry)
			{
				total++;

				const auto name = game::DB_GetXAssetName(&entry->asset);
				if (is_row_shown(type, entry, name))
				{
					rows.emplace_back(entry, name);
				}
			}, true);

			ImGui::Text("count: %i / %i, shown: %zu", total, fastfiles::get_pool_size(type), rows.size());
			ImGui::InputText("name", &assets_name_filter[type]);

			if (type == game::ASSET_TYPE_LOCALIZE)
			{
				ImGui::InputText("value", &assets_value_filter[type]);
			}

			ImGui::InputText("zone name", &zone_name_filter[type]);
			ImGui::Checkbox("default assets only", &default_only[type]);

			auto column_count = 1;
			column_count += has_viewer(type);
			column_count += show_asset_zone;
			column_count += type == game::ASSET_TYPE_LOCALIZE;

			constexpr auto flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuterH |
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;

			if (ImGui::BeginTable("assets", column_count, flags))
			{
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(rows.size()));
				while (clipper.Step())
				{
					for (auto i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
					{
						draw_table_row(type, rows[i], i);
					}
				}

				ImGui::EndTable();
			}

			ImGui::End();
		}

		void render_window()
		{
			ImGui::SetNextWindowSize(ImVec2(300.f, 500.f), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Asset list", get_menu_toggle("asset_list")))
			{
				draw_asset_type_list();
			}

			ImGui::End();

			for (auto i = 0; i < game::ASSET_TYPE_COUNT; i++)
			{
				draw_asset_list(static_cast<game::XAssetType>(i));
			}
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

			register_menu("asset_list", "Asset List", render_window);

			on_frame(draw_viewers);
		}
	};
}

REGISTER_COMPONENT(gui::asset_list::component)
