#include "lain/camera/registration/observationgraph.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <utility>

namespace lain::camera::registration
{
	static std::uint32_t root(std::vector<std::uint32_t>& parent, std::uint32_t x)
	{
		while (parent[x] != x)
		{
			parent[x] = parent[parent[x]];
			x = parent[x];
		}
		return x;
	}

	// Marks each bridge (Tarjan): an edge (u, v) of the DFS tree is a bridge when nothing below v
	// reaches u or above by another edge. Iterative, so a large rig cannot overflow the stack.
	static void markBridges(ObservationGraph& graph)
	{
		const std::uint32_t n = graph.cameras;
		std::vector<std::vector<std::pair<std::uint32_t, std::size_t>>> adjacent(n); // (camera, edge)
		for (std::size_t e = 0; e < graph.edges.size(); ++e)
		{
			adjacent[graph.edges[e].a].push_back({graph.edges[e].b, e});
			adjacent[graph.edges[e].b].push_back({graph.edges[e].a, e});
		}
		constexpr std::uint32_t kUnvisited = ~std::uint32_t(0);
		std::vector<std::uint32_t> order(n, kUnvisited), low(n, 0);
		std::uint32_t counter = 0;
		struct Frame
		{
			std::uint32_t camera;
			std::size_t viaEdge; // the tree edge that reached it; SIZE_MAX at a root
			std::size_t next;	 // the next adjacency to look at
		};
		for (std::uint32_t start = 0; start < n; ++start)
		{
			if (order[start] != kUnvisited)
				continue;
			std::vector<Frame> stack{{start, SIZE_MAX, 0}};
			order[start] = low[start] = counter++;
			while (!stack.empty())
			{
				Frame& top = stack.back();
				if (top.next < adjacent[top.camera].size())
				{
					const auto [other, edge] = adjacent[top.camera][top.next++];
					if (edge == top.viaEdge)
						continue;
					if (order[other] == kUnvisited)
					{
						order[other] = low[other] = counter++;
						stack.push_back({other, edge, 0});
					}
					else
						low[top.camera] = std::min(low[top.camera], order[other]);
					continue;
				}
				const Frame done = top;
				stack.pop_back();
				if (!stack.empty())
				{
					const std::uint32_t parent = stack.back().camera;
					low[parent] = std::min(low[parent], low[done.camera]);
					if (low[done.camera] > order[parent])
						graph.edges[done.viaEdge].bridge = true;
				}
			}
		}
	}

	ObservationGraph observationGraph(std::uint32_t cameras, const std::vector<std::vector<std::uint32_t>>& groups)
	{
		ObservationGraph graph;
		graph.cameras = cameras;
		graph.groupsPerCamera.assign(cameras, 0);

		std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> shared;
		for (std::vector<std::uint32_t> members : groups)
		{
			std::sort(members.begin(), members.end());
			members.erase(std::unique(members.begin(), members.end()), members.end());
			if (members.size() < 2)
				continue;
			for (std::size_t i = 0; i < members.size(); ++i)
			{
				++graph.groupsPerCamera[members[i]];
				for (std::size_t j = i + 1; j < members.size(); ++j)
					++shared[{members[i], members[j]}];
			}
		}
		for (const auto& [pair, count] : shared)
			graph.edges.push_back({pair.first, pair.second, count, false});

		std::vector<std::uint32_t> parent(cameras);
		std::iota(parent.begin(), parent.end(), 0u);
		for (const GraphEdge& e : graph.edges)
			parent[root(parent, e.a)] = root(parent, e.b);
		std::map<std::uint32_t, std::vector<std::uint32_t>> byRoot;
		for (std::uint32_t c = 0; c < cameras; ++c)
			byRoot[root(parent, c)].push_back(c);
		for (auto& [r, members] : byRoot)
			graph.components.push_back(std::move(members));
		std::sort(graph.components.begin(), graph.components.end(),
				  [](const auto& x, const auto& y)
				  { return x.front() < y.front(); });

		markBridges(graph);
		return graph;
	}

	std::vector<GraphEdge> weakBridges(const ObservationGraph& graph, std::uint32_t minimumSharedGroups)
	{
		std::vector<GraphEdge> out;
		for (const GraphEdge& e : graph.edges)
		{
			if (e.bridge && e.sharedGroups < minimumSharedGroups)
				out.push_back(e);
		}
		return out;
	}
} // namespace lain::camera::registration
