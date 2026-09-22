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
} // namespace lain::media
