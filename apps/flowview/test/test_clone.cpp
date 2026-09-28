// The clone census (M14 slice 1, ADR-0025): every node kind the application can create must clone
// to ITSELF — the same dynamic type, the same recipe. A run is going to read a clone of the
// document, so a kind that clones wrong is a kind whose runs compute something other than what the
// canvas shows.
//
// Driven over the PRODUCTION factory (registerExampleNodes), not a list kept here: a second list is
// how a newly registered kind quietly escapes the census. What it catches that the compiler cannot:
// `Node::clone()` is pure, so every DIRECT subclass must have one — but a subclass of a CONCRETE kind
// inherits its parent's, and comes back as the parent, sliced. Graph::clone asserts that in Debug;
// this asks it in every configuration.
//
// What it cannot catch is a member a hand-written copy constructor leaves out when that member is
// not serialized as itself (a loop's PortId members). The scene tests' "a cloned ... runs the same"
// cases are what run a clone and compare answers.

#include "graphio.h" // sceneCodecs, registerSceneSerialization
#include "scene.h"	 // registerExampleNodes — the production keys

#include <lain/core/factory.h>
#include <lain/data/value.h>
#include <lain/flow/boundary.h>
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/node.h>
#include <lain/flow/serialize/serialize.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

using lain::core::Factory;
using namespace lain::flow;

namespace
{
	Factory<Node> productionFactory()
	{
		flowview::registerSceneSerialization(); // port types + codecs, so a document can be written
		Factory<Node> factory;
		flowview::registerExampleNodes(factory, 8);
		return factory;
	}

	// A graph holding one node of kind `key`, with enough around it that its clone has something to
	// get wrong: pins on the boundary pair, and — for a kind that OWNS an interior — a node inside it.
	Graph graphHolding(const Factory<Node>& factory, const std::string& key)
	{
		Graph graph;
		graph.boundaryInputNode().addBoundary<int>("in");
		graph.boundaryOutputNode().addBoundary<int>("out");

		std::unique_ptr<Node> created = factory.create(key);
		REQUIRE(created != nullptr);
		// Asked BEFORE the add, which takes ownership and destroys a node it refuses.
		const Node& kind = *created;
		const Node& input = graph.boundaryInputNode();
		const Node& output = graph.boundaryOutputNode();
		const bool boundary = typeid(kind) == typeid(input) || typeid(kind) == typeid(output);

		const NodeId id = graph.add(std::move(created));
		if (id == NodeId{})
		{
			// Refused: the only kinds a graph refuses are its boundary pair, of which it already
			// holds exactly one each — and those are cloned with every graph, so they are covered.
			REQUIRE(boundary);
			return graph;
		}

		// Asked of the SEAM, never the class: a kind that hands out a mutable interior owns one.
		if (auto* group = dynamic_cast<GroupNode*>(&graph.node(id)))
		{
			if (Graph* inner = group->editableInner())
				inner->add(factory.create("tint"));
		}
		return graph;
	}
} // namespace

TEST_CASE("every node kind the application creates clones to itself", "[flowview][clone]")
{
	const Factory<Node> factory = productionFactory();
	const serialize::ValueCodecs codecs = flowview::sceneCodecs();

	const std::vector<std::string> keys = factory.keys();
	REQUIRE_FALSE(keys.empty());
	for (const std::string& key : keys)
	{
		INFO("factory key: " << key);
		Graph graph = graphHolding(factory, key);
		Graph clone = graph.clone();

		REQUIRE(clone.lineage() == graph.lineage());
		REQUIRE(clone.nodeIds() == graph.nodeIds());
		for (const NodeId id : graph.nodeIds())
		{
			Node& original = graph.node(id);
			Node& copy = clone.node(id);
			REQUIRE(&copy != &original);
			// The same dynamic type: a kind that inherited its parent's clone() fails HERE.
			REQUIRE(typeid(copy) == typeid(original));
			REQUIRE(copy.version() == original.version());

			// An interior the node OWNS is deep-copied; one it SHARES (a linked template) is shared.
			if (auto* group = dynamic_cast<GroupNode*>(&original))
			{
				auto& copiedGroup = dynamic_cast<GroupNode&>(copy);
				REQUIRE(copiedGroup.innerGraph() != nullptr);
				if (group->editableInner() != nullptr)
				{
					REQUIRE(copiedGroup.innerGraph() != group->innerGraph());
					REQUIRE(copiedGroup.innerGraph()->lineage() == group->innerGraph()->lineage());
				}
				else
				{
					REQUIRE(copiedGroup.innerGraph() == group->innerGraph());
				}
			}
		}

		// The same RECIPE, as the document writes it: every param, pin, payload type, carry pairing,
		// interior and id. Compared as documents because that is what "the same recipe" means here.
		REQUIRE(serialize::toValue(clone, factory, codecs) == serialize::toValue(graph, factory, codecs));
	}
}
