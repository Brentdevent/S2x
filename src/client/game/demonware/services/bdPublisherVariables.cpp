#include <std_include.hpp>
#include "../dw_include.hpp"

#include "component/console/console.hpp"

#include <utils/nt.hpp>

#include "resource.hpp"

namespace demonware
{
	namespace
	{
		struct publisher_variables_resource
		{
			std::string_view name_space;
			std::uint16_t major_version;
			std::uint16_t minor_version;
			int resource_id;
		};

		constexpr publisher_variables_resource publisher_variables[]
		{
			{"mp_tu26", 1, 20, DW_PUBLISHER_VARIABLES_MP},
			{"mm_game_tu26", 1, 12, DW_PUBLISHER_VARIABLES_MM_GAME},
			{"mm_hub_tu26", 1, 12, DW_PUBLISHER_VARIABLES_MM_HUB},
			{"mm_party_tu26", 1, 11, DW_PUBLISHER_VARIABLES_MM_PARTY},
			{"qos_tu26", 1, 14, DW_PUBLISHER_VARIABLES_QOS},
		};
	}

	bdPublisherVariables::bdPublisherVariables() : service(95, "bdPublisherVariables")
	{
		this->register_task(1, &bdPublisherVariables::retrievePublisherVariables);
	}

	void bdPublisherVariables::retrievePublisherVariables(service_server* server, byte_buffer* buffer) const
	{
		std::string context;
		std::string name_space;
		if (!buffer->read_string(&context) || !buffer->read_string(&name_space))
		{
			server->create_reply(this->task_id(), BD_PUBLISHER_VARIABLES_SERVICE_ERROR).send();
			return;
		}

		console::demonware("[DW]: [bdPublisherVariables]: retrieving %s/%s\n",
			context.c_str(), name_space.c_str());

		const auto entry = std::ranges::find(publisher_variables, name_space,
			&publisher_variables_resource::name_space);
		if (entry == std::end(publisher_variables))
		{
			console::demonware("[DW]: [bdPublisherVariables]: unknown namespace: %s\n",
				name_space.c_str());
			server->create_reply(this->task_id(), BD_PUBLISHER_VARIABLES_INVALID_NAMESPACE).send();
			return;
		}

		auto variables = utils::nt::load_resource(entry->resource_id);
		while (!variables.empty() && (variables.back() == '\r' || variables.back() == '\n'))
		{
			variables.pop_back();
		}

		if (variables.empty())
		{
			console::demonware("[DW]: [bdPublisherVariables]: missing data for namespace: %s\n",
				name_space.c_str());
			server->create_reply(this->task_id(), BD_PUBLISHER_VARIABLES_SERVICE_ERROR).send();
			return;
		}

		auto result = std::make_unique<bdPublisherVariablesInfo>();
		result->majorVersion = entry->major_version;
		result->minorVersion = entry->minor_version;
		result->nameSpace = name_space;
		result->variables = std::move(variables);

		auto reply = server->create_reply(this->task_id());
		reply.add(result);
		reply.send();
	}
}
