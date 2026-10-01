#include <std_include.hpp>
#include "../dw_include.hpp"

#include "game/game.hpp"
#include "game/demonware/loot_service.hpp"
#include "game/demonware/loot_store.hpp"

#include "steam/steam.hpp"

namespace demonware
{
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
		this->register_task(130, &bdMarketplace::getBalance);
		this->register_task(132, &bdMarketplace::getBalanceV2);
		this->register_task(165, &bdMarketplace::getInventoryPaginated);
		this->register_task(193, &bdMarketplace::putPlayersInventoryItems);
		this->register_task(199, &bdMarketplace::pawnItems);
		this->register_task(232, &bdMarketplace::getEntitlements);
	}

	void bdMarketplace::startExchangeTransaction(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
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

	void bdMarketplace::putPlayersInventoryItems(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}

	void bdMarketplace::pawnItems(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
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
						product->m_items.emplace_back(entry.item_id, 1);
					}
				}
			}

			reply.add(product);
		}

		reply.send();
	}
}
