// The map node (M8 slice 4, ADR-0014): a group whose interior is evaluated once per ELEMENT of a
// collection. This is the workload M6 and M7 were built for — one definition, N evaluations — so
// these drive the PRODUCTION schedulers rather than a test-only evaluator, and check values rather
// than structure: a plan wired plausibly but wrongly still passes a structural assertion.
//
// The rules under test, all from ADR-0014:
//   * an input SPLITS or BROADCASTS according to its own declared type — nothing is stored;
//   * outputs always GATHER, and one hole clears the whole output (a vector has no hole, and
//     shortening it would break the positional correspondence with the input);
//   * N == 0 is an empty vector, which is a VALUE, and not the same as "no collection";
//   * arity comes from a collection computed DURING the run, so the scheduler plans in stages.

#include "lain/flow/edit.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/porttyperegistry.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

#include <lain/task/task.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <vector>

using namespace lain::flow;

namespace
{
	using Ints = std::vector<int>;

	// The list types must be REGISTERED for a map to lift a pin to them: a PortType knows its
	// element type, but nothing can walk that backwards (naming std::vector<T> needs T at compile
	// time). Registered once, on first use, exactly as an app does at startup.
	void registerMapTypes()
	{
		static bool done = false;
		if (done)
			return;
		done = true;
		registerPortType<int>("Int");
		registerPortType<Ints>("ListOfInt");
		registerPortType<std::string>("String");
	}

	// A boundary pin is addressed by the PortId its declaration minted, while an ordinary node's
	// ports read most clearly by position — so these two spell the mixed case Graph::connect has no
	// overload for.
	Connection wire(Graph& g, NodeId from, PortId fromPin, NodeId to, std::size_t toIndex)
	{
		return g.connect(PortAddress{from, fromPin}, PortAddress{to, g.node(to).input(toIndex).id()});
	}

	Connection wire(Graph& g, NodeId from, std::size_t fromIndex, NodeId to, PortId toPin)
	{
		return g.connect(PortAddress{from, g.node(from).output(fromIndex).id()}, PortAddress{to, toPin});
	}

	// Emits a whole collection — the thing a map maps over. Counted, so a test can prove how often
	// the producer actually ran across the stages of one invocation.
	struct MakeInts : Node
	{
		Ints values;
		std::atomic<int>* calls = nullptr;
		PortId out;
		explicit MakeInts(Ints v, std::atomic<int>* c = nullptr)
			: Node("MakeInts")
			, values(std::move(v))
			, calls(c)
		{
			out = addOutput<Ints>("items");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			if (calls != nullptr)
				++*calls;
			evaluation.output(out).set(values);
		}
	};

