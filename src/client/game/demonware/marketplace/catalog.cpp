#include <std_include.hpp>

#include "catalog.hpp"
#include "cwl.hpp"

#include "resource.hpp"

#include "game/types/demonware.hpp"

#include "pagination.hpp"

#include <utils/cryptography.hpp>
#include <utils/nt.hpp>

#include <rapidjson/document.h>

#include <array>
#include <cctype>
#include <limits>
#include <mutex>
#include <unordered_set>

namespace demonware::marketplace_catalog
{
	struct catalog_builder
	{
		catalog value{};

		void reserve_skus(const std::size_t count)
		{
			value.skus_.reserve(count);
		}

		void add_sku(sku_record record)
		{
			const auto index = value.skus_.size();
			value.skus_by_type_[record.fields.sku_type].push_back(index);
			value.skus_.emplace_back(std::move(record));
		}

		std::size_t sku_count(const std::uint8_t type) const
		{
			const auto entry = value.skus_by_type_.find(type);
			return entry == value.skus_by_type_.end() ? 0 : entry->second.size();
		}

		void reserve_products(const std::size_t count)
		{
			value.products_.reserve(count);
			value.products_by_id_.reserve(count);
		}

		bool contains_product(const std::uint32_t id) const
		{
			return value.products_by_id_.contains(id);
		}

		void add_product(product_record record)
		{
			value.products_by_id_.emplace(record.fields.product_id,
				value.products_.size());
			value.products_.emplace_back(std::move(record));
		}
	};

	namespace
	{
		constexpr std::string_view sku_magic{"S2XSKU1\0", 8};
		constexpr std::string_view product_magic{"S2XPRD1\0", 8};

		struct manifest_resource
		{
			std::uint32_t record_count{};
			std::uint32_t type_100_count{};
			std::uint32_t type_150_count{};
			std::string resource_hash{};
			std::string records_hash{};
		};

		struct parsed_manifest
		{
			manifest_resource skus{};
			manifest_resource products{};
		};

		class binary_reader final
		{
		public:
			explicit binary_reader(const std::string_view bytes) : bytes_(bytes)
			{
			}

			bool read_magic(const std::string_view expected)
			{
				if (expected.size() > remaining() ||
					bytes_.substr(offset_, expected.size()) != expected)
				{
					return false;
				}

				offset_ += expected.size();
				return true;
			}

			bool read_u32(std::uint32_t& value)
			{
				if (remaining() < sizeof(std::uint32_t))
				{
					return false;
				}

				const auto* data = reinterpret_cast<const unsigned char*>(
					bytes_.data() + offset_);
				value = static_cast<std::uint32_t>(data[0]) |
					(static_cast<std::uint32_t>(data[1]) << 8) |
					(static_cast<std::uint32_t>(data[2]) << 16) |
					(static_cast<std::uint32_t>(data[3]) << 24);
				offset_ += sizeof(std::uint32_t);
				return true;
			}

			bool read_bytes(const std::size_t length, std::string& value)
			{
				if (length > remaining())
				{
					return false;
				}

				value.assign(bytes_.data() + offset_, length);
				offset_ += length;
				return true;
			}

			std::size_t remaining() const
			{
				return offset_ <= bytes_.size() ? bytes_.size() - offset_ : 0;
			}

		private:
			std::string_view bytes_{};
			std::size_t offset_{};
		};

		bool is_hash(const std::string_view value)
		{
			return value.size() == 64 && std::ranges::all_of(value, [](const char character)
			{
				return std::isxdigit(static_cast<unsigned char>(character)) != 0;
			});
		}

		std::string uppercase_hash(std::string value)
		{
			std::ranges::transform(value, value.begin(), [](const char character)
			{
				return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
			});
			return value;
		}

		const rapidjson::Value* unique_member(const rapidjson::Value& object,
			const char* name)
		{
			const rapidjson::Value* result = nullptr;
			for (auto entry = object.MemberBegin(); entry != object.MemberEnd(); ++entry)
			{
				if (entry->name.IsString() && entry->name.GetStringLength() == std::strlen(name) &&
					std::memcmp(entry->name.GetString(), name, entry->name.GetStringLength()) == 0)
				{
					if (result)
					{
						return nullptr;
					}

					result = &entry->value;
				}
			}

			return result;
		}

