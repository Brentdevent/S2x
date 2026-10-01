#include <std_include.hpp>
#include "../dw_include.hpp"

#include "game/game.hpp"
#include "game/demonware/loot_service.hpp"
#include "game/demonware/loot_store.hpp"

#include "steam/steam.hpp"

#include "component/console/console.hpp"

#include <utils/string.hpp>

namespace demonware
{
	namespace
	{
		void log_request(const char* name, byte_buffer* buffer)
		{
			const auto data = buffer->get_remaining();
			std::string hex{};
			for (std::size_t i = 0; i < data.size() && i < 512; ++i)
			{
				hex += utils::string::va("%02X", static_cast<unsigned char>(data[i]));
			}

			console::demonware("[DW] bdMarketplace: %s request (%zu bytes) %s\n", name, data.size(), hex.data());
		}
	}

	bdMarketplace::bdMarketplace() : service(80, "bdMarketplace")
	{
		this->register_task(42, &bdMarketplace::startExchangeTransaction);
		this->register_task(43, &bdMarketplace::purchaseOnSteamInitialize);
		this->register_task(44, &bdMarketplace::purchaseOnSteamFinalize);
		this->register_task(49, &bdMarketplace::getExpiredInventoryItems);
		this->register_task(58, &bdMarketplace::validateInventoryItemsToken);
		this->register_task(60, &bdMarketplace::steamProcessDurable);
		this->register_task(85, &bdMarketplace::steamProcessDurableV2);
		this->register_task(99, &bdMarketplace::getProducts);
		this->register_task(106, &bdMarketplace::purchaseSkus);
		this->register_task(111, &bdMarketplace::getSkusPaginated);
		this->register_task(123, &bdMarketplace::purchaseSku);
		this->register_task(130, &bdMarketplace::getBalance);
		this->register_task(132, &bdMarketplace::getBalanceV2);
		this->register_task(165, &bdMarketplace::getInventoryPaginated);
		this->register_task(168, &bdMarketplace::updateInventoryItems);
		this->register_task(193, &bdMarketplace::putPlayersInventoryItems);
		this->register_task(199, &bdMarketplace::pawnItems);
		this->register_task(232, &bdMarketplace::getEntitlements);
		this->register_task(242, &bdMarketplace::unknown242);
	}

