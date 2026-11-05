#include "lain/media/frameref.h"

#include <lain/string/format.h>

#include <tuple>

namespace lain::media
{
	std::string FrameRef::toString() const
	{
		if (!valid())
			return "no frame";
		return lain::string::format("frame {} of {}", ordinal, source);
	}

	bool capturedBefore(const FrameRef& a, const FrameRef& b)
	{
		return std::tie(a.timestamp, a.ordinal, a.source) < std::tie(b.timestamp, b.ordinal, b.source);
	}
} // namespace lain::media
