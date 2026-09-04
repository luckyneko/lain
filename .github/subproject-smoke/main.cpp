// The consumer's side of the subproject smoke: link lain::flow alone and use it. Deliberately
// not the app stack — the point is that a headless consumer needs nothing of it.

#include <lain/core/version.h>
#include <lain/flow/graph.h>

#include <optional>

int main()
{
	// Every Graph is born with its boundary pair, so a default-constructed one is already a
	// working document — enough to prove the library links and its invariants hold.
	const lain::flow::Graph graph;
	if (graph.nodeIds().size() != 2)
	{
		return 1;
	}

	// lain::core comes in as a PUBLIC dependency of lain::flow (a NodeId IS a core::Uuid), so a
	// consumer linking only lain::flow can use it — check that too.
	const std::optional<lain::core::Version> version = lain::core::Version::parse("1.2.3");
	return version && version->major() == 1 ? 0 : 1;
}
