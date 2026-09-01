#include "lain/media/frameposition.h"

namespace lain::media
{
	std::string FramePosition::toString() const
	{
		// Self-describing rather than a bare number: this renders in the cli dump and the inspector
		// beside values of every other type, and "12" alone would say nothing about what it counts.
		return "frame " + std::to_string(value);
	}

	bool operator==(FramePosition a, FramePosition b) { return a.value == b.value; }
	bool operator!=(FramePosition a, FramePosition b) { return !(a == b); }
} // namespace lain::media
