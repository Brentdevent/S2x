#pragma once

namespace demonware
{
	class bdReward final : public service
	{
	public:
		bdReward();

	private:
		void incrementTime(service_server* server, byte_buffer* buffer) const;
		void claimRewardRoll(service_server* server, byte_buffer* buffer) const;
		void claimClientAchievements(service_server* server, byte_buffer* buffer) const;
		void reportRewardEvents(service_server* server, byte_buffer* buffer) const;
		void reportRewardGameEventsForUsers(service_server* server, byte_buffer* buffer) const;
		void reportRewardEventsSync(service_server* server, byte_buffer* buffer) const;
	};
}
