#include <std_include.hpp>
#include "../dw_include.hpp"
#include "game/demonware/marketplace_inventory.hpp"
#include "game/demonware/marketplace_purchase.hpp"
#include "game/demonware/marketplace_collection.hpp"
#include "game/demonware/marketplace_pawn.hpp"
#include "game/demonware/promotional_vouchers.hpp"
#include "game/demonware/marketplace_catalog.hpp"
#include "game/demonware/marketplace_queries.hpp"
#include "game/demonware/runtime_context.hpp"
#include "game/game.hpp"

namespace demonware
{
	namespace
	{
		void send_query(service_server* server, const std::uint8_t task,
			marketplace_queries::result response)
		{
			auto reply = server->create_reply(task, response.error);
			if (!response.error)
				for (auto& record : response.records) reply.add(record);
			reply.send();
		}

		template <typename Result>
		void send_catalog(service_server* server, const std::uint8_t task, const Result& response)
		{
			const auto error = marketplace_catalog::task_error(response.status);
			auto reply = server->create_reply(task, error);
			if (!error)
			{
				for (const auto& raw : response.records)
				{
					auto record = std::make_unique<marketplace_catalog::raw_task_result>(raw);
					reply.add(record);
				}
			}
			reply.send();
		}
	}

	bdMarketplace::bdMarketplace() : service(80, "bdMarketplace")
	{
		this->register_task(42, &bdMarketplace::startExchangeTransaction);
		this->register_task(43, &bdMarketplace::purchaseOnSteamInitialize);
		this->register_task(44, &bdMarketplace::purchaseOnSteamFinalize);
		this->register_task(49, &bdMarketplace::getExpiredInventoryItems);
		this->register_task(50, &bdMarketplace::deleteInventoryItems);
		this->register_task(58, &bdMarketplace::validateInventoryItemsToken);
		this->register_task(60, &bdMarketplace::steamProcessDurable);
		this->register_task(85, &bdMarketplace::steamProcessDurableV2);
		this->register_task(96, &bdMarketplace::consumeInventoryItems);
		this->register_task(99, &bdMarketplace::getProducts);
		this->register_task(106, &bdMarketplace::purchaseSkus);
		this->register_task(111, &bdMarketplace::getSkusPaginated);
		this->register_task(123, &bdMarketplace::purchaseSkusV3);
		this->register_task(130, &bdMarketplace::getBalance);
		this->register_task(132, &bdMarketplace::getBalanceV2);
		this->register_task(165, &bdMarketplace::getInventoryPaginated);
		this->register_task(168, &bdMarketplace::putInventoryItemsData);
		this->register_task(193, &bdMarketplace::putPlayersInventoryItems);
		this->register_task(199, &bdMarketplace::pawnItems);
		this->register_task(232, &bdMarketplace::getEntitlements);
		this->register_task(242, &bdMarketplace::applyConversionRule);
	}

