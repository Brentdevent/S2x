#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "console/console.hpp"

#include <utils/hook.hpp>

namespace asset_overrides
{
	namespace
	{
		constexpr std::string_view override_prefix = "override/";

		utils::hook::detour db_find_xasset_header_hook;

		std::mutex logged_mutex;
		std::unordered_set<std::string> logged_names;

		void log_redirect(const char* name, const std::string& override_name)
		{
			std::lock_guard _(logged_mutex);
			if (logged_names.emplace(name).second)
			{
				console::info("[override] %s -> %s\n", name, override_name.data());
			}
		}

		game::XAssetHeader db_find_xasset_header_stub(const game::XAssetType type, const char* name, const int allow_create_default)
		{
			if (type == game::ASSET_TYPE_RAWFILE && name && !std::string_view(name).starts_with(override_prefix))
			{
				const auto override_name = std::string(override_prefix) + name;

				if (game::DB_XAssetExists(type, override_name.data()) &&
					!game::DB_IsXAssetDefault(type, override_name.data()))
				{
					const auto header = db_find_xasset_header_hook.invoke<game::XAssetHeader>(type, override_name.data(), 0);
					if (header.rawfile)
					{
						log_redirect(name, override_name);
						return header;
					}
				}
			}

			return db_find_xasset_header_hook.invoke<game::XAssetHeader>(type, name, allow_create_default);
		}
	}

	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			db_find_xasset_header_hook.create(game::DB_FindXAssetHeader, db_find_xasset_header_stub);
		}
	};
}

REGISTER_COMPONENT(asset_overrides::component)
