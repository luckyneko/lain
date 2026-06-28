#pragma once

#include <cstdint>

#include <lain/core/time.h>

namespace lain::app
{
	// A per-frame time snapshot, filled by the Application each loop iteration and
	// passed to the delegates alongside InputState. Mirrors InputState's role.
	struct TimeState
	{
		lain::core::Time elapsed; // since the loop started (absolute, monotonic)
		lain::core::Time delta;   // since the previous frame
		uint64_t frame{ 0 };      // update/frame index
	};
}
