#pragma once

// Template method definitions for lain::flow::Graph (see graph.h).

namespace lain::flow
{
	template <typename T, typename... Args>
	NodeId Graph::add(Args&&... args)
	{
		static_assert(std::is_base_of<Node, T>::value, "T must derive from lain::flow::Node");
		return add(std::make_unique<T>(std::forward<Args>(args)...)); // the adopt overload assigns the id
	}
} // namespace lain::flow
