#pragma once

enum class component_priority
{
	min = 0,
	// must run after the steam_proxy
	name,
	// must run after the updater
	steam_proxy,
	updater,
	// must have the highest priority
	arxan,
};

enum class component_type
{
	multiplayer,
	singleplayer,
	any,
};

struct generic_component
{
	static constexpr component_type type = component_type::any;

	virtual ~generic_component() = default;

	virtual void post_load()
	{
	}

	virtual void pre_destroy()
	{
	}

	virtual void post_thread_setup()
	{
	}

	virtual void post_unpack()
	{
	}

	virtual component_priority priority() const
	{
		return component_priority::min;
	}
};

struct multiplayer_component : generic_component
{
	static constexpr component_type type = component_type::multiplayer;
};

struct singleplayer_component : generic_component
{
	static constexpr component_type type = component_type::singleplayer;
};
