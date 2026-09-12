#pragma once

// The std::hash specialisation for core::Uri. Split out of uri.h for the reason
// details/uuid.inl is split out of uuid.h: the header should read as the type's interface, and
// forwarding to std::hash<std::string> is implementation.

#include <cstddef>
#include <functional>
#include <string>

template <>
struct std::hash<lain::core::Uri>
{
	std::size_t operator()(const lain::core::Uri& uri) const noexcept
	{
		return std::hash<std::string>{}(uri.toString());
	}
};
