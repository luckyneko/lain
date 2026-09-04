// The node catalog — what the gui's three add surfaces offer (the menu bar's Add, the canvas
// right-click palette, and the Nodes pane all read nodeCatalog()).
//
// M10 slice 7 is where the frame-sequence kinds joined it: until then they were registered in the
// factory alone, reachable from the cli and from a saved document but from no menu. The invariant
// worth pinning is the one a hand-added entry breaks silently — a key the factory cannot build is a
// menu item that does nothing at all, which is M5's bug six (a gesture that compiled, linked, and
// was unreachable) arriving from the other direction.

#include "scene.h"

#include <lain/core/factory.h>
#include <lain/flow/node.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace lain;

namespace
{
	std::vector<std::string> catalogKeys()
	{
		std::vector<std::string> keys;
		for (const flowview::NodeCategory& category : flowview::nodeCatalog())
			keys.insert(keys.end(), category.keys.begin(), category.keys.end());
		return keys;
	}

	bool offers(const std::string& key)
	{
		const std::vector<std::string> keys = catalogKeys();
		return std::find(keys.begin(), keys.end(), key) != keys.end();
	}
} // namespace

TEST_CASE("every catalog kind can actually be created", "[catalog]")
{
	core::Factory<flow::Node> factory;
	flowview::registerExampleNodes(factory, 8);

	const std::vector<std::string> keys = catalogKeys();
	REQUIRE_FALSE(keys.empty());
	for (const std::string& key : keys)
	{
		INFO("catalog key: " << key);
		CHECK(factory.create(key) != nullptr);
	}
}

TEST_CASE("the frame-sequence kinds are on the menu", "[catalog]")
{
	// The three that make footage reachable without hand-editing a document: bring it in, take one
	// frame of it, trim the range.
	CHECK(offers("openSequence"));
	CHECK(offers("frameAt"));
	CHECK(offers("clipSequence"));

	// The boundary pair stays OFF it — one each per graph, born with the graph and grown from the
	// Interface panel, not added like an ordinary node.
	CHECK_FALSE(offers("groupInput"));
	CHECK_FALSE(offers("groupOutput"));
	// ... and so does a linked group, which needs a template chosen first (the menu bar adds it
	// through a file dialog).
	CHECK_FALSE(offers("linkedGroup"));
}
