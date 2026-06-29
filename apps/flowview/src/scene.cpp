#include "scene.h"

#include <archimedes/acmTypes.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/graph.h>

namespace flowview
{
	lain::flow::NodeId buildExampleScene(lain::flow::Graph& graph, acm::Device device, std::uint32_t size)
	{
		using namespace lain::flow;
		return graph.add<example::GradientNode>(device, acm::Extent2D{size, size});
	}
} // namespace flowview
