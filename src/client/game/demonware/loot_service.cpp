#include <std_include.hpp>

#include "loot_service.hpp"
#include "loot_catalog.hpp"
#include "loot_store.hpp"
#include "achievement_store.hpp"
#include "challenge_service.hpp"

#include "component/console/console.hpp"

#include <mutex>
#include <random>

namespace demonware::loot_service
{
	namespace
	{
		constexpr std::size_t supply_drop_item_count = 3;
		constexpr std::size_t zombie_supply_drop_item_count = 5;
		constexpr std::uint8_t currency_cod_points = 2;
		constexpr std::uint32_t infinite_cod_points_balance = 999999;
		constexpr std::uint8_t currency_armory_credits = 6;
		constexpr std::uint32_t payroll_amount = 200;
		constexpr std::uint64_t payroll_cooldown = 4 * 60 * 60;
		constexpr auto payroll_achievement = "payroll_officer";
		constexpr auto payroll_achievement_kind = 5;

		std::mutex settings_mutex{};
		settings current_settings{};

		settings get_settings()
		{
			std::lock_guard lock{settings_mutex};
			return current_settings;
		}

		std::uint32_t add_capped(const std::uint32_t balance, const std::uint32_t amount)
		{
			return amount > std::numeric_limits<std::uint32_t>::max() - balance
				? std::numeric_limits<std::uint32_t>::max() : balance + amount;
		}

		using allocator_type = rapidjson::Document::AllocatorType;

		rapidjson::Value make_string(const std::string_view value, allocator_type& allocator)
		{
			return rapidjson::Value{value.data(), static_cast<rapidjson::SizeType>(value.size()), allocator};
		}

		rapidjson::Document make_response(const std::string_view action, const std::string_view client_transaction,
			const bool success)
		{
			rapidjson::Document response{};
			response.SetObject();
			auto& allocator = response.GetAllocator();
			response.AddMember("Version", 0, allocator);
			response.AddMember("Action", make_string(action, allocator), allocator);
			response.AddMember("Status", rapidjson::StringRef(success ? "ok" : "error"), allocator);
			response.AddMember("ClientTx", make_string(client_transaction, allocator), allocator);
			return response;
		}

		std::string serialize(const rapidjson::Document& response)
		{
			rapidjson::StringBuffer buffer{};
			rapidjson::Writer<rapidjson::StringBuffer, rapidjson::Document::EncodingType,
				rapidjson::ASCII<>> writer{buffer};
			response.Accept(writer);
			return {buffer.GetString(), buffer.GetSize()};
		}

		rapidjson::Value make_inventory(const std::map<std::uint32_t, std::uint32_t>& changes,
			allocator_type& allocator)
		{
			const auto now = static_cast<std::uint32_t>(time(nullptr));
			rapidjson::Value inventory{rapidjson::kArrayType};
			for (const auto& [guid, quantity] : changes)
			{
				rapidjson::Value entry{rapidjson::kObjectType};
				entry.AddMember("item_id", guid, allocator);
				entry.AddMember("collision_field", 0, allocator);
				entry.AddMember("expiry_duration", 0, allocator);
				entry.AddMember("item_quantity", quantity, allocator);
				entry.AddMember("mod_date_time", now, allocator);
				inventory.PushBack(entry, allocator);
			}

			return inventory;
		}

