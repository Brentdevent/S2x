#include <std_include.hpp>

#include "runtime_context.hpp"

#include <atomic>

namespace demonware::runtime_context
{
	namespace
	{
		std::atomic<std::shared_ptr<const identity>> current{};
	}

	std::shared_ptr<const identity> get_snapshot()
	{
		return current.load(std::memory_order_acquire);
	}

	bool publish(identity value)
	{
		if (!value.user_id || value.persona_name.empty() || value.persona_name.size() > 255 ||
			value.persona_name.find('\0') != std::string::npos)
		{
			return false;
		}
		current.store(std::make_shared<const identity>(std::move(value)), std::memory_order_release);
		return true;
	}

	void clear()
	{
		current.store({}, std::memory_order_release);
	}
}
