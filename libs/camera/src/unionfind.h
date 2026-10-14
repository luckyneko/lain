#pragma once

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
} // namespace lain::camera::detail
