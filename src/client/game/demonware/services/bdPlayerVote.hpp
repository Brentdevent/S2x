#pragma once

namespace demonware
{
	class bdPlayerVote final : public service
	{
	public:
		bdPlayerVote();

	private:
		void vote(service_server* server, byte_buffer* buffer) const;
		void getVoteStatus(service_server* server, byte_buffer* buffer) const;
		void getVoteCounts(service_server* server, byte_buffer* buffer) const;
	};
}
