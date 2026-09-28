#pragma once

// A scene with BOTH kinds of child evaluation — a group's one and a map's N — beside two
// independent chains, shared by the suites that exercise M14's host-side machinery (ADR-0025):
// clones, published evaluations, and whatever runs against them next. Every node counts its own
// computes into a caller-owned counter held by REFERENCE, so a clone of a node counts into the same
// place as the original — which is what lets a test add up the work done across several clones.
//
// Kept in lain::flow::test beside testnodes.h, so a suite that needs the scene includes it rather
// than growing a copy.

#include "lain/flow/edit.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/node.h"
#include "lain/flow/porttyperegistry.h"
#include "testnodes.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace lain::flow::test
{
	using Ints = std::vector<int>;

	// The port types the scene's map lifts through: a map mirrors an inner Int as a list, which it
	// finds through the port-type registry (ADR-0014). Idempotent, so every case may call it.
	inline void registerSceneTypes()
	{
		static bool done = false;
		if (done)
			return;
		done = true;
		registerPortType<int>("Int");
		registerPortType<Ints>("ListOfInt");
	}

	// An int source whose value is a PARAM, so editing it is a recipe change (a version bump).
	struct Source : Node
	{
		std::atomic<int>& calls;
		PortId value, out;
		Source(std::atomic<int>& c, int initial)
			: Node("Source")
			, calls(c)
		{
			value = addParam<int>("value", initial);
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Source>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(param(value).get<int>());
		}
	};

	// Passes an int through, counted.
	struct Relay : Node
	{
		std::atomic<int>& calls;
		PortId in, out;
		explicit Relay(std::atomic<int>& c)
			: Node("Relay")
			, calls(c)
		{
			in = addInput<int>("in");
			out = addOutput<int>("out");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<Relay>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	// A whole collection, counted — what the map maps over. The list is a PARAM, so a test can edit
	// the map's arity the way a user would: as a recipe change.
	struct MakeInts : Node
	{
		std::atomic<int>& calls;
		PortId values, out;
		MakeInts(std::atomic<int>& c, Ints initial)
			: Node("MakeInts")
			, calls(c)
		{
			values = addParam<Ints>("values", std::move(initial));
			out = addOutput<Ints>("items");
		}
		std::unique_ptr<Node> clone() const override { return std::make_unique<MakeInts>(*this); }
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(param(values).get<Ints>());
		}
	};

	// Where each piece of work is counted.
	struct Calls
	{
		std::atomic<int> source{0};	 // the root source feeding the group
		std::atomic<int> inner{0};	 // the relay INSIDE the inline group
		std::atomic<int> sink{0};	 // the root relay downstream of the group
		std::atomic<int> list{0};	 // the collection the map maps over
		std::atomic<int> element{0}; // the relay inside the map, once per element
	};

	// source -> [group: relay] -> sink, and beside it list -> [map: relay]. Two independent chains, so
	// an edit to the first can be shown to leave the second alone.
	struct Scene
	{
		NodeId source;
		NodeId group;
		NodeId sink;
		NodeId list;
		NodeId map;
	};

	// Wire an interior `in -> body -> out` between its own boundary pins, then mirror its face.
	inline void buildInterior(Graph& parent, NodeId groupId, Graph& inner, std::unique_ptr<Node> body)
	{
		const PortId inPin = inner.boundaryInputNode().addBoundary<int>("x");
		const PortId outPin = inner.boundaryOutputNode().addBoundary<int>("y");
		const NodeId bodyId = inner.add(std::move(body));
		REQUIRE(inner.connect(PortAddress{inner.boundaryInputNode().id(), inPin},
							  PortAddress{bodyId, inner.node(bodyId).input(0).id()}) == Connection::Ok);
		REQUIRE(inner.connect(PortAddress{bodyId, inner.node(bodyId).output(0).id()},
							  PortAddress{inner.boundaryOutputNode().id(), outPin}) == Connection::Ok);
		edit::syncGroupPorts(parent, groupId);
	}

	inline Scene buildScene(Graph& graph, Calls& calls)
	{
		registerSceneTypes();
		Scene s{};
		s.source = graph.add<Source>(calls.source, 1);

		s.group = graph.add<InlineGroupNode>();
		auto& group = static_cast<InlineGroupNode&>(graph.node(s.group));
		buildInterior(graph, s.group, group.inner(), std::make_unique<Relay>(calls.inner));

		s.sink = graph.add<Relay>(calls.sink);
		REQUIRE(graph.connect(s.source, 0, s.group, 0) == Connection::Ok);
		REQUIRE(graph.connect(s.group, 0, s.sink, 0) == Connection::Ok);

		s.list = graph.add<MakeInts>(calls.list, Ints{1, 2, 3});
		s.map = graph.add<MapNode>();
		auto& map = static_cast<MapNode&>(graph.node(s.map));
		buildInterior(graph, s.map, map.inner(), std::make_unique<Relay>(calls.element));
		REQUIRE(graph.connect(s.list, 0, s.map, 0) == Connection::Ok);
		return s;
	}

	// A node's first output, as the int it carries.
	inline int intOut(const Graph& graph, const Evaluation& evaluation, NodeId node)
	{
		return output(graph, evaluation, node, 0).get<int>();
	}
} // namespace lain::flow::test
