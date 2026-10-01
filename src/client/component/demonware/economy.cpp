#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "economy.hpp"
#include "component/command.hpp"
#include "component/console/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "game/ui_scripting/execution.hpp"
#include "component/ui_scripting.hpp"
#include "game/demonware/economy_tools.hpp"
#include "game/demonware/loot_catalog.hpp"
#include "game/demonware/loot_compatibility.hpp"
#include "game/demonware/runtime_context.hpp"
#include "game/demonware/zombies_loot_policy.hpp"

#include <utils/cryptography.hpp>
#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace economy
{
	namespace
	{
		using namespace demonware;
		// Queue ownership stays on the main pipeline. Revisions also cross the
		// console/LUI and native completion boundaries to suppress stale UI updates.
		bool inventory_refresh_queued{}, wallet_refresh_queued{};
		std::atomic_uint64_t wallet_revision{}, wallet_fetch_revision{};
		std::uint64_t wallet_notified_revision{};
		utils::hook::detour wallet_fetch_success_hook;

		char wallet_fetch_success(void* task)
		{
			const auto controller = utils::hook::invoke<int>(0x839370_g, task);
			const auto result = wallet_fetch_success_hook.invoke<char>(task);
			const auto revision = controller == 0 ? wallet_fetch_revision.load() : 0;
			if (revision)
			{
				scheduler::schedule([revision]
				{
					// A newer grant owns its own fetch/notification. Retry only if this
					// completed fetch is still relevant and LUI is temporarily unavailable.
					if (revision != wallet_revision.load() || revision <= wallet_notified_revision)
					{
						return true;
					}
					if (!ui_scripting::notify("update_currency", {{"controllerIndex", 0}}))
					{
						return false;
					}
					wallet_notified_revision = revision;
					return true;
				}, scheduler::pipeline::main, 100ms);
			}
			return result;
		}

		std::string new_transaction()
		{
			return "local:" + utils::cryptography::random::get_challenge();
		}

		bool start_refresh(const bool item)
		{
			if (!runtime_context::get_snapshot() || !loot_catalog::get_snapshot())
			{
				return false;
			}
			const auto revision = wallet_revision.load();
			// The native task slots reject overlapping requests (0x839650). Once
			// accepted, the stock pump owns request retries and Task 165 pagination.
			const auto started = item ? utils::hook::invoke<char>(0x278B30_g, 0, 1) :
				utils::hook::invoke<char>(0x278A50_g, 0);
			if (!started)
			{
				return false;
			}
			if (!item)
			{
				wallet_fetch_revision = revision;
			}
			(item ? inventory_refresh_queued : wallet_refresh_queued) = false;
			return true;
		}

		void queue_refresh(const bool item)
		{
			const auto revision = item ? 0 : ++wallet_revision;
			scheduler::once([item, revision]
			{
				// A fetch may already include this grant before its queued callback runs.
				if (!item && revision <= wallet_fetch_revision.load())
				{
					return;
				}
				auto& queued = item ? inventory_refresh_queued : wallet_refresh_queued;
				if (std::exchange(queued, true))
				{
					return;
				}
				if (!start_refresh(item))
				{
					scheduler::schedule([item]
					{
						return start_refresh(item);
					}, scheduler::pipeline::main, 100ms);
				}
			}, scheduler::pipeline::main);
		}

		void install_store_functions()
		{
			const auto engine = ui_scripting::get_globals().get("Engine").as<ui_scripting::table>();
			engine["S2xCodPointBundles"] = []
			{
				ui_scripting::table result;
				const auto catalog = loot_catalog::get_snapshot();
				int index{};
				if (catalog)
				{
					for (const auto& bundle : catalog->cod_point_bundles)
					{
						ui_scripting::table row;
						row["id"] = bundle.id;
						row["title"] = bundle.title;
						row["amount"] = bundle.amount;
						row["image"] = bundle.image;
						result[++index] = row;
					}
				}
				return result;
			};
			engine["S2xEconomyTransaction"] = new_transaction;
			engine["S2xTopUpCodPoints"] = [](const std::string& id, const std::string& transaction)
			{
				const auto catalog = loot_catalog::get_snapshot();
				const auto identity = runtime_context::get_snapshot();
				if (!catalog || !identity || transaction.empty())
				{
					return false;
				}
				const auto bundle = std::ranges::find(catalog->cod_point_bundles, id, &loot_catalog::cod_point_bundle::id);
				if (bundle == catalog->cod_point_bundles.end())
				{
					return false;
				}
				const auto result = economy_tools::give_currency(2, bundle->amount, identity->user_id, "topup:" + transaction);
				if (!economy_tools::succeeded(result.status))
				{
					return false;
				}
				queue_refresh(false);
				return true;
			};
		}

		void give(const command::params& args, const bool item)
		{
			std::uint32_t id{}, amount{1};
			if (args.size() < (item ? 2 : 3) || args.size() > 3 ||
				!economy_tools::parse_number(args[1], id) ||
				(args.size() == 3 && !economy_tools::parse_number(args[2], amount)) || !amount || amount > INT32_MAX)
			{
				console::info("Usage: %s <id> %s (decimal or 0x; amount 1..2147483647)\n",
					item ? "giveItem" : "giveCurrency", item ? "[amount]" : "<amount>");
				return;
			}
			const auto identity = runtime_context::get_snapshot();
			if (!identity)
			{
				console::error("Economy is not ready.\n");
				return;
			}
			const auto result = item ? economy_tools::give_item(id, amount, identity->user_id, new_transaction()) :
				economy_tools::give_currency(id, amount, identity->user_id, new_transaction());
			if (!economy_tools::succeeded(result.status))
			{
				console::error("Grant failed (%u): invalid ID/state, capacity or persistence error; nothing granted.\n",
					static_cast<unsigned>(result.status));
				return;
			}
			queue_refresh(item);
			console::info("Granted %u of %s %u (0x%x); saved.\n", amount, item ? "item" : "currency", id, id);
		}

		std::uint32_t quantity(const marketplace_store::snapshot& state, const std::uint32_t id)
		{
			const auto row = std::ranges::find(state.inventory, id, &marketplace_store::inventory_record::item_id);
			return row == state.inventory.end() ? 0 : row->quantity;
		}

		struct listing_options
		{
			std::vector<std::string> terms;
			std::filesystem::path file;
		};

		std::optional<listing_options> parse_listing(const command::params& args,
			const bool dump, const bool allow_filter, const char* default_name)
		{
			const auto usage = [&]() -> std::optional<listing_options>
			{
				console::info("Usage: %s%s%s\n", args[0], dump ? " [filename]" : "",
					allow_filter ? " [filter]" : "");
				return std::nullopt;
			};
			listing_options options;
			if (dump)
			{
				const std::string name = args.size() > 1 ? args[1] : default_name;
				if (name.empty() || name == "." || name == ".." || name.starts_with("--") ||
					name.find_first_of("/\\:<>\"|?*") != std::string::npos)
				{
					return usage();
				}
				options.file = std::filesystem::path("s2x") / name;
				if (!options.file.has_extension())
				{
					options.file += ".txt";
				}
			}

			for (int index = dump ? 2 : 1; index < args.size(); ++index)
			{
				const std::string argument = args[index];
				if (!allow_filter || argument.starts_with("--"))
				{
					return usage();
				}

				std::istringstream words(utils::string::to_lower(argument));
				std::string term;
				while (words >> term)
				{
					options.terms.push_back(term);
				}
			}
			return options;
		}

		bool matches(const listing_options& options, const std::string& row)
		{
			const auto text = utils::string::to_lower(row);
			return std::ranges::all_of(options.terms, [&](const auto& term)
			{
				return text.find(term) != std::string::npos;
			});
		}

		void print_listing(const listing_options& options, const std::vector<std::string>& rows)
		{
			const auto count = std::to_string(rows.size()) + " matches.\n";
			if (options.file.empty())
			{
				for (const auto& row : rows)
				{
					console::info("%s\n", row.c_str());
				}
				console::info("%s", count.c_str());
				return;
			}

			std::string output;
			for (const auto& row : rows)
			{
				output += row + '\n';
			}
			output += count;
			std::error_code error;
			std::filesystem::create_directories(options.file.parent_path(), error);
			std::ofstream file;
			if (!error)
			{
				file.open(options.file, std::ios::binary | std::ios::trunc);
				file.write(output.data(), static_cast<std::streamsize>(output.size()));
				file.close();
			}
			const auto path = options.file.generic_string();
			if (error || !file)
			{
				console::error("Could not write %s.\n", path.c_str());
				return;
			}
			console::info("Wrote %zu matches to %s.\n", rows.size(), path.c_str());
		}

		const char* rarity_name(const loot_catalog::item_definition& row)
		{
			constexpr std::array names{"Common", "Rare", "Legendary", "Epic", "Heroic"};
			return row.rarity_valid && row.item.rarity >= 0 && row.item.rarity < names.size()
				? names[row.item.rarity] : "Unknown";
		}

		const char* drop_status(const loot_catalog::supply_drop& drop)
		{
			if (loot_compatibility::is_confirmed_mp_supply_drop(drop) || zombies_loot_policy::supports(drop))
			{
				return "supported/openable";
			}
			const auto complex = std::ranges::any_of(drop.slots, [](const auto& slot)
			{
				return slot.dupe_protection || !slot.operation.empty() ||
					(slot.type != 0 && slot.type != 23 && slot.type != 24);
			});
			return complex ? "deliberately omitted: targeted/event/no-duplicate" : "known unsupported";
		}

		std::string item_line(const loot_catalog::item_definition& row, const marketplace_store::snapshot& state)
		{
			const auto& item = row.item;
			auto name = row.display_name.empty() ? item.reference : row.display_name;
			std::replace_if(name.begin(), name.end(), [](const char c) { return c == '\r' || c == '\n' || c == '\t'; }, ' ');
			std::ostringstream line;
			line << "0x" << std::hex << item.item_id << std::dec << " (" << item.item_id << ") | qty "
				<< quantity(state, item.item_id) << " | " << rarity_name(row) << " | " << item.group << " | " << name;
			if (!row.display_name.empty() && row.display_name != item.reference)
			{
				line << " | ref " << item.reference;
			}
			return line.str();
		}

		std::string drop_line(const loot_catalog::supply_drop& drop, const loot_catalog::catalog& catalog,
			const marketplace_store::snapshot& state)
		{
			const auto item = std::ranges::find_if(catalog.items, [&](const auto& row) { return row.item.item_id == drop.item_id; });
			std::ostringstream line;
			if (item != catalog.items.end())
			{
				line << item_line(*item, state) << " | ";
			}
			else
			{
				line << "0x" << std::hex << drop.item_id << std::dec << " (" << drop.item_id << ") | qty "
					<< quantity(state, drop.item_id) << " | ";
			}
			line << "supply_drop | " << drop.backend_id << " | " << drop_status(drop);
			return line.str();
		}

		void list_items(const command::params& args, const bool dump)
		{
			const auto options = parse_listing(args, dump, true, "items.txt");
			if (!options)
			{
				return;
			}
			const auto catalog = loot_catalog::get_snapshot();
			const auto state = marketplace_store::get_snapshot();
			if (!catalog || state.status != marketplace_store::store_status::ready)
			{
				console::error("Catalog/economy is not ready.\n");
				return;
			}

			std::vector<std::string> rows;
			std::unordered_set<std::uint32_t> seen;
			for (const auto& row : catalog->items)
			{
				const auto id = row.item.item_id;
				if (!id)
				{
					continue;
				}
				const auto drop = std::ranges::find(catalog->supply_drops, id, &loot_catalog::supply_drop::item_id);
				auto line = item_line(row, state);
				if (drop != catalog->supply_drops.end())
				{
					line += std::string(" | supply_drop | ") + drop->backend_id + " | " + drop_status(*drop);
				}
				if (matches(*options, line) && seen.insert(id).second)
				{
					rows.push_back(std::move(line));
				}
			}
			for (const auto& drop : catalog->supply_drops)
			{
				auto line = drop_line(drop, *catalog, state);
				if (matches(*options, line) && seen.insert(drop.item_id).second)
				{
					rows.push_back(std::move(line));
				}
			}
			print_listing(*options, rows);
		}

		void list_currencies(const command::params& args, const bool dump)
		{
			const auto options = parse_listing(args, dump, false, "currencies.txt");
			if (!options)
			{
				return;
			}
			auto state = marketplace_store::get_snapshot();
			if (state.status != marketplace_store::store_status::ready)
			{
				console::error("Economy is not ready.\n");
				return;
			}
			for (const auto id : {2, 6, 7})
			{
				if (std::ranges::none_of(state.currencies, [id](const auto& row) { return row.currency_id == id; }))
				{
					state.currencies.push_back({static_cast<std::uint8_t>(id), 0});
				}
			}
			std::ranges::sort(state.currencies, {}, &marketplace_store::currency_record::currency_id);
			std::vector<std::string> rows;
			for (const auto& row : state.currencies)
			{
				const auto name = economy_tools::currency_name(row.currency_id);
				std::ostringstream line;
				line << "0x" << std::hex << static_cast<unsigned>(row.currency_id) << std::dec << " ("
					<< static_cast<unsigned>(row.currency_id) << ") | qty " << row.value << " | " << (name ? name : "(unnamed)");
				rows.push_back(line.str());
			}
			print_listing(*options, rows);
		}

		void list_drops(const command::params& args, const bool dump)
		{
			const auto options = parse_listing(args, dump, true, "drops.txt");
			if (!options)
			{
				return;
			}
			const auto catalog = loot_catalog::get_snapshot();
			const auto state = marketplace_store::get_snapshot();
			if (!catalog || state.status != marketplace_store::store_status::ready)
			{
				console::error("Catalog/economy is not ready.\n");
				return;
			}

			std::vector<std::string> rows;
			for (const auto& drop : catalog->supply_drops)
			{
				auto line = drop_line(drop, *catalog, state);
				if (matches(*options, line))
				{
					rows.push_back(std::move(line));
				}
			}
			print_listing(*options, rows);
		}
	}

	void request_inventory_refresh()
	{
		queue_refresh(true);
	}

	class component final : public multiplayer_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			wallet_fetch_success_hook.create(0x27B230_g, wallet_fetch_success);
			ui_scripting::on_start(install_store_functions);

			command::add("giveItem", [](const command::params& args)
			{
				give(args, true);
			});
			command::add("giveCurrency", [](const command::params& args)
			{
				give(args, false);
			});
			command::add("listItems", [](const command::params& args)
			{
				list_items(args, false);
			});
			command::add("dumpItems", [](const command::params& args)
			{
				list_items(args, true);
			});
			command::add("listCurrencies", [](const command::params& args)
			{
				list_currencies(args, false);
			});
			command::add("dumpCurrencies", [](const command::params& args)
			{
				list_currencies(args, true);
			});
			command::add("listDrops", [](const command::params& args)
			{
				list_drops(args, false);
			});
			command::add("dumpDrops", [](const command::params& args)
			{
				list_drops(args, true);
			});
		}
	};
}

REGISTER_COMPONENT(economy::component)
