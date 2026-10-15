#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// How lain::camera joins things into components, wherever it does: the camera observation graph and
// feature-track extraction. Private to lain::camera.
namespace lain::camera::detail
{
	// The root of `x` in a union-find forest, halving the path on the way.
	inline std::uint32_t root(std::vector<std::uint32_t>& parent, std::uint32_t x)
	{
		while (parent[x] != x)
		{
			parent[x] = parent[parent[x]];
			x = parent[x];
		}
		return x;
	}

	// Joins the components of `a` and `b`, the lower root kept, so a component's root is its smallest
	// member whatever order the joins come in.
	inline void join(std::vector<std::uint32_t>& parent, std::uint32_t a, std::uint32_t b)
	{
		const std::uint32_t ra = root(parent, a);
		const std::uint32_t rb = root(parent, b);
		if (ra != rb)
			parent[std::max(ra, rb)] = std::min(ra, rb);
	}
} // namespace lain::camera::detail
