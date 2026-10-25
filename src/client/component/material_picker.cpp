#include <std_include.hpp>

#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include "material_picker/picker.hpp"

namespace material_picker
{
	class component final : public generic_component
	{
	public:
		void post_unpack() override
		{
			if (game::environment::is_dedicated())
			{
				return;
			}

			initialize();
		}
	};
}

REGISTER_COMPONENT(material_picker::component)