		std::string open_supply_drop(const std::string_view action, const std::string_view client_transaction,
			const rapidjson::Value& request)
		{
			if (!request.HasMember("SupplyDropID") || !request["SupplyDropID"].IsString())
			{
				return serialize(make_response(action, client_transaction, false));
			}

			const std::string_view name{request["SupplyDropID"].GetString(), request["SupplyDropID"].GetStringLength()};
			const auto drop = loot_catalog::find_drop(name);
			const auto items = drop ? loot_catalog::roll_drop(*drop,
				drop->zombies ? zombie_supply_drop_item_count : supply_drop_item_count) : std::vector<std::uint32_t>{};
			if (items.empty())
			{
				console::demonware("[DW] loot: cannot open supply drop '%.*s'\n",
					static_cast<int>(name.size()), name.data());
				return serialize(make_response(action, client_transaction, false));
			}

			std::vector<std::uint32_t> candidates{drop->guid};
			for (const std::string_view suffix : {"_testA", "_testB"})
			{
				if (name.ends_with(suffix))
				{
					if (const auto base = loot_catalog::find_drop_guid(name.substr(0, name.size() - suffix.size())))
					{
						candidates.push_back(*base);
					}
				}
			}

			std::map<std::uint32_t, std::uint32_t> changes{};
			const auto saved = loot_store::mutate([&](loot_store::state& state)
			{
				auto owned = state.items.end();
				for (const auto guid : candidates)
				{
					owned = state.items.find(guid);
					if (owned != state.items.end() && owned->second)
					{
						break;
					}
				}

				if (owned == state.items.end() || !owned->second)
				{
					return false;
				}

				changes[owned->first] = --owned->second;
				if (!owned->second)
				{
					state.items.erase(owned);
				}

				for (const auto guid : items)
				{
					changes[guid] = ++state.items[guid];
				}

				return true;
			});

			if (!saved)
			{
				console::demonware("[DW] loot: supply drop '%.*s' not owned\n",
					static_cast<int>(name.size()), name.data());
				return serialize(make_response(action, client_transaction, false));
			}

			auto response = make_response(action, client_transaction, true);
			auto& allocator = response.GetAllocator();
			rapidjson::Value granted{rapidjson::kArrayType};
			for (const auto guid : items)
			{
				rapidjson::Value entry{rapidjson::kObjectType};
				entry.AddMember("id", guid, allocator);
				granted.PushBack(entry, allocator);
			}

			response.AddMember("GrantedItems", granted, allocator);
			response.AddMember("DetailedInventory", make_inventory(changes, allocator), allocator);
			console::demonware("[DW] loot: opened '%.*s' (%zu items)\n",
				static_cast<int>(name.size()), name.data(), items.size());
			return serialize(response);
		}

		std::string start_mission(const std::string_view action, const std::string_view client_transaction)
		{
			static std::mt19937 engine{std::random_device{}()};
			std::uniform_int_distribution<std::uint32_t> distribution{1, std::numeric_limits<std::int32_t>::max()};

			auto response = make_response(action, client_transaction, true);
			response.AddMember("MissionInstanceId", distribution(engine), response.GetAllocator());
			return serialize(response);
		}

		std::string end_mission(const std::string_view action, const std::string_view client_transaction)
		{
			const auto grant = get_settings().match_cod_points;
			std::uint32_t balance_before{};
			const auto saved = grant && loot_store::mutate([&](loot_store::state& state)
			{
				auto& balance = state.currencies[currency_cod_points];
				balance_before = balance;
				balance = add_capped(balance, grant);
				return true;
			});

			auto response = make_response(action, client_transaction, true);
			auto& allocator = response.GetAllocator();
			rapidjson::Value currencies{rapidjson::kArrayType};

			if (saved)
			{
				rapidjson::Value entry{rapidjson::kObjectType};
				entry.AddMember("currency_id", currency_cod_points, allocator);
				entry.AddMember("balance_delta", grant, allocator);
				entry.AddMember("balance_before", balance_before, allocator);
				currencies.PushBack(entry, allocator);
				console::demonware("[DW] loot: granted %u CoD Points for match\n", grant);
			}

			response.AddMember("GrantedItems", rapidjson::Value{rapidjson::kArrayType}, allocator);
			response.AddMember("GrantedCurrencies", currencies, allocator);
			return serialize(response);
		}
	}

	void set_settings(const settings& value)
	{
		std::lock_guard lock{settings_mutex};
		current_settings = value;
	}

	std::map<std::uint32_t, std::uint32_t> get_balances()
	{
		const auto config = get_settings();
		const auto today = static_cast<std::int64_t>(time(nullptr) / 86400);
		loot_store::mutate([&](loot_store::state& state)
		{
			if (!config.daily_cod_points || state.last_login_day == today)
			{
				return false;
			}

			state.login_streak = state.last_login_day == today - 1 ? state.login_streak + 1 : 1;
			state.last_login_day = today;
			auto& balance = state.currencies[currency_cod_points];
			balance = add_capped(balance, config.daily_cod_points);
			console::demonware("[DW] loot: daily login reward %u CoD Points (streak %u)\n",
				config.daily_cod_points, state.login_streak);
			return true;
		});

		auto balances = loot_store::get().currencies;
		if (config.infinite_cod_points)
		{
			balances[currency_cod_points] = infinite_cod_points_balance;
		}

		return balances;
	}