	void bdMarketplace::startExchangeTransaction(service_server* server, byte_buffer* buffer) const
	{

		server->create_reply(this->task_id(), game::demonware::BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::purchaseOnSteamInitialize(service_server* server, byte_buffer* buffer) const
	{

		server->create_reply(this->task_id(), game::demonware::BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::purchaseOnSteamFinalize(service_server* server, byte_buffer* buffer) const
	{

		server->create_reply(this->task_id(), game::demonware::BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::steamProcessDurable(service_server* server, byte_buffer* buffer) const
	{

		server->create_reply(this->task_id(), game::demonware::BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::steamProcessDurableV2(service_server* server, byte_buffer* buffer) const
	{

		server->create_reply(this->task_id(), game::demonware::BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::consumeInventoryItems(service_server* server, byte_buffer* buffer) const
	{
		const auto identity = runtime_context::get_snapshot();
		const auto user_id = game::environment::is_dedicated() || !identity ? 0 : identity->user_id;
		std::vector<std::string> updates;
		const auto error = marketplace_inventory::consume(buffer, user_id, updates);
		server->create_reply(this->task_id(), error).send();
		if (!error)
		{
			for (const auto& bytes : updates)
			{
				byte_buffer push{bytes};
				server->create_message(BD_LOBBY_SERVICE_PUSH_MESSAGE).send(&push, true);
			}
		}
	}

	void bdMarketplace::purchaseSkus(service_server* server, byte_buffer* buffer) const
	{

		server->create_reply(this->task_id(), game::demonware::BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::purchaseSkusV3(service_server* server, byte_buffer* buffer) const
	{

		marketplace_purchase::request input;
		if (!marketplace_purchase::parse_request(buffer, input))
		{
			server->create_reply(this->task_id(), BD_MARKETPLACE_INVALID_PARAMETER).send();
			return;
		}
		const auto identity = runtime_context::get_snapshot();
		const auto user_id = game::environment::is_dedicated() || !identity ? 0 : identity->user_id;
		const auto& catalog = marketplace_catalog::get_embedded();
		const marketplace_sku::result* sku{};
		const marketplace_product::result* product{};
		if (catalog.value)
		{
			for (const auto& entry : catalog.value->skus())
				if (entry.fields.sku_id == input.sku_id &&
					(entry.fields.sku_type == marketplace_sku::quartermaster_sku_type ||
						entry.fields.sku_type == marketplace_sku::collection_sku_type)) { sku = &entry.fields; break; }
			const auto found = sku ? catalog.value->find_product(sku->field_36) : nullptr;
			if (found) product = &found->fields;
		}
		const auto collections = collection_catalog::get_snapshot();
		auto response = std::make_unique<marketplace_purchase::result>(
			marketplace_purchase::purchase(input, user_id, sku, product, collections.get()));
		auto reply = server->create_reply(this->task_id(), response->error);
		if (!response->error) reply.add(response);
		reply.send();
	}

	void bdMarketplace::putPlayersInventoryItems(service_server* server, byte_buffer* buffer) const
	{

		server->create_reply(this->task_id(), game::demonware::BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::putInventoryItemsData(service_server* server, byte_buffer* buffer) const
	{

		const auto identity = runtime_context::get_snapshot();
		const auto user_id = game::environment::is_dedicated() || !identity ? 0 : identity->user_id;
		server->create_reply(this->task_id(), marketplace_inventory::put_item_data(buffer, user_id)).send();
	}

	void bdMarketplace::pawnItems(service_server* server, byte_buffer* buffer) const
	{

		const auto identity = runtime_context::get_snapshot();
		const auto user_id = game::environment::is_dedicated() || !identity ? 0 : identity->user_id;
		const auto catalog = pawn_catalog::get_snapshot();
		std::vector<std::string> updates;
		const auto error = marketplace_pawn::pawn(buffer, user_id, catalog.get(), updates);
		server->create_reply(this->task_id(), error).send();
		if (!error)
			for (const auto& bytes : updates)
			{
				byte_buffer push{bytes};
				server->create_message(BD_LOBBY_SERVICE_PUSH_MESSAGE).send(&push, true);
			}
	}

	void bdMarketplace::applyConversionRule(service_server* server, byte_buffer* buffer) const
	{

		marketplace_collection::request input;
		if (!marketplace_collection::parse_request(buffer, input))
		{
			server->create_reply(this->task_id(), BD_MARKETPLACE_INVALID_PARAMETER).send();
			return;
		}
		const auto identity = runtime_context::get_snapshot();
		const auto user_id = game::environment::is_dedicated() || !identity ? 0 : identity->user_id;
		if (auto voucher = promotional_vouchers::redeem(input, user_id))
		{
			auto response = std::make_unique<marketplace_collection::result>(std::move(*voucher));
			auto reply = server->create_reply(this->task_id(), response->error);
			if (!response->error) reply.add(response);
			reply.send_struct();
			return;
		}
		const auto catalog = collection_catalog::get_snapshot();
		const auto pawns = pawn_catalog::get_snapshot();
		// A committed conversion determines its handler even while the native
		// catalog is invalidated during a map/menu transition. Each handler still
		// validates the complete fingerprint before projecting current balances.
		const auto receipt = marketplace_store::find_transaction("marketplace:242:" + input.client_tx);
		const auto uniform = receipt ? receipt->request_fingerprint.starts_with("pawn:") :
			pawns && std::ranges::any_of(pawns->items,
				[&](const auto& entry) { return !entry.second.rule.empty() && entry.second.rule == input.rule; });
		auto response = std::make_unique<marketplace_collection::result>(
			uniform ? marketplace_pawn::convert(input, user_id, pawns.get()) :
			marketplace_collection::redeem(input, user_id, catalog.get()));
		auto reply = server->create_reply(this->task_id(), response->error);
		if (!response->error) reply.add(response);
		reply.send_struct();
	}

	void bdMarketplace::getExpiredInventoryItems(service_server* server, byte_buffer* /*buffer*/) const
	{
		// Existing read-only compatibility stub; schema remains uncaptured.
		server->create_reply(this->task_id()).send();
	}

	void bdMarketplace::validateInventoryItemsToken(service_server* server, byte_buffer* /*buffer*/) const
	{
		// Existing read-only compatibility stub; schema remains uncaptured.
		server->create_reply(this->task_id()).send();
	}

	void bdMarketplace::deleteInventoryItems(service_server* server, byte_buffer* buffer) const
	{
		// S2 expiry acknowledgement: false success invokes 0x27A8F0, removing
		// native records despite no persistent deletion. Mutation is unimplemented;
		// keep it fail-closed. The particular negative error remains provisional.

		server->create_reply(this->task_id(), game::demonware::BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::getEntitlements(service_server* server, byte_buffer* /*buffer*/) const
	{
		// Existing read-only compatibility stub; schema remains uncaptured.
		server->create_reply(this->task_id()).send();
	}

	void bdMarketplace::getProducts(service_server* server, byte_buffer* buffer) const
	{

		send_catalog(server, this->task_id(),
			marketplace_catalog::handle_task99(buffer, marketplace_catalog::get_embedded()));
	}

	void bdMarketplace::getSkusPaginated(service_server* server, byte_buffer* buffer) const
	{

		send_catalog(server, this->task_id(),
			marketplace_catalog::handle_task111(buffer, marketplace_catalog::get_embedded()));
	}

	void bdMarketplace::getBalance(service_server* server, byte_buffer* /*buffer*/) const
	{
		// No confirmed S2 task130 caller/schema; do not alias BOIII's request.
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::getBalanceV2(service_server* server, byte_buffer* buffer) const
	{
		send_query(server, this->task_id(), marketplace_queries::balance(buffer));
	}

	void bdMarketplace::getInventoryPaginated(service_server* server, byte_buffer* buffer) const
	{
		const auto dedicated = game::environment::is_dedicated(); // Immutable launch state.
		const auto identity = runtime_context::get_snapshot();
		const auto local_user_id = dedicated || !identity ? 0 : identity->user_id;
		send_query(server, this->task_id(),
			marketplace_queries::inventory(buffer, local_user_id, dedicated));
	}
}