	void bdMarketplace::startExchangeTransaction(service_server* server, byte_buffer* buffer) const
	{
		log_request("startExchangeTransaction", buffer);
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::purchaseOnSteamInitialize(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::purchaseOnSteamFinalize(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::getExpiredInventoryItems(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::validateInventoryItemsToken(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::steamProcessDurable(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::steamProcessDurableV2(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::purchaseSkus(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::getBalance(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::getBalanceV2(service_server* server, byte_buffer* /*buffer*/) const
	{
		auto reply = server->create_reply(this->task_id());
		if (!game::environment::is_dedicated())
		{
			for (const auto& [currency_id, value] : loot_service::get_balances())
			{
				if (currency_id > std::numeric_limits<std::uint8_t>::max())
				{
					continue;
				}

				auto currency = std::make_unique<bdMarketplaceCurrency>();
				currency->m_currencyId = static_cast<std::uint8_t>(currency_id);
				currency->m_value = value;
				reply.add(currency);
			}
		}

		reply.send();
	}

	void bdMarketplace::getInventoryPaginated(service_server* server, byte_buffer* buffer) const
	{
		std::string platform{};
		std::uint32_t page{};
		std::uint32_t items_per_page{};
		if (!buffer->read_string(&platform) || !buffer->read_uint32(&page) || !buffer->read_uint32(&items_per_page))
		{
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send();
			return;
		}

		auto reply = server->create_reply(this->task_id());
		if (!game::environment::is_dedicated() && page > 0 && items_per_page > 0)
		{
			const auto user_id = steam::SteamUser()->GetSteamID().bits;
			const auto now = static_cast<std::uint32_t>(time(nullptr));
			const auto items = loot_store::get().items;
			const auto first = static_cast<std::uint64_t>(page - 1) * items_per_page;
			std::uint64_t index{};
			for (const auto& [item_id, quantity] : items)
			{
				if (index++ < first)
				{
					continue;
				}

				if (index > first + items_per_page)
				{
					break;
				}

				auto item = std::make_unique<bdMarketplaceInventory>();
				item->m_playerId = user_id;
				item->unk = "steam";
				item->m_itemId = item_id;
				item->m_itemQuantity = quantity;
				item->m_itemXp = 0;
				item->m_expireDateTime = 0;
				item->m_expiryDuration = 0;
				item->m_collisionField = 0;
				item->m_modDateTime = now;
				reply.add(item);
			}
		}

		reply.send();
	}

	void bdMarketplace::updateInventoryItems(service_server* server, byte_buffer* /*buffer*/) const
	{
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::unknown242(service_server* server, byte_buffer* buffer) const
	{
		log_request("unknown242", buffer);
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::putPlayersInventoryItems(service_server* server, byte_buffer* buffer) const
	{
		log_request("putPlayersInventoryItems", buffer);
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::pawnItems(service_server* server, byte_buffer* buffer) const
	{
		log_request("pawnItems", buffer);
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::getEntitlements(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::getSkusPaginated(service_server* server, byte_buffer* /*buffer*/) const
	{
		auto reply = server->create_reply(this->task_id());
		if (!game::environment::is_dedicated())
		{
			for (const auto& entry : loot_service::get_skus())
			{
				auto sku = std::make_unique<bdMarketplaceSku>();
				sku->m_skuId = entry.sku_id;
				sku->m_productId = entry.sku_id;
				sku->m_skuData = entry.sku_data;
				sku->m_prices.emplace_back(entry.currency_id, entry.price);
				reply.add(sku);
			}
		}

		reply.send();
	}

	void bdMarketplace::purchaseSku(service_server* server, byte_buffer* buffer) const
	{
		constexpr std::size_t max_transaction_id = 24;
		constexpr std::size_t max_platform = 9;
		constexpr std::uint32_t max_prices = 10;

		std::string platform{};
		std::string transaction_id{};
		std::uint64_t user_id{};
		std::string user_platform{};
		std::uint32_t count{};
		std::uint32_t sku_id{};
		std::uint32_t quantity{};
		std::uint32_t unknown{};
		std::uint32_t price_count{};
		if (!buffer->read_string(&platform) || !buffer->read_string(&transaction_id) || !buffer->read_uint64(&user_id)
			|| !buffer->read_string(&user_platform) || !buffer->read_uint32(&count) || count != 1
			|| !buffer->read_uint32(&sku_id) || !buffer->read_uint32(&quantity) || !buffer->read_uint32(&unknown)
			|| !buffer->read_uint32(&price_count) || price_count > max_prices
			|| transaction_id.size() > max_transaction_id || game::environment::is_dedicated())
		{
			server->create_reply(this->task_id(), BD_MARKETPLACE_INVALID_PARAMETER).send();
			return;
		}

		const auto result = loot_service::purchase(sku_id, quantity);
		if (!result)
		{
			server->create_reply(this->task_id(), BD_MARKETPLACE_INVALID_PARAMETER).send();
			return;
		}

		auto purchase = std::make_unique<bdMarketplacePurchase>();
		purchase->m_transactionId = transaction_id;
		purchase->m_userId = steam::SteamUser()->GetSteamID().bits;
		purchase->m_platform = user_platform.size() <= max_platform ? user_platform : "steam";
		purchase->m_currencies.emplace_back(result->currency_id, result->balance);
		for (const auto& [item_id, item_quantity] : result->items)
		{
			purchase->m_items.emplace_back(item_id, item_quantity);
		}

		auto reply = server->create_reply(this->task_id());
		reply.add(purchase);
		reply.send();
	}

	void bdMarketplace::getProducts(service_server* server, byte_buffer* buffer) const
	{
		constexpr std::uint32_t max_products = 100;

		std::string platform{};
		std::uint32_t unknown{};
		std::uint32_t count{};
		std::uint32_t id_count{};
		if (!buffer->read_string(&platform) || !buffer->read_uint32(&unknown) || !buffer->read_uint32(&count)
			|| !buffer->read_uint32(&id_count) || id_count > max_products)
		{
			server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send();
			return;
		}

		std::vector<std::uint32_t> product_ids(id_count);
		for (auto& product_id : product_ids)
		{
			if (!buffer->read_uint32(&product_id))
			{
				server->create_reply(this->task_id(), BD_PARAM_PARSE_ERROR).send();
				return;
			}
		}

		auto reply = server->create_reply(this->task_id());
		for (const auto product_id : product_ids)
		{
			auto product = std::make_unique<bdMarketplaceProduct>();
			product->m_productId = product_id;
			if (!game::environment::is_dedicated())
			{
				for (const auto& entry : loot_service::get_skus())
				{
					if (entry.sku_id == product_id)
					{
						for (const auto item_id : entry.item_ids)
						{
							product->m_items.emplace_back(item_id, 1);
						}
					}
				}
			}

			reply.add(product);
		}

		reply.send();
	}
}
