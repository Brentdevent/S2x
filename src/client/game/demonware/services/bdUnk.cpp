#include <std_include.hpp>
#include "../dw_include.hpp"

namespace demonware
{
	bdUnk::bdUnk() : service(55, "bdUnk")
	{
		this->register_task(3, &bdUnk::unk);
	}

	void bdUnk::unk(service_server* server, byte_buffer* /*buffer*/) const
	{
		// TODO:
		auto reply = server->create_reply(this->task_id());
		reply.send();
	}
}
