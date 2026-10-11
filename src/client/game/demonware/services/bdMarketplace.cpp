#include <std_include.hpp>
#include "../dw_include.hpp"

#include "game/demonware/marketplace/catalog.hpp"
#include "game/demonware/marketplace/collection.hpp"
#include "game/demonware/marketplace/inventory.hpp"
#include "game/demonware/marketplace/pawn.hpp"
#include "game/demonware/marketplace/purchase.hpp"
#include "game/demonware/marketplace/queries.hpp"
#include "game/demonware/marketplace/promotional_vouchers.hpp"
#include "game/demonware/runtime_context.hpp"

#include "game/game.hpp"

namespace demonware
{
	namespace
	{
		void send_pushes(service_server* server, const std::vector<std::string>& messages)
		{
			for (const auto& message : messages)
			{
				byte_buffer push{message};
				server->create_message(BD_LOBBY_SERVICE_PUSH_MESSAGE).send(&push, true);
			}
		}

		void send_query(service_server* server, const std::uint8_t task, marketplace_queries::result response)
		{
			auto reply = server->create_reply(task, response.error);
			if (!response.error)
			{
				for (auto& record : response.records)
				{
					reply.add(record);
				}
			}

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

		template <typename Result>
		void send_struct_result(service_server* server, const std::uint8_t task, Result result)
		{
			auto response = std::make_unique<Result>(std::move(result));

			auto reply = server->create_reply(task, response->error);
			if (!response->error)
			{
				reply.add(response);
			}

			reply.send_struct();
		}

		bool is_purchasable_sku_type(const std::uint8_t type)
		{
			return type == marketplace_sku::quartermaster_sku_type || type == marketplace_sku::collection_sku_type;
		}

		const marketplace_sku::result* find_purchasable_sku(const marketplace_catalog::catalog& catalog,
			const std::uint32_t sku_id)
		{
			for (const auto& entry : catalog.skus())
			{
				if (entry.fields.sku_id == sku_id && is_purchasable_sku_type(entry.fields.sku_type))
				{
					return &entry.fields;
				}
			}

			return nullptr;
		}

		bool is_pawn_conversion(const marketplace_collection::request& input,
			const std::shared_ptr<const pawn_catalog::catalog>& pawns)
		{
			if (const auto receipt = marketplace_store::find_transaction("marketplace:242:" + input.client_tx))
			{
				return receipt->request_fingerprint.starts_with("pawn:");
			}

			return pawns && std::ranges::any_of(pawns->items, [&](const auto& entry)
			{
				return !entry.second.rule.empty() && entry.second.rule == input.rule;
			});
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

	void bdMarketplace::startExchangeTransaction(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::purchaseOnSteamInitialize(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::purchaseOnSteamFinalize(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::getExpiredInventoryItems(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id()).send();
	}

	void bdMarketplace::deleteInventoryItems(service_server* server, byte_buffer* /*buffer*/) const
	{
		// Success makes 0x27A8F0 drop native records, but nothing is deleted from storage yet
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::validateInventoryItemsToken(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id()).send();
	}

	void bdMarketplace::steamProcessDurable(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::steamProcessDurableV2(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::consumeInventoryItems(service_server* server, byte_buffer* buffer) const
	{
		std::vector<std::string> updates{};
		const auto error = marketplace_inventory::consume(buffer, runtime_context::get_local_user_id(), updates);

		server->create_reply(this->task_id(), error).send();

		if (!error)
		{
			send_pushes(server, updates);
		}
	}

	void bdMarketplace::getProducts(service_server* server, byte_buffer* buffer) const
	{
		send_catalog(server, this->task_id(), marketplace_catalog::handle_task99(buffer, marketplace_catalog::get_embedded()));
	}

	void bdMarketplace::purchaseSkus(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::getSkusPaginated(service_server* server, byte_buffer* buffer) const
	{
		send_catalog(server, this->task_id(), marketplace_catalog::handle_task111(buffer, marketplace_catalog::get_embedded()));
	}

	void bdMarketplace::purchaseSkusV3(service_server* server, byte_buffer* buffer) const
	{
		marketplace_purchase::request input{};
		if (!marketplace_purchase::parse_request(buffer, input))
		{
			server->create_reply(this->task_id(), BD_MARKETPLACE_INVALID_PARAMETER).send();
			return;
		}

		const marketplace_sku::result* sku{};
		const marketplace_product::result* product{};

		if (const auto& catalog = marketplace_catalog::get_embedded().value)
		{
			sku = find_purchasable_sku(*catalog, input.sku_id);

			const auto found = sku ? catalog->find_product(sku->field_36) : nullptr;
			product = found ? &found->fields : nullptr;
		}

		const auto collections = collection_catalog::get_snapshot();
		auto response = std::make_unique<marketplace_purchase::result>(marketplace_purchase::purchase(input,
			runtime_context::get_local_user_id(), sku, product, collections.get()));

		auto reply = server->create_reply(this->task_id(), response->error);
		if (!response->error)
		{
			reply.add(response);
		}

		reply.send();
	}

	void bdMarketplace::getBalance(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::getBalanceV2(service_server* server, byte_buffer* buffer) const
	{
		send_query(server, this->task_id(), marketplace_queries::balance(buffer));
	}

	void bdMarketplace::getInventoryPaginated(service_server* server, byte_buffer* buffer) const
	{
		send_query(server, this->task_id(), marketplace_queries::inventory(buffer, runtime_context::get_local_user_id(),
			game::environment::is_dedicated()));
	}

	void bdMarketplace::putInventoryItemsData(service_server* server, byte_buffer* buffer) const
	{
		const auto error = marketplace_inventory::put_item_data(buffer, runtime_context::get_local_user_id());
		server->create_reply(this->task_id(), error).send();
	}

	void bdMarketplace::putPlayersInventoryItems(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id(), BD_SERVICE_NOT_AVAILABLE).send();
	}

	void bdMarketplace::pawnItems(service_server* server, byte_buffer* buffer) const
	{
		const auto catalog = pawn_catalog::get_snapshot();

		std::vector<std::string> updates{};
		const auto error = marketplace_pawn::pawn(buffer, runtime_context::get_local_user_id(), catalog.get(), updates);

		server->create_reply(this->task_id(), error).send();

		if (!error)
		{
			send_pushes(server, updates);
		}
	}

	void bdMarketplace::getEntitlements(service_server* server, byte_buffer* /*buffer*/) const
	{
		server->create_reply(this->task_id()).send();
	}

	void bdMarketplace::applyConversionRule(service_server* server, byte_buffer* buffer) const
	{
		marketplace_collection::request input{};
		if (!marketplace_collection::parse_request(buffer, input))
		{
			server->create_reply(this->task_id(), BD_MARKETPLACE_INVALID_PARAMETER).send();
			return;
		}

		const auto user_id = runtime_context::get_local_user_id();

		if (auto voucher = promotional_vouchers::redeem(input, user_id))
		{
			send_struct_result(server, this->task_id(), std::move(*voucher));
			return;
		}

		const auto pawns = pawn_catalog::get_snapshot();
		if (is_pawn_conversion(input, pawns))
		{
			send_struct_result(server, this->task_id(), marketplace_pawn::convert(input, user_id, pawns.get()));
			return;
		}

		const auto collections = collection_catalog::get_snapshot();
		send_struct_result(server, this->task_id(), marketplace_collection::redeem(input, user_id, collections.get()));
	}
}
