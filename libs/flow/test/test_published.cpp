// The published evaluation (M14 slice 2, ADR-0025). A gui host's panes cannot read the working
// evaluation while a run holds it, so they read a COPY of it — and a copy reads through the graph
// its source was last prepared against (ready(), describe(), and every child's reads into that
// graph's interiors). So the copy and that graph travel together, as one PublishedEvaluation.
//
// Each run here reads a shared clone of the document, which is the runner's shape (slice 4): the
// clone is what a published evaluation must keep alive once the run that read it is gone.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"
#include "testscene.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

using namespace lain::flow;
using namespace lain::flow::test;

// The document cloned into shared ownership and run — what a host would publish from.
static std::shared_ptr<const Graph> runOnAClone(const Graph& document, Evaluation& working)
{
	auto clone = std::make_shared<const Graph>(document.clone());
	SerialScheduler{}.run(*clone, working);
	return clone;
}

// An int in a slot of its own, for a binding.
static PortValue intValue(int v)
{
	PortValue value;
	value.set(v);
	return value;
}

// The one node inside an interior that is not its boundary pair — the scene's relay.
static NodeId bodyOf(const Graph& inner)
{
	for (const NodeId id : inner.nodeIds())
	{
		if (id != inner.boundaryInputNode().id() && id != inner.boundaryOutputNode().id())
			return id;
	}
	FAIL("the interior has no body node");
	return NodeId{};
}

// Everything `published` answers matches what `working` answers, at this level and every level
// below it — the same payload objects, not merely equal ones. Returns how many ports held a value,
// so a caller can require the comparison was not vacuous.
static std::size_t requireSameReads(const Graph& graph, const Evaluation& working, const Evaluation& published)
{
	std::size_t held = 0;
	for (const NodeId id : graph.nodeIds())
	{
		const Node& node = graph.node(id);
		INFO("node: " << node.name());
		REQUIRE(published.ready(id) == working.ready(id));
		REQUIRE(published.needsRecompute(id) == working.needsRecompute(id));

		const auto compare = [&](const Port& port)
		{
			const PortAddress address{id, port.id()};
			REQUIRE(published.value(address).samePayload(working.value(address)));
			REQUIRE(published.describe(address) == working.describe(address));
			if (!working.value(address).empty())
				++held;
		};
		for (std::size_t i = 0; i < node.inputCount(); ++i)
			compare(node.input(i));
		for (std::size_t o = 0; o < node.outputCount(); ++o)
			compare(node.output(o));

		if (const Graph* inner = node.innerGraph())
		{
			REQUIRE(published.childCount(id) == working.childCount(id));
			for (std::size_t e = 0; e < working.childCount(id); ++e)
			{
				// A child of its own, not a second name for the working one's.
				REQUIRE(&published.child(id, e) != &working.child(id, e));
				held += requireSameReads(*inner, working.child(id, e), published.child(id, e));
			}
		}
	}
	return held;
}

TEST_CASE("a published evaluation reads what its source read", "[flow][published]")
{
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);
	Evaluation working{document};
	const std::shared_ptr<const Graph> clone = runOnAClone(document, working);

	const PublishedEvaluation published{clone, working};
	const Evaluation& copy = published.evaluation();

	REQUIRE(copy.definition() == clone.get());
	REQUIRE(copy.childCount(s.group) == 1);
	REQUIRE(copy.childCount(s.map) == 3); // one per element — the whole child tree came across
	// Root, group interior and all three map elements, every port. The count is what keeps a copy
	// that silently dropped its values from passing as "empty equals empty".
	REQUIRE(requireSameReads(*clone, working, copy) > 10);
	REQUIRE(intOut(*clone, copy, s.sink) == 1);
}

TEST_CASE("a published evaluation is unaffected by the runs after it", "[flow][published]")
{
	// The reason it is a COPY rather than a view onto the working evaluation: the run after it
	// rebinds every slot it recomputes and resizes the map's children, and the panes must go on
	// showing what was published until the host publishes again.
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);
	Evaluation working{document};
	const std::shared_ptr<const Graph> first = runOnAClone(document, working);

	const PublishedEvaluation published{first, working};
	const PortAddress sinkOut{s.sink, document.node(s.sink).output(0).id()};
	const PortValue seen = working.value(sinkOut); // the payload that was published

	// Edit the document both ways a run can move a published value: a param, and the arity.
	auto& source = static_cast<Source&>(document.node(s.source));
	REQUIRE(source.setParam(source.value, 5));
	auto& list = static_cast<MakeInts&>(document.node(s.list));
	REQUIRE(list.setParam(list.values, Ints{9}));
	const std::shared_ptr<const Graph> second = runOnAClone(document, working);
	REQUIRE(intOut(*second, working, s.sink) == 5);
	REQUIRE(working.childCount(s.map) == 1);

	const Evaluation& copy = published.evaluation();
	REQUIRE(intOut(*first, copy, s.sink) == 1);
	REQUIRE(copy.value(sinkOut).samePayload(seen)); // the very object, not an equal one
	REQUIRE(test::output(*first, copy, s.map, 0).get<Ints>() == Ints{1, 2, 3});

	// Every element the run just dropped is still readable in the copy, with what it computed.
	const Graph& inner = *first->node(s.map).innerGraph();
	const NodeId relay = bodyOf(inner);
	REQUIRE(copy.childCount(s.map) == 3);
	for (std::size_t e = 0; e < 3; ++e)
		REQUIRE(intOut(inner, copy.child(s.map, e), relay) == static_cast<int>(e) + 1);
}