		bool parse_manifest_resource(const rapidjson::Value& value,
			const bool sku, manifest_resource& output)
		{
			if (!value.IsObject())
			{
				return false;
			}

			const auto* record_count = unique_member(value, "record_count");
			const auto* resource_hash = unique_member(value, "sha256");
			const auto* records_hash = unique_member(value, "records_sha256");
			if (!record_count || !record_count->IsUint() || !resource_hash ||
				!resource_hash->IsString() || !records_hash || !records_hash->IsString())
			{
				return false;
			}

			manifest_resource parsed{};
			parsed.record_count = record_count->GetUint();
			parsed.resource_hash.assign(resource_hash->GetString(),
				resource_hash->GetStringLength());
			parsed.records_hash.assign(records_hash->GetString(),
				records_hash->GetStringLength());
			if (!is_hash(parsed.resource_hash) || !is_hash(parsed.records_hash))
			{
				return false;
			}

			parsed.resource_hash = uppercase_hash(std::move(parsed.resource_hash));
			parsed.records_hash = uppercase_hash(std::move(parsed.records_hash));

			if (sku)
			{
				const auto* type_100_count = unique_member(value, "type_100_count");
				const auto* type_150_count = unique_member(value, "type_150_count");
				if (!type_100_count || !type_100_count->IsUint() ||
					!type_150_count || !type_150_count->IsUint())
				{
					return false;
				}

				parsed.type_100_count = type_100_count->GetUint();
				parsed.type_150_count = type_150_count->GetUint();
			}

			output = std::move(parsed);
			return true;
		}

		bool parse_manifest(const std::string_view json, parsed_manifest& output)
		{
			if (json.empty() || json.size() > maximum_manifest_size)
			{
				return false;
			}

			rapidjson::Document document{};
			document.Parse(json.data(), json.size());
			if (document.HasParseError() || !document.IsObject())
			{
				return false;
			}

			const auto* schema = unique_member(document, "schema");
			const auto* sku_resource = unique_member(document, "sku_resource");
			const auto* product_resource = unique_member(document, "product_resource");
			parsed_manifest parsed{};
			if (!schema || !schema->IsUint() || schema->GetUint() != manifest_schema_version ||
				!sku_resource || !product_resource ||
				!parse_manifest_resource(*sku_resource, true, parsed.skus) ||
				!parse_manifest_resource(*product_resource, false, parsed.products) ||
				parsed.skus.record_count != captured_sku_count ||
				parsed.skus.type_100_count != captured_type_100_count ||
				parsed.skus.type_150_count != captured_type_150_count ||
				parsed.products.record_count != captured_product_count)
			{
				return false;
			}

			output = std::move(parsed);
			return true;
		}

		bool parse_skus(const std::string_view resource, const manifest_resource& manifest,
			catalog_builder& builder, std::string& concatenated, std::string& error)
		{
			if (resource.empty() || resource.size() > maximum_sku_resource_size ||
				utils::cryptography::sha256::compute(std::string{resource}, true) !=
				manifest.resource_hash)
			{
				error = "SKU resource size or hash mismatch";
				return false;
			}

			binary_reader reader{resource};
			std::uint32_t version{};
			std::uint32_t record_count{};
			if (!reader.read_magic(sku_magic) || !reader.read_u32(version) ||
				version != resource_format_version || !reader.read_u32(record_count) ||
				record_count != manifest.record_count)
			{
				error = "SKU resource header is invalid";
				return false;
			}

			builder.reserve_skus(record_count);
			std::unordered_set<std::uint32_t> ids{};
			ids.reserve(record_count);
			concatenated.clear();
			concatenated.reserve(resource.size());
			for (std::uint32_t index = 0; index < record_count; ++index)
			{
				std::uint32_t declared_id{};
				std::uint32_t raw_length{};
				sku_record record{};
				if (!reader.read_u32(declared_id) || !reader.read_u32(raw_length) ||
					raw_length == 0 || raw_length > maximum_sku_record_size ||
					!reader.read_bytes(raw_length, record.raw) ||
					!marketplace_sku::parse_result(record.raw, record.fields) ||
					record.fields.sku_id != declared_id || !ids.emplace(declared_id).second ||
					(record.fields.sku_type != marketplace_sku::quartermaster_sku_type &&
						record.fields.sku_type != marketplace_sku::collection_sku_type))
				{
					error = "SKU resource contains an invalid or duplicate record";
					return false;
				}

				concatenated.append(record.raw);
				builder.add_sku(std::move(record));
			}

			if (reader.remaining() != 0 ||
				builder.sku_count(marketplace_sku::quartermaster_sku_type) !=
					manifest.type_100_count ||
				builder.sku_count(marketplace_sku::collection_sku_type) !=
					manifest.type_150_count ||
				utils::cryptography::sha256::compute(concatenated, true) !=
					manifest.records_hash)
			{
				error = "SKU resource counts, boundary, or aggregate hash mismatch";
				return false;
			}

			return true;
		}

