#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace demonware::loot_service
{
	struct sku
	{
		std::uint32_t sku_id;
		const char* sku_data;
		std::uint8_t currency_id;
		std::uint32_t price;
		std::uint32_t item_id;
	};

	struct settings
	{
		bool infinite_cod_points{};
		std::uint32_t daily_cod_points{};
		std::uint32_t match_cod_points{};
	};

	const std::vector<sku>& get_skus();
	void set_settings(const settings& value);
	std::map<std::uint32_t, std::uint32_t> get_balances();

	std::optional<std::string> handle_action(std::string_view action, std::string_view client_transaction,
		const rapidjson::Value& request);
}
