#pragma once

#include <string>
#include <string_view>

namespace lain::core
{
	// An environment variable's value, or empty if it is unset. Reads it the platform-sanctioned way:
	// std::getenv trips MSVC's C4996 under /WX, so Windows uses _dupenv_s (freeing its allocation).
	std::string envVar(std::string_view name);
} // namespace lain::core
