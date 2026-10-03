#pragma once

#include "byte_buffer.hpp"
#include "data_types.hpp"
#include "marketplace_product.hpp"
#include "marketplace_sku.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace demonware::marketplace_catalog
{
	inline constexpr std::uint32_t resource_format_version = 1;
	inline constexpr std::uint32_t manifest_schema_version = 1;
	inline constexpr std::size_t captured_sku_count = 1024;
	inline constexpr std::size_t captured_type_100_count = 141;
	inline constexpr std::size_t captured_type_150_count = 883;
	inline constexpr std::size_t captured_product_count = 330;
	inline constexpr std::size_t maximum_manifest_size = 64 * 1024;
	inline constexpr std::size_t maximum_sku_resource_size = 4 * 1024 * 1024;
	inline constexpr std::size_t maximum_product_resource_size = 8 * 1024 * 1024;
	inline constexpr std::size_t maximum_sku_record_size = 4096;
	inline constexpr std::size_t maximum_product_record_size = 16384;

	enum class load_status
	{
		ready,
		missing,
		invalid,
	};

	struct sku_record
	{
		marketplace_sku::result fields{};
		std::string raw{};
	};

	struct product_record
	{
		marketplace_product::result fields{};
		std::string raw{};
	};

	class catalog final
	{
	public:
		const std::vector<sku_record>& skus() const;
		const std::vector<product_record>& products() const;
		const std::vector<std::size_t>* find_skus(std::uint8_t sku_type) const;
		const product_record* find_product(std::uint32_t product_id) const;

	private:
		friend struct catalog_builder;

		std::vector<sku_record> skus_{};
		std::vector<product_record> products_{};
		std::unordered_map<std::uint8_t, std::vector<std::size_t>> skus_by_type_{};
		std::unordered_map<std::uint32_t, std::size_t> products_by_id_{};
	};

	struct load_result
	{
		load_status status{load_status::invalid};
		std::shared_ptr<const catalog> value{};
		std::string error{};
	};

	enum class selection_status
	{
		success,
		invalid_request,
		record_not_found,
		invalid_catalog,
	};

	struct sku_selection
	{
		selection_status status{selection_status::invalid_request};
		std::vector<const sku_record*> records{};
	};

	struct product_selection
	{
		selection_status status{selection_status::invalid_request};
		std::uint32_t rejected_id{};
		std::vector<const product_record*> records{};
	};

	enum class task_handler_status
	{
		success,
		parameter_error,
		service_unavailable,
		marketplace_error,
	};

	struct sku_task_result
	{
		task_handler_status status{task_handler_status::parameter_error};
		marketplace_sku::request request{};
		std::vector<std::string> records{};
		bool unsupported_type{};
	};

	struct product_task_result
	{
		task_handler_status status{task_handler_status::parameter_error};
		marketplace_product::request request{};
		std::vector<std::string> records{};
		std::uint32_t rejected_id{};
	};

	class raw_task_result final : public bdTaskResult
	{
	public:
		explicit raw_task_result(std::string_view raw);
		void serialize(byte_buffer* buffer) override;

	private:
		std::string raw_{};
	};

	load_result load(std::string_view manifest, std::string_view sku_resource,
		std::string_view product_resource);
	sku_selection select_sku_page(const catalog& value, std::uint8_t sku_type,
		std::uint32_t page, std::uint32_t page_size);
	product_selection select_products(const catalog& value,
		std::span<const std::uint32_t> product_ids);
	std::uint32_t task_error(task_handler_status status);
	sku_task_result handle_task111(byte_buffer* buffer, const load_result& loaded);
	product_task_result handle_task99(byte_buffer* buffer, const load_result& loaded);

	void initialize_embedded();
	const load_result& get_embedded();
}
