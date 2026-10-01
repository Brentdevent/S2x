#include <std_include.hpp>

#include "loot_service.hpp"
#include "loot_catalog.hpp"
#include "loot_store.hpp"

#include "component/console/console.hpp"

#include <mutex>
#include <random>

namespace demonware::loot_service
{
	namespace
	{
		constexpr std::size_t supply_drop_item_count = 3;
		constexpr std::uint8_t currency_cod_points = 2;
		constexpr std::uint32_t infinite_cod_points_balance = 999999;

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
			const auto items = drop ? loot_catalog::roll_drop(*drop, supply_drop_item_count) : std::vector<std::uint32_t>{};
			if (items.empty())
			{
				console::demonware("[DW] loot: cannot open supply drop '%.*s'\n",
					static_cast<int>(name.size()), name.data());
				return serialize(make_response(action, client_transaction, false));
			}

			std::map<std::uint32_t, std::uint32_t> changes{};
			const auto saved = loot_store::mutate([&](loot_store::state& state)
			{
				if (auto& owned = state.items[drop->guid])
				{
					changes[drop->guid] = --owned;
				}

				for (const auto guid : items)
				{
					changes[guid] = ++state.items[guid];
				}

				return true;
			});

			if (!saved)
			{
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

	const std::vector<sku>& get_skus()
	{
		static const std::vector<sku> skus
		{
			{1001, "t:MP", currency_cod_points, 200, 2},
			{1002, "t:ZM", currency_cod_points, 200, 6},
		};

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