TEST_CASE("a published evaluation keeps the graph it reads through alive", "[flow][published]")
{
	Calls calls;
	Graph document;
	const Scene s = buildScene(document, calls);

	std::weak_ptr<const Graph> watch;
	PublishedEvaluation published;
	{
		Evaluation working{document};
		const std::shared_ptr<const Graph> clone = runOnAClone(document, working);
		watch = clone;
		published = PublishedEvaluation{clone, working};
	} // the run's clone loses its last other owner here, and the working evaluation goes with it

	REQUIRE_FALSE(watch.expired());
	const Evaluation& copy = published.evaluation();
	REQUIRE(copy.definition() == watch.lock().get());

	// Reads that go THROUGH the definition — a port's declared type, the node's required inputs —
	// at the root and inside an interior the clone's node owns.
	const PortAddress sinkOut{s.sink, document.node(s.sink).output(0).id()};
	REQUIRE(copy.ready(s.sink));
	REQUIRE(copy.describe(sinkOut) == "1");

	const Graph& inner = *document.node(s.group).innerGraph(); // same ids as the clone's interior
	const NodeId relay = bodyOf(inner);
	const Evaluation& child = copy.child(s.group);
	REQUIRE(child.ready(relay));
	REQUIRE(child.describe(PortAddress{relay, inner.node(relay).output(0).id()}) == "1");

	// And the hold is exactly as long as the copy's: replacing it lets the graph go.
	published = PublishedEvaluation{};
	REQUIRE(watch.expired());
}

TEST_CASE("publishing refuses an evaluation prepared against another graph", "[flow][published]")
{
	// The copy reads through evaluation.definition(), so that is the object that must be held.
	// Holding any other keeps the wrong graph alive while the copy reads one that may not outlive it.
	Calls calls;
	Graph document;
	buildScene(document, calls);
	Evaluation working{document};
	SerialScheduler{}.run(document, working); // last prepared against the DOCUMENT

	const auto clone = std::make_shared<const Graph>(document.clone());
	REQUIRE_THROWS_AS(PublishedEvaluation(clone, working), std::logic_error);
	REQUIRE_THROWS_AS(PublishedEvaluation(nullptr, working), std::logic_error);

	// Run against the clone, and the same pair is accepted.
	SerialScheduler{}.run(*clone, working);
	REQUIRE_NOTHROW(PublishedEvaluation(clone, working));
}

TEST_CASE("nothing published reads as empty", "[flow][published]")
{
	// A host has something to hand its panes before the first run has returned.
	Graph graph;
	const NodeId node = graph.add<ConstInt>(1);
	const PortAddress out{node, graph.node(node).output(0).id()};

	const PublishedEvaluation published;
	const Evaluation& nothing = published.evaluation();
	REQUIRE(nothing.definition() == nullptr);
	REQUIRE(nothing.value(out).empty());
	REQUIRE_FALSE(nothing.ready(node));
	REQUIRE(nothing.describe(out) == "(empty)");
	REQUIRE(nothing.childCount(node) == 0);
}

TEST_CASE("binding a published evaluation writes the copy, never its source", "[flow][published]")
{
	// A host shows a PENDING binding at once by writing it into what its panes read, while the same
	// value waits in a queue for the next run. The working evaluation belongs to a run meanwhile, so
	// the write must not reach it — and the next run, not this, is what makes the value real.
	Graph document;
	const PortId pin = document.boundaryInputNode().addBoundary<int>("x");
	const PortAddress bound{document.boundaryInputNode().id(), pin};
	std::atomic<int> relayCalls{0};
	const NodeId relay = document.add<Relay>(relayCalls);
	REQUIRE(document.connect(bound, PortAddress{relay, document.node(relay).input(0).id()}) == Connection::Ok);

	Evaluation working{document};
	working.bind(bound, intValue(1));
	const std::shared_ptr<const Graph> clone = runOnAClone(document, working);

	PublishedEvaluation published{clone, working};
	published.bind(bound, intValue(5));
	REQUIRE(published.evaluation().value(bound).get<int>() == 5);
	REQUIRE(working.value(bound).get<int>() == 1);
	// Nothing downstream moved: a copy is never run, so showing a binding computes nothing.
	REQUIRE(intOut(*clone, published.evaluation(), relay) == 1);

	// A pin added since the run the copy came from has no slot in it, and binding it is ignored
	// rather than refused — the queued value still reaches the next run, which prepares that slot.
	const PortId later = document.boundaryInputNode().addBoundary<int>("later");
	const PortAddress laterAddress{document.boundaryInputNode().id(), later};
	published.bind(laterAddress, intValue(9));
	REQUIRE(published.evaluation().value(laterAddress).empty());
}
