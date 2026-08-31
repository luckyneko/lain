#include "lain/media/frameref.h"

#include <lain/string/format.h>

namespace lain::media
{
	std::string FrameRef::toString() const
	{
		if (!valid())
			return "no frame";
		return lain::string::format("frame {} of {}", ordinal, source);
	}

	bool operator==(const FrameRef& a, const FrameRef& b)
	{
		// The timestamp is derived from the source and the ordinal, so two refs agreeing on
		// those agree on it too; comparing it as well costs nothing and keeps this a plain
		// structural equality rather than a rule about which fields count.
		return a.source == b.source && a.ordinal == b.ordinal && a.timestamp == b.timestamp;
	}

	bool operator!=(const FrameRef& a, const FrameRef& b) { return !(a == b); }
} // namespace lain::media