		bool parse_products(const std::string_view resource,
			const manifest_resource& manifest, catalog_builder& builder,
			std::string& concatenated, std::string& error)
		{
			if (resource.empty() || resource.size() > maximum_product_resource_size ||
				utils::cryptography::sha256::compute(std::string{resource}, true) !=
				manifest.resource_hash)
			{
				error = "product resource size or hash mismatch";
				return false;
			}

			binary_reader reader{resource};
			std::uint32_t version{};
			std::uint32_t record_count{};
			if (!reader.read_magic(product_magic) || !reader.read_u32(version) ||
				version != resource_format_version || !reader.read_u32(record_count) ||
				record_count != manifest.record_count)
			{
				error = "product resource header is invalid";
				return false;
			}

			builder.reserve_products(record_count);
			concatenated.clear();
			concatenated.reserve(resource.size());
			for (std::uint32_t index = 0; index < record_count; ++index)
			{
				std::uint32_t declared_id{};
				std::uint32_t raw_length{};
				product_record record{};
				if (!reader.read_u32(declared_id) || !reader.read_u32(raw_length) ||
					raw_length == 0 || raw_length > maximum_product_record_size ||
					!reader.read_bytes(raw_length, record.raw) ||
					!marketplace_product::parse_result(record.raw, record.fields) ||
					record.fields.product_id != declared_id ||
					builder.contains_product(declared_id))
				{
					error = "product resource contains an invalid or duplicate record";
					return false;
				}

				concatenated.append(record.raw);
				builder.add_product(std::move(record));
			}

			if (reader.remaining() != 0 ||
				utils::cryptography::sha256::compute(concatenated, true) !=
					manifest.records_hash)
			{
				error = "product resource boundary or aggregate hash mismatch";
				return false;
			}

			return true;
		}

		load_result load_embedded_resources()
		{

			const auto manifest = utils::nt::load_resource(DW_MARKETPLACE_CATALOG_MANIFEST);
			const auto skus = utils::nt::load_resource(DW_MARKETPLACE_SKUS);
			const auto products = utils::nt::load_resource(DW_MARKETPLACE_PRODUCTS);
			return load(manifest, skus, products);
		}

		std::once_flag embedded_once{};
		load_result embedded_result{};
	}

	const std::vector<sku_record>& catalog::skus() const
	{
		return skus_;
	}

	const std::vector<product_record>& catalog::products() const
	{
		return products_;
	}

	const std::vector<std::size_t>* catalog::find_skus(const std::uint8_t sku_type) const
	{
		const auto entry = skus_by_type_.find(sku_type);
		return entry == skus_by_type_.end() ? nullptr : &entry->second;
	}

	const product_record* catalog::find_product(const std::uint32_t product_id) const
	{
		const auto entry = products_by_id_.find(product_id);
		return entry == products_by_id_.end() ? nullptr : &products_[entry->second];
	}

	raw_task_result::raw_task_result(const std::string_view raw) : raw_(raw)
	{
	}

	void raw_task_result::serialize(byte_buffer* buffer)
	{
		if (buffer)
		{
			buffer->write(raw_);
		}
	}