	// One element in, one element out — the body of a map's interior.
	struct AddOne : Node
	{
		PortId in, out;
		AddOne()
			: Node("AddOne")
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(evaluation.input(in).get<int>() + 1);
		}
	};

	// Adds its two inputs — used to prove a BROADCAST input reaches every element.
	struct AddTwo : Node
	{
		PortId a, b, out;
		AddTwo()
			: Node("AddTwo")
		{
			a = addInput<int>("a");
			b = addInput<int>("b");
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(evaluation.input(a).get<int>() + evaluation.input(b).get<int>());
		}
	};

	// Passes a value through, but produces NOTHING for one chosen value — a hole in the gather.
	struct HoleAt : Node
	{
		int skip;
		PortId in, out;
		explicit HoleAt(int s)
			: Node("HoleAt")
			, skip(s)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			const int value = evaluation.input(in).get<int>();
			if (value == skip)
				evaluation.output(out).clear(); // this element delivers nothing
			else
				evaluation.output(out).set(value);
		}
	};

	// Passes an int through and counts its runs — for asking whether an element recomputed.
	struct CountingInt : Node
	{
		std::atomic<int>& calls;
		PortId in, out;
		explicit CountingInt(std::atomic<int>& c)
			: Node("CountingInt")
			, calls(c)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// Reads a whole collection — a consumer downstream of the map.
	struct SumInts : Node
	{
		PortId in, out;
		SumInts()
			: Node("SumInts")
		{
			in = addInput<Ints>("items");
			out = addOutput<int>("sum");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			int total = 0;
			for (const int v : evaluation.input(in).get<Ints>())
				total += v;
			evaluation.output(out).set(total);
		}
	};

	// A map whose interior is `body`, mirrored (and so LIFTED) onto its own face. Returns its id.
	// The interior's boundary pins are declared first, then edit::syncGroupPorts derives the outer
	// ports — the same route the host takes, rather than a hand-built face a test could get wrong.
	struct MapFixture
	{
		NodeId map;
		PortId itemsIn;	  // the map's outer split input   (vector<int>)
		PortId resultOut; // the map's outer gathered output (vector<int>)
	};

	template <typename Body>
	MapFixture buildMap(Graph& graph)
	{
		const NodeId id = graph.add<MapNode>();
		auto& map = static_cast<MapNode&>(graph.node(id));

		const PortId inPin = map.inner().boundaryInputNode().addBoundary<int>("item");
		const PortId outPin = map.inner().boundaryOutputNode().addBoundary<int>("result");
		const NodeId body = map.inner().add<Body>();
		REQUIRE(wire(map.inner(), map.inner().boundaryInputNode().id(), inPin, body, 0) == Connection::Ok);
		REQUIRE(wire(map.inner(), body, 0, map.inner().boundaryOutputNode().id(), outPin) == Connection::Ok);

		edit::syncGroupPorts(graph, id);
		return MapFixture{id, map.input(0).id(), map.output(0).id()};
	}
} // namespace

TEST_CASE("a map lifts its interior's pins to collections", "[flow][map]")
{
	registerMapTypes();
	Graph graph;
	const MapFixture f = buildMap<AddOne>(graph);
	const Node& map = graph.node(f.map);

	// The interior takes an int and delivers an int; the FACE takes and delivers vector<int>. That
	// lift is the whole difference between a map and a group.
	REQUIRE(map.inputCount() == 1);
	REQUIRE(map.outputCount() == 1);
	REQUIRE(map.input(0).type() == std::type_index(typeid(Ints)));
	REQUIRE(map.output(0).type() == std::type_index(typeid(Ints)));
	REQUIRE(map.input(0).name() == "item"); // the pin's own name, unchanged by lifting
}

TEST_CASE("a map runs its interior once per element", "[flow][map]")
{
	registerMapTypes();
	Graph graph;
	const MapFixture f = buildMap<AddOne>(graph);
	const NodeId source = graph.add<MakeInts>(Ints{1, 2, 3});
	const NodeId sink = graph.add<SumInts>();
	REQUIRE(graph.connect(source, 0, f.map, 0) == Connection::Ok);
	REQUIRE(graph.connect(f.map, 0, sink, 0) == Connection::Ok);

	Evaluation evaluation{graph};

	auto check = [&]
	{
		// Each element went through the interior separately, and the results came back in order.
		const Ints expected{2, 3, 4};
		REQUIRE(evaluation.value(PortAddress{f.map, f.resultOut}).get<Ints>() == expected);
		REQUIRE(test::output(graph, evaluation, sink, 0).get<int>() == 9);
		REQUIRE(evaluation.childCount(f.map) == 3); // one evaluation per element
	};

	SECTION("serial")
	{
		SerialScheduler{}.run(graph, evaluation);
		check();
	}

	SECTION("parallel")
	{
		// Serial and parallel must agree: this is where N evaluations of ONE definition genuinely
		// run at once, which is the contract M6 built and M8 is the first real user of.
		lain::task::Executor executor;
		ParallelScheduler{executor}.run(graph, evaluation);
		check();
	}
}

