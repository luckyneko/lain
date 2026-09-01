#include "framepattern.h"

#include <lain/io/uri.h>

namespace flowview
{
	std::string frameOutputPath(std::string_view pattern, std::size_t frame)
	{
		return lain::io::substituteNumber(pattern, static_cast<unsigned long long>(frame));
	}

	bool isFramePattern(std::string_view pattern)
	{
		return lain::io::numberField(pattern).found();
	}
} // namespace flowview
