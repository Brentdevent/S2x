#pragma once

namespace fps
{
	// Rolling renderer FPS, or zero before the first measured frame (and on dedicated servers).
	int get_fps();
}
