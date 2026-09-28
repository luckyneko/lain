#include "lain/flow/node.h"

#include <atomic>

namespace lain::flow
{
	std::uint64_t Node::mintVersion()
	{
		// Starts at 1, so no version ever equals the 0 a freshly prepared Evaluation records as
		// "computed at nothing". Relaxed: the only property wanted is that no value is handed out
		// twice, and a node may be constructed on any thread (a loader, a coordinator).
		static std::atomic<std::uint64_t> next{1};
		return next.fetch_add(1, std::memory_order_relaxed);
	}
} // namespace lain::flow