	std::optional<purchase_result> purchase(const std::uint32_t sku_id, const std::uint32_t quantity)
	{
		constexpr std::uint32_t max_quantity = 100;

		const auto& skus = get_skus();
		const auto entry = std::find_if(skus.begin(), skus.end(), [&](const sku& value)
		{
			return value.sku_id == sku_id;
		});

		if (entry == skus.end() || !quantity || quantity > max_quantity)
		{
			return std::nullopt;
		}

		const auto infinite = get_settings().infinite_cod_points && entry->currency_id == currency_cod_points;
		const auto cost = static_cast<std::uint64_t>(entry->price) * quantity;
		purchase_result result{entry->currency_id};
		const auto saved = loot_store::mutate([&](loot_store::state& state)
		{
			auto& balance = state.currencies[entry->currency_id];
			if (!infinite)
			{
				if (balance < cost)
				{
					return false;
				}

				balance -= static_cast<std::uint32_t>(cost);
			}

			result.balance = infinite ? infinite_cod_points_balance : balance;

			for (const auto item_id : entry->item_ids)
			{
				auto& owned = state.items[item_id];
				owned = add_capped(owned, quantity);
				result.items[item_id] = owned;
			}

			return true;
		});

		if (!saved)
		{
			console::demonware("[DW] loot: cannot purchase sku %u x%u\n", sku_id, quantity);
			return std::nullopt;
		}

		console::demonware("[DW] loot: purchased sku %u x%u (balance %u)\n", sku_id, quantity, result.balance);
		return result;
	}

	std::optional<std::string> collect_payroll()
	{
		const auto now = static_cast<std::uint64_t>(time(nullptr));
		achievement_record completed{};
		const auto result = achievement_store::mutate(payroll_achievement, [&](achievement_record& record)
		{
			if (record.completion_timestamp && now < record.completion_timestamp + payroll_cooldown)
			{
				return false;
			}

			record.kind = payroll_achievement_kind;
			record.progress = 1;
			record.progress_target = 1;
			record.fulfilled_times = record.completion_timestamp ? record.fulfilled_times + 1 : 1;
			record.completion_timestamp = now;
			record.status = achievement_status::finished;
			completed = record;
			return true;
		});

		if (result != achievement_store::mutation_result::updated)
		{
			console::demonware("[DW] loot: payroll not available\n");
			return std::nullopt;
		}

		std::uint32_t balance_before{};
		loot_store::mutate([&](loot_store::state& state)
		{
			auto& balance = state.currencies[currency_armory_credits];
			balance_before = balance;
			balance = add_capped(balance, payroll_amount);
			return true;
		});

		rapidjson::Document response{};
		response.SetObject();
		auto& allocator = response.GetAllocator();
		response.AddMember("kind", completed.kind, allocator);
		response.AddMember("name", rapidjson::StringRef(payroll_achievement), allocator);
		response.AddMember("requiresClaim", false, allocator);
		response.AddMember("progress", completed.progress, allocator);
		response.AddMember("progressTarget", completed.progress_target, allocator);
		response.AddMember("fulfilledTimes", completed.fulfilled_times, allocator);
		response.AddMember("completionTimestamp", completed.completion_timestamp, allocator);
		response.AddMember("status", rapidjson::StringRef(get_achievement_status_name(completed.status)), allocator);
		response.AddMember("reason", "completed", allocator);
		response.AddMember("type", "ACHIEVEMENT", allocator);

		rapidjson::Value currency{rapidjson::kObjectType};
		currency.AddMember("currency_id", currency_armory_credits, allocator);
		currency.AddMember("balance_delta", payroll_amount, allocator);
		currency.AddMember("balance_before", balance_before, allocator);
		rapidjson::Value currencies{rapidjson::kArrayType};
		currencies.PushBack(currency, allocator);
		rapidjson::Value inventory{rapidjson::kObjectType};
		inventory.AddMember("currencies", currencies, allocator);
		rapidjson::Value trigger{rapidjson::kObjectType};
		trigger.AddMember("type", "GRANT_CURRENCY", allocator);
		trigger.AddMember("inventory", inventory, allocator);
		rapidjson::Value triggers{rapidjson::kArrayType};
		triggers.PushBack(trigger, allocator);
		response.AddMember("triggers", triggers, allocator);

		console::demonware("[DW] loot: payroll granted %u Armory Credits\n", payroll_amount);
		return serialize(response);
	}

