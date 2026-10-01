#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace demonware::loot_service
{
	struct sku
	{
		std::uint32_t sku_id;
		std::string sku_data;
		std::uint8_t currency_id;
		std::uint32_t price;
		std::vector<std::uint32_t> item_ids;
	};

	struct settings
	{
		bool infinite_cod_points{};
		std::uint32_t daily_cod_points{};
		std::uint32_t match_cod_points{};
	};

	std::vector<sku> get_skus();
	void set_settings(const settings& value);
	std::map<std::uint32_t, std::uint32_t> get_balances();

	struct purchase_result
	{
		std::uint8_t currency_id;
		std::uint32_t balance;
		std::map<std::uint32_t, std::uint32_t> items;
	};

	std::optional<purchase_result> purchase(std::uint32_t sku_id, std::uint32_t quantity);
	std::optional<std::string> collect_payroll();

	std::optional<std::string> handle_action(std::string_view action, std::string_view client_transaction,
		const rapidjson::Value& request);
}