	load_result load(const std::string_view manifest, const std::string_view sku_resource,
		const std::string_view product_resource)
	{
		if (manifest.empty() && sku_resource.empty() && product_resource.empty())
		{
			return {load_status::missing, nullptr, "captured catalog resources are absent"};
		}

		if (manifest.empty() || sku_resource.empty() || product_resource.empty())
		{
			return {load_status::invalid, nullptr,
				"captured catalog resource set is incomplete"};
		}

		parsed_manifest parsed{};
		if (!parse_manifest(manifest, parsed))
		{
			return {load_status::invalid, nullptr,
				"captured catalog manifest is invalid or unsupported"};
		}

		catalog_builder builder{};
		std::string concatenated{};
		std::string error{};
		if (!parse_skus(sku_resource, parsed.skus, builder, concatenated, error) ||
			!parse_products(product_resource, parsed.products, builder, concatenated, error))
		{
			return {load_status::invalid, nullptr, std::move(error)};
		}

		const auto* type_100 = builder.value.find_skus(
			marketplace_sku::quartermaster_sku_type);
		if (!type_100)
		{
			return {load_status::invalid, nullptr,
				"captured catalog has no type-100 SKU index"};
		}

		for (const auto index : *type_100)
		{
			if (index >= builder.value.skus().size() ||
				!builder.value.find_product(builder.value.skus()[index].fields.field_36))
			{
				return {load_status::invalid, nullptr,
					"captured type-100 SKU has no captured product"};
			}
		}

		// Local offline prices: no SKU for these captured products exists in our
		// retail captures. Use the observed Zombies contract range (200-325 AC):
		// opening all doors is the lower tier; reaching wave 20 in 30 minutes is
		// the upper tier. MP kill contracts use the captured 65-kill TDM price
		// (100 AC). IDs/tokens come from captured products and the native
		// periodic challenge table. Keep additions separate from captured bytes;
		// a recovered SKU for the product takes precedence over this policy.
		struct local_contract { std::uint32_t product, token, challenge, price; };
		static constexpr local_contract local_contracts[] = {
			{1073742138, 83886241, 1109, 200},
			{1073742140, 83886243, 1111, 325},
			{1073742105, 83886227, 724, 100},
			{1073742032, 83886210, 613, 100},
		};
		for (const auto& entry : local_contracts)
		{
			const auto* product = builder.value.find_product(entry.product);
			if (!product || product->fields.items.size() != 1 ||
				product->fields.items[0].first != entry.token || product->fields.items[0].second != 1 ||
				std::ranges::any_of(builder.value.skus(), [&](const sku_record& sku)
				{ return sku.fields.sku_id == entry.product || sku.fields.field_36 == entry.product; }))
			{
				continue;
			}

			sku_record sku{};
			sku.fields.sku_id = entry.product;
			sku.fields.field_36 = entry.product;
			sku.fields.sku_data = "c:" + std::to_string(entry.challenge);
			sku.fields.prices.push_back({6, entry.price});
			byte_buffer wire;
			sku.fields.serialize(&wire);
			sku.raw = wire.get_buffer();
			builder.add_sku(std::move(sku));
		}

		for (const auto& pack : marketplace_cwl::packs)
		{
			const auto id = pack.items[0];
			if (builder.contains_product(id) || std::ranges::any_of(builder.value.skus(), [id](const sku_record& sku) { return sku.fields.sku_id == id; }))
			{
				return {load_status::invalid, {}, "CWL policy identity collision"};
			}

			sku_record sku;
			sku.fields.sku_id = sku.fields.field_36 = id;
			char limiter[16]{};
			snprintf(limiter, sizeof(limiter), "0x%X", id);
			sku.fields.sku_data = "t:" + std::string{pack.tag} + ";l:" + limiter + "|1";
			sku.fields.promotional_text = std::string{pack.name} + ";5 CWL cosmetics";
			sku.fields.prices.push_back({6, marketplace_cwl::price});
			byte_buffer sku_wire;
			sku.fields.serialize(&sku_wire);
			sku.raw = sku_wire.get_buffer();
			builder.add_sku(std::move(sku));
			product_record product;
			product.fields.product_id = id;
			for (const auto item : pack.items)
			{
				product.fields.items.push_back({item, 1});
			}

			byte_buffer product_wire;
			product.fields.serialize(&product_wire);
			product.raw = product_wire.get_buffer();
			builder.add_product(std::move(product));
		}

		auto value = std::make_shared<catalog>(std::move(builder.value));
		return {load_status::ready, std::move(value), {}};
	}

