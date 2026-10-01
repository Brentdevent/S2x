#pragma once

namespace economy
{
	// Safe from service threads, the native fetch runs on main
	void request_inventory_refresh();
}
