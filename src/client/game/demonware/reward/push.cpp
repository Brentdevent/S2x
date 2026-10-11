#include <std_include.hpp>
#include "game/demonware/dw_include.hpp"
#include "push.hpp"

namespace demonware
{
	void send_reward_push(service_server* server, const std::uint32_t type, const std::uint64_t user_id,
		const std::string& json, const std::string& context)
	{
		bdRewardEvent event{};
		event.push_type = type;
		event.r2 = 1;
		event.user_id = user_id;
		event.platform1 = "steam";
		event.platform2 = context;
		event.rewardEventType = 1;
		event.r7 = 1;
		event.r8 = 1;
		event.json_buffer = json;

		byte_buffer buffer{};
		event.serialize(&buffer);
		server->create_message(BD_LOBBY_SERVICE_PUSH_MESSAGE).send(&buffer, true);
	}
}