	std::vector<sku> get_skus()
	{
		static const std::vector<sku> base_skus
		{
			{1001, "t:MP", currency_cod_points, 200, {2}},
			{1002, "t:ZM", currency_cod_points, 200, {6}},
			{2001, "t:CWL_CWL;l:0x80018B|1", currency_cod_points, 500, {0x6632177, 0x200012F, 0x240042A, 0x7000097, 0x80018B}},
			{2002, "t:CWL_EF;l:0x80017C|1", currency_cod_points, 500, {0x6632175, 0x200010C, 0x240042B, 0x7000098, 0x80017C}},
			{2003, "t:CWL_ENVY;l:0x80017D|1", currency_cod_points, 500, {0x6632181, 0x2000117, 0x240042C, 0x7000099, 0x80017D}},
			{2004, "t:CWL_EPSI;l:0x80017E|1", currency_cod_points, 500, {0x6632176, 0x200010D, 0x240042D, 0x700009A, 0x80017E}},
			{2005, "t:CWL_EU;l:0x80017F|1", currency_cod_points, 500, {0x6632178, 0x200010E, 0x240042E, 0x700009B, 0x80017F}},
			{2006, "t:CWL_EVIL;l:0x800180|1", currency_cod_points, 500, {0x6632179, 0x200010F, 0x240042F, 0x700009C, 0x800180}},
			{2007, "t:CWL_FAZE;l:0x800181|1", currency_cod_points, 500, {0x663217A, 0x2000110, 0x2400430, 0x700009D, 0x800181}},
			{2008, "t:CWL_LUMI;l:0x800182|1", currency_cod_points, 500, {0x663217B, 0x2000111, 0x2400431, 0x700009F, 0x800182}},
			{2009, "t:CWL_MIND;l:0x800183|1", currency_cod_points, 500, {0x663217C, 0x2000112, 0x2400432, 0x70000A0, 0x800183}},
			{2010, "t:CWL_OPT;l:0x800184|1", currency_cod_points, 500, {0x663217D, 0x2000113, 0x2400433, 0x70000A1, 0x800184}},
			{2011, "t:CWL_RED;l:0x800185|1", currency_cod_points, 500, {0x663217E, 0x2000114, 0x2400434, 0x70000A2, 0x800185}},
			{2012, "t:CWL_RISE;l:0x800186|1", currency_cod_points, 500, {0x663217F, 0x2000115, 0x2400435, 0x70000A3, 0x800186}},
			{2013, "t:CWL_SPLY;l:0x800187|1", currency_cod_points, 500, {0x6632180, 0x2000116, 0x2400436, 0x70000A4, 0x800187}},
			{2014, "t:CWL_UNI;l:0x800188|1", currency_cod_points, 500, {0x6632184, 0x200011A, 0x2400437, 0x70000A5, 0x800188}},
			{2015, "t:CWL_VITA;l:0x800189|1", currency_cod_points, 500, {0x6632183, 0x2000119, 0x2400439, 0x70000A6, 0x800189}},
			{2016, "t:CWL_KALI;l:0x80018A|1", currency_cod_points, 500, {0x6632182, 0x2000118, 0x2400438, 0x700009E, 0x80018A}},
		};

		auto skus = base_skus;
		for (const auto& contract : challenge_service::get_contract_skus())
		{
			skus.push_back({contract.sku_id, contract.sku_data, currency_armory_credits, contract.price, {contract.item_id}});
		}

		return skus;
	}

	std::optional<std::string> handle_action(const std::string_view action, const std::string_view client_transaction,
		const rapidjson::Value& request)
	{
		if (action == "open_supply_drop")
		{
			return open_supply_drop(action, client_transaction, request);
		}

		if (action == "start_mission")
		{
			return start_mission(action, client_transaction);
		}

		if (action == "end_mission")
		{
			return end_mission(action, client_transaction);
		}

		if (action == "reset_missions")
		{
			return serialize(make_response(action, client_transaction, true));
		}

		return std::nullopt;
	}
}