TEST_CASE("an input broadcasts when it is declared un-lifted", "[flow][map]")
{
	// Split vs broadcast is read off the DECLARATION (ADR-0014): a vector<int> outer port against an
	// int inner pin splits; an int outer port against an int inner pin hands the same value to every
	// element. Nothing is stored, so nothing can disagree with the port.
	registerMapTypes();
	Graph graph;
	const NodeId id = graph.add<MapNode>();
	auto& map = static_cast<MapNode&>(graph.node(id));

	const PortId itemPin = map.inner().boundaryInputNode().addBoundary<int>("item");
	const PortId offsetPin = map.inner().boundaryInputNode().addBoundary<int>("offset");
	const PortId outPin = map.inner().boundaryOutputNode().addBoundary<int>("result");
	const NodeId body = map.inner().add<AddTwo>();
	REQUIRE(wire(map.inner(), map.inner().boundaryInputNode().id(), itemPin, body, 0) == Connection::Ok);
	REQUIRE(wire(map.inner(), map.inner().boundaryInputNode().id(), offsetPin, body, 1) == Connection::Ok);
	REQUIRE(wire(map.inner(), body, 0, map.inner().boundaryOutputNode().id(), outPin) == Connection::Ok);

	// `item` is mirrored lifted (split); `offset` is declared un-lifted by hand, which is what a
	// host's "broadcast this pin" gesture will do.
	map.exposePort(Port::Direction::Input, *map.inner().boundaryInputNode().findOutput(itemPin));
	map.exposeInput<int>("offset", offsetPin);
	map.exposePort(Port::Direction::Output, *map.inner().boundaryOutputNode().findInput(outPin));

	const NodeId items = graph.add<MakeInts>(Ints{10, 20, 30});
	const NodeId offset = graph.add<test::ConstInt>(5);
	REQUIRE(graph.connect(items, 0, id, 0) == Connection::Ok);
	REQUIRE(graph.connect(offset, 0, id, 1) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	const Ints expected{15, 25, 35}; // the one offset reached every element
	REQUIRE(evaluation.value(PortAddress{id, map.output(0).id()}).get<Ints>() == expected);

	SECTION("and syncGroupPorts leaves the broadcast choice alone")
	{
		// Mirroring is by PortId, so an already-mapped pin is never re-typed — which is what lets
		// the choice survive the per-frame reconciliation a host runs.
		edit::syncGroupPorts(graph, id);
		REQUIRE(graph.node(id).input(1).type() == std::type_index(typeid(int)));
	}
}

TEST_CASE("an empty collection maps to an empty collection", "[flow][map]")
{
	// N == 0 is an ANSWER, not a failure: empty in, empty vector out. A consumer must be able to
	// tell it from "the map produced nothing at all", which is what a hole or an unrunnable map do.
	registerMapTypes();
	Graph graph;
	const MapFixture f = buildMap<AddOne>(graph);
	const NodeId source = graph.add<MakeInts>(Ints{});
	REQUIRE(graph.connect(source, 0, f.map, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	const PortValue& result = evaluation.value(PortAddress{f.map, f.resultOut});
	REQUIRE_FALSE(result.empty()); // a value, not an absence
	REQUIRE(result.get<Ints>().empty());
	REQUIRE(evaluation.childCount(f.map) == 0);
}

TEST_CASE("one suppressed element clears the whole output", "[flow][map]")
{
	// A std::vector<int> cannot hold a hole, and gathering only the survivors would break the
	// positional correspondence between the input collection and this one — so the map delivers
	// NOTHING, and ADR-0007's ordinary emptiness rule suppresses downstream with no new machinery.
	registerMapTypes();
	Graph graph;
	const NodeId id = graph.add<MapNode>();
	auto& map = static_cast<MapNode&>(graph.node(id));
	const PortId inPin = map.inner().boundaryInputNode().addBoundary<int>("item");
	const PortId outPin = map.inner().boundaryOutputNode().addBoundary<int>("result");
	const NodeId body = map.inner().add<HoleAt>(2); // element with value 2 delivers nothing
	REQUIRE(wire(map.inner(), map.inner().boundaryInputNode().id(), inPin, body, 0) == Connection::Ok);
	REQUIRE(wire(map.inner(), body, 0, map.inner().boundaryOutputNode().id(), outPin) == Connection::Ok);
	edit::syncGroupPorts(graph, id);

	const NodeId source = graph.add<MakeInts>(Ints{1, 2, 3});
	const NodeId sink = graph.add<SumInts>();
	REQUIRE(graph.connect(source, 0, id, 0) == Connection::Ok);
	REQUIRE(graph.connect(id, 0, sink, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	REQUIRE(evaluation.value(PortAddress{id, graph.node(id).output(0).id()}).empty());
	REQUIRE(test::output(graph, evaluation, sink, 0).empty()); // and the emptiness propagated

	// Which element failed is readable from the child evaluations, so the engine needs no reporting
	// channel of its own — flow core stays log-free and a host can point at element 1.
	REQUIRE(evaluation.childCount(id) == 3);
	const NodeId innerOut = map.inner().boundaryOutputNode().id();
	REQUIRE(evaluation.child(id, 0).value(PortAddress{innerOut, outPin}).empty() == false);
	REQUIRE(evaluation.child(id, 1).value(PortAddress{innerOut, outPin}).empty());
	REQUIRE(evaluation.child(id, 2).value(PortAddress{innerOut, outPin}).empty() == false);
}

TEST_CASE("a map with no value to map over produces nothing", "[flow][map]")
{
	// Its required input carries no value, so ADR-0007's ordinary gate applies and the map is
	// suppressed — distinct from the empty-collection case above, which is a value.
	registerMapTypes();
	Graph graph;
	const MapFixture f = buildMap<AddOne>(graph);
	// deliberately unconnected input

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	REQUIRE(evaluation.value(PortAddress{f.map, f.resultOut}).empty());
	REQUIRE(evaluation.childCount(f.map) == 0);
}

TEST_CASE("a map re-sizes when its collection changes length", "[flow][map]")
{
	registerMapTypes();
	Graph graph;
	const MapFixture f = buildMap<AddOne>(graph);
	const NodeId source = graph.add<MakeInts>(Ints{1, 2, 3});
	REQUIRE(graph.connect(source, 0, f.map, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler sched;
	sched.run(graph, evaluation);
	REQUIRE(evaluation.childCount(f.map) == 3);

	// Shorten the collection: the tail children go, and their retained values with them.
	static_cast<MakeInts&>(graph.node(source)).values = Ints{7};
	evaluation.requestRecompute(source);
	sched.run(graph, evaluation);

	const Ints expected{8};
	REQUIRE(evaluation.value(PortAddress{f.map, f.resultOut}).get<Ints>() == expected);
	REQUIRE(evaluation.childCount(f.map) == 1);
}

TEST_CASE("a map does not re-run its producer across stages", "[flow][map]")
{
	// Staging plans the run more than once, but must not EXECUTE anything twice: the producer runs
	// in stage 1, the elements in stage 2. A stage that re-ran already-computed work would make
	// every map cost double, and an on-request source would deliver a different value per stage.
	registerMapTypes();
	Graph graph;
	std::atomic<int> calls{0};
	const MapFixture f = buildMap<AddOne>(graph);
	const NodeId source = graph.add<MakeInts>(Ints{1, 2}, &calls);
	REQUIRE(graph.connect(source, 0, f.map, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	REQUIRE(calls == 1);
	const Ints expected{2, 3};
	REQUIRE(evaluation.value(PortAddress{f.map, f.resultOut}).get<Ints>() == expected);
}

TEST_CASE("a map inside a map costs one more stage and nothing else", "[flow][map]")
{
	// ADR-0014 claims nesting falls out of staging rather than needing anything: the outer map is
	// deferred, prepared, and expanded; each of its elements then contains an inner map that is
	// itself deferred, prepared and expanded. Worth testing rather than asserting, because each
	// element's inner map is a DIFFERENT evaluation of one definition — exactly the case the
	// {definition, evaluation, node} frontier address exists for.
	registerMapTypes();
	registerPortType<std::vector<Ints>>("ListOfListOfInt");

	Graph graph;

	// outer map: vector<vector<int>> -> vector<vector<int>>, its interior a map over vector<int>.
	const NodeId outerId = graph.add<MapNode>();
	auto& outer = static_cast<MapNode&>(graph.node(outerId));
	const PortId outerIn = outer.inner().boundaryInputNode().addBoundary<Ints>("row");
	const PortId outerOut = outer.inner().boundaryOutputNode().addBoundary<Ints>("row");

	const NodeId innerId = outer.inner().add<MapNode>();
	auto& innerMap = static_cast<MapNode&>(outer.inner().node(innerId));
	const PortId innerIn = innerMap.inner().boundaryInputNode().addBoundary<int>("item");
	const PortId innerOut = innerMap.inner().boundaryOutputNode().addBoundary<int>("result");
	const NodeId body = innerMap.inner().add<AddOne>();
	REQUIRE(wire(innerMap.inner(), innerMap.inner().boundaryInputNode().id(), innerIn, body, 0) == Connection::Ok);
	REQUIRE(wire(innerMap.inner(), body, 0, innerMap.inner().boundaryOutputNode().id(), innerOut) == Connection::Ok);
	edit::syncGroupPorts(outer.inner(), innerId);

	// Wire the outer map's interior: its row pin feeds the inner map, whose result is the row out.
	REQUIRE(outer.inner().connect(PortAddress{outer.inner().boundaryInputNode().id(), outerIn},
								  PortAddress{innerId, outer.inner().node(innerId).input(0).id()}) == Connection::Ok);
	REQUIRE(outer.inner().connect(PortAddress{innerId, outer.inner().node(innerId).output(0).id()},
								  PortAddress{outer.inner().boundaryOutputNode().id(), outerOut}) == Connection::Ok);
	edit::syncGroupPorts(graph, outerId);

	struct MakeRows : Node
	{
		PortId out;
		MakeRows()
			: Node("MakeRows")
		{
			out = addOutput<std::vector<Ints>>("rows");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(std::vector<Ints>{{1, 2}, {10}});
		}
	};
	const NodeId source = graph.add<MakeRows>();
	REQUIRE(graph.connect(source, 0, outerId, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler{}.run(graph, evaluation);

	const std::vector<Ints> expected{{2, 3}, {11}};
	REQUIRE(evaluation.value(PortAddress{outerId, graph.node(outerId).output(0).id()}).get<std::vector<Ints>>() == expected);
	REQUIRE(evaluation.childCount(outerId) == 2);
	// Each ROW is its own evaluation of the inner map, with its own element count.
	REQUIRE(evaluation.child(outerId, 0).childCount(innerId) == 2);
	REQUIRE(evaluation.child(outerId, 1).childCount(innerId) == 1);
}

TEST_CASE("a map keeps its elements' work across runs", "[flow][map]")
{
	// Each element's Evaluation is retained, so a second run with nothing stale recomputes nothing —
	// the same incrementality a group has, N times over. This is what prepare's "keep the children
	// you have" rule protects.
	registerMapTypes();
	Graph graph;
	const NodeId id = graph.add<MapNode>();
	auto& map = static_cast<MapNode&>(graph.node(id));
	std::atomic<int> bodyCalls{0};

	const PortId inPin = map.inner().boundaryInputNode().addBoundary<int>("item");
	const PortId outPin = map.inner().boundaryOutputNode().addBoundary<int>("result");
	const NodeId body = map.inner().add<CountingInt>(bodyCalls);
	REQUIRE(wire(map.inner(), map.inner().boundaryInputNode().id(), inPin, body, 0) == Connection::Ok);
	REQUIRE(wire(map.inner(), body, 0, map.inner().boundaryOutputNode().id(), outPin) == Connection::Ok);
	edit::syncGroupPorts(graph, id);

	const NodeId source = graph.add<MakeInts>(Ints{1, 2, 3});
	REQUIRE(graph.connect(source, 0, id, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	SerialScheduler sched;
	sched.run(graph, evaluation);
	REQUIRE(bodyCalls == 3); // once per element

	sched.run(graph, evaluation); // nothing stale
	REQUIRE(bodyCalls == 3);	  // every element kept its result
}
