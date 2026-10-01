#pragma once

namespace economy
{
	// Safe from service threads; the native paginated fetch runs on main.
	void request_inventory_refresh();
}
