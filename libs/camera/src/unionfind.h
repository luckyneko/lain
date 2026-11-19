#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

// How lain::camera joins things into components, wherever it does: connected cameras (the camera
// observation graph, the hold-out guard, a spanning tree) and feature tracks. Private to lain::camera.
namespace lain::camera::detail
{
	// A union-find forest over the members 0 to count - 1, each its own component until joined.
	class UnionFind
	{
	public:
		explicit UnionFind(std::size_t count)
			: m_parent(count)
		{
			std::iota(m_parent.begin(), m_parent.end(), 0u);
		}

		// The root of `x`'s component, halving the path on the way.
		std::uint32_t root(std::uint32_t x)
		{
			while (m_parent[x] != x)
			{
				m_parent[x] = m_parent[m_parent[x]];
				x = m_parent[x];
			}
			return x;
		}

		// Joins the components of `a` and `b`, the lower root kept, so a component's root is its
		// smallest member whatever order the joins come in. False when they were one already.
		bool join(std::uint32_t a, std::uint32_t b)
		{
			const std::uint32_t ra = root(a);
			const std::uint32_t rb = root(b);
			if (ra == rb)
				return false;
			m_parent[std::max(ra, rb)] = std::min(ra, rb);
			return true;
		}

	private:
		std::vector<std::uint32_t> m_parent;
	};
} // namespace lain::camera::detail
