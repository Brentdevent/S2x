#pragma once

namespace demonware
{
	class bdUnk final : public service
	{
	public:
		bdUnk();

	private:
		void unk(service_server* server, byte_buffer* buffer) const;
	};
}