	sku_selection select_sku_page(const catalog& value, const std::uint8_t sku_type,
		const std::uint32_t page, const std::uint32_t page_size)
	{
		if (page_size != marketplace_sku::stock_page_size)
		{
			return {selection_status::invalid_request, {}};
		}

		const auto* indices = value.find_skus(sku_type);
		const auto range = marketplace_pagination::get_page_range(
			indices ? indices->size() : 0, page_size, page);
		if (!range)
		{
			return {selection_status::invalid_request, {}};
		}

		if (!indices)
		{
			return {selection_status::success, {}};
		}

		sku_selection result{selection_status::success, {}};
		result.records.reserve(range->last - range->first);
		for (auto position = range->first; position < range->last; ++position)
		{
			const auto record_index = (*indices)[position];
			if (record_index >= value.skus().size())
			{
				return {selection_status::invalid_catalog, {}};
			}

			result.records.push_back(&value.skus()[record_index]);
		}

		return result;
	}

	product_selection select_products(const catalog& value,
		const std::span<const std::uint32_t> product_ids)
	{
		if (product_ids.empty() ||
			product_ids.size() > marketplace_product::maximum_product_results)
		{
			return {selection_status::invalid_request, 0, {}};
		}

		std::unordered_set<std::uint32_t> unique_ids{};
		unique_ids.reserve(product_ids.size());
		product_selection result{selection_status::success, 0, {}};
		result.records.reserve(product_ids.size());
		for (const auto product_id : product_ids)
		{
			if (product_id == 0 || !unique_ids.emplace(product_id).second)
			{
				return {selection_status::invalid_request, product_id, {}};
			}

			const auto* record = value.find_product(product_id);
			if (!record)
			{
				return {selection_status::record_not_found, product_id, {}};
			}

			result.records.push_back(record);
		}

		return result;
	}

	std::uint32_t task_error(const task_handler_status status)
	{
		switch (status)
		{
		case task_handler_status::success:
			return 0;
		case task_handler_status::parameter_error:
			return game::demonware::BD_PARAM_PARSE_ERROR;
		case task_handler_status::service_unavailable:
			return game::demonware::BD_SERVICE_NOT_AVAILABLE;
		case task_handler_status::marketplace_error:
		default:
			return game::demonware::BD_MARKETPLACE_ERROR;
		}
	}

	sku_task_result handle_task111(byte_buffer* buffer, const load_result& loaded)
	{
		sku_task_result result{};
		if (!marketplace_sku::parse_request(buffer, result.request))
		{
			return result;
		}

		if (loaded.status != load_status::ready || !loaded.value)
		{
			result.status = task_handler_status::marketplace_error;
			return result;
		}

		const auto selected = select_sku_page(*loaded.value, result.request.sku_type,
			result.request.page, result.request.page_size);
		if (selected.status == selection_status::invalid_request)
		{
			result.status = task_handler_status::parameter_error;
			return result;
		}

		if (selected.status != selection_status::success)
		{
			result.status = task_handler_status::marketplace_error;
			return result;
		}

		result.unsupported_type = loaded.value->find_skus(result.request.sku_type) == nullptr;
		result.records.reserve(selected.records.size());
		for (const auto* record : selected.records)
		{
			if (!record)
			{
				result.status = task_handler_status::marketplace_error;
				result.records.clear();
				return result;
			}

			result.records.emplace_back(record->raw);
		}

		result.status = task_handler_status::success;
		return result;
	}

	product_task_result handle_task99(byte_buffer* buffer, const load_result& loaded)
	{
		product_task_result result{};
		if (!marketplace_product::parse_request(buffer, result.request))
		{
			return result;
		}

		if (loaded.status != load_status::ready || !loaded.value)
		{
			result.status = task_handler_status::marketplace_error;
			return result;
		}

		const auto selected = select_products(*loaded.value, result.request.product_ids);
		result.rejected_id = selected.rejected_id;
		if (selected.status == selection_status::record_not_found)
		{
			result.status = task_handler_status::service_unavailable;
			return result;
		}

		if (selected.status == selection_status::invalid_request)
		{
			result.status = task_handler_status::parameter_error;
			return result;
		}

		if (selected.status != selection_status::success)
		{
			result.status = task_handler_status::marketplace_error;
			return result;
		}

		result.records.reserve(selected.records.size());
		for (const auto* record : selected.records)
		{
			if (!record)
			{
				result.status = task_handler_status::marketplace_error;
				result.records.clear();
				return result;
			}

			result.records.emplace_back(record->raw);
		}

		result.status = task_handler_status::success;
		return result;
	}

	void initialize_embedded()
	{
		std::call_once(embedded_once, []
		{
			embedded_result = load_embedded_resources();
		});
	}

	const load_result& get_embedded()
	{
		initialize_embedded();
		return embedded_result;
	}
}
