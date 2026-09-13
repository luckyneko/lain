#include "framepattern.h"

#include <lain/io/image/open.h> // frameKey
#include <lain/string/pattern.h>

namespace flowview
{
	std::optional<std::string> frameOutputPath(std::string_view pattern, std::size_t frame)
	{
		lain::string::Dictionary values;
		values.set(lain::io::image::frameKey, frame);
		return lain::string::Pattern{pattern}.format(values);
	}

	bool isFramePattern(std::string_view pattern)
	{
		return lain::string::Pattern{pattern}.has(lain::io::image::frameKey);
	}
} // namespace flowview
