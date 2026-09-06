// The loop node (M11, ADR-0021): a group whose interior is evaluated once per ITERATION, each
// pass's carried outputs seeding the next one's inputs.
//
// Two halves. The STRUCTURAL cases (slice 2) drive the real edit::syncGroupPorts against a real
// interior rather than asserting about a hand-built port list:
//   * a loop's FACE is derived exactly as a group's, except for two refusals;
//   * the two RESERVED pins are the engine's, skipped BY ID so a rename cannot change behaviour;
//   * a CARRY is a pair, created only by a paired gesture, so half of one cannot be authored;
//   * a loop owns ports no group does, so it is the first kind where an inner pin's name can
//     collide with one — refused, and reported, rather than silently degrading (or asserting).
//
// The FOLD cases (slice 4) run one through the PRODUCTION schedulers and check values, because a
// plan wired plausibly but wrongly still passes a structural assertion:
//   * a carried value seeds the next iteration, and only the last one survives;
//   * the fold is BOUNDED by construction, and `continue` breaks out early — `iterations` is what
//     makes "converged at 3" distinguishable from "hit the bound";
//   * count == 0 is the fold IDENTITY (every carry delivers its seed), never a failure;
//   * a suppressed carry, or a loop that cannot run at all, clears the whole output;
//   * nesting works both ways round, and a map inside a loop re-prepares every iteration.

#include "lain/flow/edit.h"
#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"
#include "lain/flow/nodes/constant.h"
#include "lain/flow/porttyperegistry.h"
#include "lain/flow/scheduler.h"
#include "testnodes.h"

#include <lain/task/task.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

using namespace lain::flow;

namespace
{
	// A loop seated in a parent graph — the only shape syncGroupPorts works on.
	struct Scene
	{
		Graph parent;
		NodeId id;

		Scene()
			: id(parent.add<LoopNode>())
		{
		}

		LoopNode& loop() { return static_cast<LoopNode&>(parent.node(id)); }
		edit::GroupSync sync() { return edit::syncGroupPorts(parent, id); }
	};

	// Whether a node declares a port of that name on that side — the question every "did it mirror?"
	// assertion is really asking.
	bool has(const Node& node, Port::Direction side, const std::string& name)
	{
		return node.hasPortNamed(side, name);
	}

	bool listed(const std::vector<std::string>& names, const std::string& name)
	{
		return std::find(names.begin(), names.end(), name) != names.end();
	}

	//=========================================================================
	// Fold fixtures — what a RUNNING loop is built from
	//=========================================================================

	// int -> int + 1: the body of the simplest possible fold. Counted, so a test can prove how
	// often the interior actually ran across the stages of one invocation.
	struct AddOne : Node
	{
		int* calls = nullptr;
		PortId in, out;
		explicit AddOne(int* c = nullptr)
			: Node("AddOne")
			, calls(c)
		{
			in = addInput<int>("x");
			out = addOutput<int>("x");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			if (calls != nullptr)
				++*calls;
			evaluation.output(out).set(evaluation.input(in).get<int>() + 1);
		}
	};

	// int -> bool: the condition a WHILE loop wires into the reserved `continue` pin.
	struct Below : Node
	{
		int limit;
		PortId in, out;
		explicit Below(int l)
			: Node("Below")
			, limit(l)
		{
			in = addInput<int>("x");
			out = addOutput<bool>("below");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			evaluation.output(out).set(evaluation.input(in).get<int>() < limit);
		}
	};

	// int -> nothing, once the value reaches `at`: a body that SUPPRESSES itself, which is how an
	// iteration fails — ADR-0007's emptiness arriving at the engine.
	struct StopAt : Node
	{
		int at;
		PortId in, out;
		explicit StopAt(int a)
			: Node("StopAt")
			, at(a)
		{
			in = addInput<int>("x");
			out = addOutput<int>("x");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			const int x = evaluation.input(in).get<int>();
			if (x < at)
				evaluation.output(out).set(x + 1);
			else
				evaluation.output(out).clear();
		}
	};

	// Whatever int arrives, straight back out — the way a test reads the reserved `index` pin.
	struct PassInt : Node
	{
		PortId in, out;
		PassInt()
			: Node("PassInt")
		{
			in = addInput<int>("x");
			out = addOutput<int>("x");
		}
		void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).set(evaluation.input(in).get<int>()); }
	};

	// An int output that is never set: an upstream that produced nothing, so whatever it feeds is
	// not READY and does not run at all.
	struct Nothing : Node
	{
		PortId out;
		Nothing()
			: Node("Nothing")
		{
			out = addOutput<int>("x");
		}
		void compute(NodeEvaluation& evaluation) const override { evaluation.output(out).clear(); }
	};

	// int -> a list holding it twice, and a list -> its sum: the two ends of a map inside a loop,
	// so that what the map maps over CHANGES with every iteration.
	struct PairOf : Node
	{
		PortId in, out;
		PairOf()
			: Node("PairOf")
		{
			in = addInput<int>("x");
			out = addOutput<std::vector<int>>("pair");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			const int x = evaluation.input(in).get<int>();
			evaluation.output(out).set(std::vector<int>{x, x});
		}
	};

	struct SumInts : Node
	{
		PortId in, out;
		SumInts()
			: Node("SumInts")
		{
			in = addInput<std::vector<int>>("items");
			out = addOutput<int>("sum");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			int total = 0;
			for (const int value : evaluation.input(in).get<std::vector<int>>())
				total += value;
			evaluation.output(out).set(total);
		}
	};

	// The list type must be REGISTERED for a map to lift a pin to it (ADR-0014). Registered once,
	// on first use, exactly as an app does at startup; a re-register replaces, so sharing the keys
	// with the map suite is harmless.
	void registerLoopTypes()
	{
		registerPortType<int>("Int");
		registerPortType<std::vector<int>>("ListOfInt");
	}

	PortId inputNamed(const Node& node, const std::string& name)
	{
		for (std::size_t i = 0; i < node.inputCount(); ++i)
		{
			if (node.input(i).name() == name)
				return node.input(i).id();
		}
		return PortId{};
	}

	PortId outputNamed(const Node& node, const std::string& name)
	{
		for (std::size_t i = 0; i < node.outputCount(); ++i)
		{
			if (node.output(i).name() == name)
				return node.output(i).id();
		}
		return PortId{};
	}

	// A loop that folds ONE carried int, seated in a parent graph — the shape nearly every fold
	// case below is a variation of:
	//
	//     parent:   seed -> loop."value",  bound -> loop."count",  loop."value" -> read here
	//     interior: GroupInput."value" -> body -> GroupOutput."value"
	//
	// Both sources are ConstantNodes, so a test can change one and run again: a param write goes
	// through the node's own seam and invalidates as part of the write.
	struct Fold
	{
		Graph parent;
		NodeId loopId;
		NodeId seedId;
		NodeId boundId;
		LoopNode::Carry carry;

		Fold(int seed, int trips)
			: loopId(parent.add<LoopNode>())
			, seedId(parent.add<ConstantNode<int>>(seed))
			, boundId(parent.add<ConstantNode<int>>(trips))
		{
			carry = loop().addCarry<int>("value");
			REQUIRE(carry.innerIn != PortId{});
		}

		LoopNode& loop() { return static_cast<LoopNode&>(parent.node(loopId)); }
		Graph& inner() { return loop().inner(); }
		NodeId innerIn() { return inner().boundaryInputNode().id(); }
		NodeId innerOut() { return inner().boundaryOutputNode().id(); }

		// GroupInput."value" -> body -> GroupOutput."value": the carry closed through a body.
		void carryThrough(NodeId body, std::size_t bodyIn = 0, std::size_t bodyOut = 0)
		{
			REQUIRE(inner().connect(PortAddress{innerIn(), carry.innerIn},
									PortAddress{body, inner().node(body).input(bodyIn).id()}) == Connection::Ok);
			REQUIRE(inner().connect(PortAddress{body, inner().node(body).output(bodyOut).id()},
									PortAddress{innerOut(), carry.innerOut}) == Connection::Ok);
		}

		// Derive the loop's face from the interior, then feed it: the seed onto the carried input
		// and the trip bound onto `count`. Called once, after every inner pin exists.
		void close(bool wireSeed = true)
		{
			edit::syncGroupPorts(parent, loopId);
			if (wireSeed)
			{
				REQUIRE(parent.connect(PortAddress{seedId, parent.node(seedId).output(0).id()},
									   PortAddress{loopId, inputNamed(parent.node(loopId), "value")}) == Connection::Ok);
			}
			REQUIRE(parent.connect(PortAddress{boundId, parent.node(boundId).output(0).id()},
								   PortAddress{loopId, loop().countPort()}) == Connection::Ok);
		}

		const PortValue& out(const Evaluation& evaluation, const std::string& name)
		{
			return evaluation.value(PortAddress{loopId, outputNamed(parent.node(loopId), name)});
		}

		const PortValue& iterations(const Evaluation& evaluation)
		{
			return evaluation.value(PortAddress{loopId, loop().iterationsPort()});
		}
	};

} // namespace

TEST_CASE("a fresh loop owns a bound and a report, and nothing else", "[flow][loop]")
{
	Scene scene;
	LoopNode& loop = scene.loop();

	// The two node-owned ports, and no mirrored ones: the interior has only its reserved pins.
	REQUIRE(loop.inputCount() == 1);
	REQUIRE(loop.outputCount() == 1);
	REQUIRE(loop.input(loop.countPort()).name() == "count");
	REQUIRE(loop.output(loop.iterationsPort()).name() == "iterations");

	// Bounded by CONSTRUCTION — there is no unbounded state to represent, so nothing has to refuse
	// one at runtime. Default 1, so a fresh loop behaves exactly like a group.
	const Param* bound = loop.defaultOf(loop.countPort());
	REQUIRE(bound != nullptr);
	REQUIRE(bound->get<int>() == 1);

	// The structural seam slice 1 built, now with something that answers it.
	REQUIRE(loop.interiorEvaluation() == InteriorEvaluation::PerIteration);
	// A loop OWNS its interior, so a host may edit through it (without this, every pane is
	// read-only inside a loop — M8 slice 6c, found in map-shaped form).
	REQUIRE(loop.editableInner() == loop.innerGraph());
	REQUIRE(loop.editableInner() != nullptr);
}

TEST_CASE("the interior is born with the two pins the engine talks through", "[flow][loop]")
{
	Scene scene;
	LoopNode& loop = scene.loop();
	const GroupInputNode& into = loop.inner().boundaryInputNode();
	const GroupOutputNode& from = loop.inner().boundaryOutputNode();

	REQUIRE(into.findOutput(loop.indexPin()) != nullptr);
	REQUIRE(into.output(loop.indexPin()).name() == "index");
	REQUIRE(from.findInput(loop.continuePin()) != nullptr);
	REQUIRE(from.input(loop.continuePin()).name() == "continue");

	// `continue` defaults to TRUE, which is what separates "nobody wired it" from "the thing wired
	// to it was suppressed" — the same empty slot with opposite meanings, and the trap
	// GateNode::enable hit on 2026-08-15. A default seeds only an UNCONNECTED input.
	const Param* fallback = from.defaultOf(loop.continuePin());
	REQUIRE(fallback != nullptr);
	REQUIRE(fallback->get<bool>() == true);
}

TEST_CASE("the reserved pins are static, so serialization never replays them", "[flow][loop]")
{
	// Pinned HERE rather than at the serialization slice, where a replay double-adding onto a pin
	// the constructor already made would surface as an unrelated-looking load issue. Static is what
	// makes the constructor the single builder of these two pins.
	Scene scene;
	LoopNode& loop = scene.loop();
	REQUIRE_FALSE(loop.inner().boundaryInputNode().output(loop.indexPin()).isDynamic());
	REQUIRE_FALSE(loop.inner().boundaryOutputNode().input(loop.continuePin()).isDynamic());

	// A user-added boundary pin IS dynamic — the distinction has to be real, not incidental.
	const PortId added = loop.inner().boundaryInputNode().addBoundary<int>("seedish");
	REQUIRE(loop.inner().boundaryInputNode().output(added).isDynamic());
}

TEST_CASE("neither reserved pin is mirrored, and renaming one does not change that", "[flow][loop]")
{
	Scene scene;
	LoopNode& loop = scene.loop();

	edit::GroupSync first = scene.sync();
	REQUIRE(first.added == 0);
	REQUIRE(loop.inputCount() == 1);  // still just `count`
	REQUIRE(loop.outputCount() == 1); // still just `iterations`

	// Skipped BY ID, never by name: pairing on the name would mean a rename silently turns a while
	// loop into a count loop — no error, just a different answer.
	loop.inner().boundaryInputNode().output(loop.indexPin()).setName("i");
	loop.inner().boundaryOutputNode().input(loop.continuePin()).setName("keepGoing");

	edit::GroupSync second = scene.sync();
	REQUIRE(second.added == 0);
	// Not a candidate, so not a REFUSAL either — a loop must not report its own reserved pins as
	// problems on every pass for the whole life of the document.
	REQUIRE(second.refused.empty());
	REQUIRE(first.refused.empty());
	REQUIRE(loop.inputCount() == 1);
	REQUIRE(loop.outputCount() == 1);
	REQUIRE_FALSE(has(loop, Port::Direction::Input, "i"));
	REQUIRE_FALSE(has(loop, Port::Direction::Output, "keepGoing"));
}

TEST_CASE("a carry mirrors out as a seed input AND a final output of the same name", "[flow][loop]")
{
	Scene scene;
	LoopNode& loop = scene.loop();

	const LoopNode::Carry carry = loop.addCarry<int>("total");
	REQUIRE(carry.innerIn != PortId{});
	REQUIRE(carry.innerOut != PortId{});
	REQUIRE(loop.carries().size() == 1);
	REQUIRE(loop.carries().at(carry.innerIn) == carry.innerOut);
	REQUIRE(loop.inner().boundaryInputNode().output(carry.innerIn).name() == "total");
	REQUIRE(loop.inner().boundaryOutputNode().input(carry.innerOut).name() == "total");

	edit::GroupSync sync = scene.sync();
	REQUIRE(sync.added == 2);
	REQUIRE(sync.refused.empty());

	// The first node in the tree where an outer input and an outer output share a name BY DESIGN.
	// hasPortNamed is per-direction, so it is unambiguous — but it has never been deliberate before.
	REQUIRE(has(loop, Port::Direction::Input, "total"));
	REQUIRE(has(loop, Port::Direction::Output, "total"));
	REQUIRE(loop.inputCount() == 2);
	REQUIRE(loop.outputCount() == 2);
}

TEST_CASE("an unpaired inner pin mirrors as one port — invariant in, last out", "[flow][loop]")
{
	Scene scene;
	LoopNode& loop = scene.loop();
	loop.inner().boundaryInputNode().addBoundary<int>("scale");	  // read every iteration, never written
	loop.inner().boundaryOutputNode().addBoundary<int>("report"); // written every iteration, never read

	edit::GroupSync sync = scene.sync();
	REQUIRE(sync.added == 2);
	REQUIRE(loop.carries().empty()); // nothing pairs them, and nothing invents a pairing

	REQUIRE(has(loop, Port::Direction::Input, "scale"));
	REQUIRE_FALSE(has(loop, Port::Direction::Output, "scale"));
	REQUIRE(has(loop, Port::Direction::Output, "report"));
	REQUIRE_FALSE(has(loop, Port::Direction::Input, "report"));
}

TEST_CASE("addCarry refuses a taken name and touches NEITHER side", "[flow][loop]")
{
	Scene scene;
	LoopNode& loop = scene.loop();
	GroupInputNode& into = loop.inner().boundaryInputNode();
	GroupOutputNode& from = loop.inner().boundaryOutputNode();

	SECTION("taken on the input side")
	{
		into.addBoundary<int>("total");
		REQUIRE(loop.addCarry<int>("total").innerIn == PortId{});
		REQUIRE(loop.carries().empty());
		// The whole point: the OTHER side must not have grown a lone pin.
		REQUIRE_FALSE(has(from, Port::Direction::Input, "total"));
	}

	SECTION("taken on the output side")
	{
		from.addBoundary<int>("total");
		REQUIRE(loop.addCarry<int>("total").innerIn == PortId{});
		REQUIRE(loop.carries().empty());
		REQUIRE_FALSE(has(into, Port::Direction::Output, "total"));
	}

	SECTION("not a valid port name")
	{
		REQUIRE(loop.addCarry<int>("2fast").innerIn == PortId{});
		REQUIRE(loop.carries().empty());
		REQUIRE(into.outputCount() == 1); // just `index`
		REQUIRE(from.inputCount() == 1);  // just `continue`
	}
}

TEST_CASE("an inner pin colliding with a loop's own port is refused and named", "[flow][loop]")
{
	// A loop is the first group kind with ports of its own, so it is the first that can hit this.
	// Without the guard, addInputLike's duplicate-name assert fires.
	Scene scene;
	LoopNode& loop = scene.loop();
	loop.inner().boundaryInputNode().addBoundary<int>("count");
	loop.inner().boundaryOutputNode().addBoundary<int>("iterations");

	edit::GroupSync sync = scene.sync();
	REQUIRE(sync.added == 0);
	REQUIRE(sync.refused.size() == 2);
	REQUIRE(listed(sync.refused, "count"));
	REQUIRE(listed(sync.refused, "iterations"));

	// The node keeps ITS ports — the refusal costs the inner pin its mirror, nothing else.
	REQUIRE(loop.inputCount() == 1);
	REQUIRE(loop.outputCount() == 1);
	REQUIRE(loop.input(loop.countPort()).name() == "count");
	REQUIRE(loop.output(loop.iterationsPort()).name() == "iterations");
}

TEST_CASE("a refusal alone is not a change, so it never bumps the recipe version", "[flow][loop]")
{
	// A refused pin is retried on EVERY pass and a host syncs every frame, so counting a refusal as
	// a change would bump the node's version continuously and force every evaluation to re-run
	// forever. It describes a steady state, not a transition.
	Scene scene;
	LoopNode& loop = scene.loop();
	loop.inner().boundaryInputNode().addBoundary<int>("count");

	scene.sync(); // settle whatever the first pass legitimately does
	const std::uint64_t before = loop.version();
	edit::GroupSync sync = scene.sync();

	REQUIRE_FALSE(sync.refused.empty());
	REQUIRE_FALSE(sync.changed());
	REQUIRE(loop.version() == before);
}

TEST_CASE("a rename onto a loop's own port name is refused, keeping the old label", "[flow][loop]")
{
	// The same collision arriving by the other door: a pin mirrored under one name, then renamed to
	// one the loop already owns. Taking it would leave two same-named outer ports, which makes every
	// name-addressed edge through them ambiguous on disk.
	Scene scene;
	LoopNode& loop = scene.loop();
	const PortId pin = loop.inner().boundaryInputNode().addBoundary<int>("scale");
	REQUIRE(scene.sync().added == 1);

	loop.inner().boundaryInputNode().output(pin).setName("count");
	edit::GroupSync sync = scene.sync();

	REQUIRE(sync.renamed == 0);
	REQUIRE(listed(sync.refused, "count"));
	REQUIRE(has(loop, Port::Direction::Input, "scale")); // the outer label stood its ground
	REQUIRE(loop.inputCount() == 2);					 // `count` and the stale-labelled mirror
}

TEST_CASE("losing half a carry drops the pairing and the survivor becomes ordinary", "[flow][loop]")
{
	Scene scene;
	LoopNode& loop = scene.loop();
	const LoopNode::Carry carry = loop.addCarry<int>("total");
	REQUIRE(scene.sync().added == 2);
	REQUIRE(loop.carries().size() == 1);

	// What the Interface pane's ± does to one half.
	const NodeId outNode = loop.inner().boundaryOutputNode().id();
	REQUIRE(edit::removePort(loop.inner(), PortAddress{outNode, carry.innerOut}));

	edit::GroupSync sync = scene.sync();
	REQUIRE(sync.removed == 1); // the outer `total` output went with its pin

	// The pairing was a fact about the interior, and it stopped being true — so it is dropped rather
	// than kept as something a later reader would have to distrust. The survivor now means exactly
	// what an unpaired inner input means: an invariant.
	REQUIRE(loop.carries().empty());
	REQUIRE(has(loop, Port::Direction::Input, "total"));
	REQUIRE_FALSE(has(loop, Port::Direction::Output, "total"));
}

//=============================================================================
// The fold — a loop actually running, through the production schedulers
//=============================================================================

TEST_CASE("a count loop folds its carried value once per iteration", "[flow][loop]")
{
	// The whole point of the node: iteration k's carried OUTPUT is iteration k+1's carried INPUT,
	// and only the last one survives — a map maps, a loop folds.
	Fold f{10, 5};
	const NodeId body = f.inner().add<AddOne>();
	f.carryThrough(body);
	f.close();

	Evaluation evaluation{f.parent};

	SECTION("serial")
	{
		SerialScheduler{}.run(f.parent, evaluation);
	}

	SECTION("parallel")
	{
		// Both strategies share one staging loop and differ only in executePlan, so the fold
		// cannot drift between them.
		lain::task::Executor executor;
		ParallelScheduler{executor}.run(f.parent, evaluation);
	}

	REQUIRE(f.out(evaluation, "value").get<int>() == 15);
	REQUIRE(f.iterations(evaluation).get<int>() == 5);
}

TEST_CASE("the body is told which iteration it is in", "[flow][loop]")
{
	// `index` is the engine's own pin: it is written before each pass and never mirrored outward.
	// Read through an UNPAIRED output, which delivers the last iteration's value.
	Fold f{0, 4};
	const NodeId body = f.inner().add<AddOne>();
	f.carryThrough(body);

	const PortId stepPin = f.inner().boundaryOutputNode().addBoundary<int>("step");
	const NodeId pass = f.inner().add<PassInt>();
	REQUIRE(f.inner().connect(PortAddress{f.innerIn(), f.loop().indexPin()},
							  PortAddress{pass, f.inner().node(pass).input(0).id()}) == Connection::Ok);
	REQUIRE(f.inner().connect(PortAddress{pass, f.inner().node(pass).output(0).id()},
							  PortAddress{f.innerOut(), stepPin}) == Connection::Ok);
	f.close();

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	REQUIRE(f.out(evaluation, "value").get<int>() == 4);
	REQUIRE(f.out(evaluation, "step").get<int>() == 3); // iterations are indexed from zero
}

TEST_CASE("a count of zero is the fold identity, not a failure", "[flow][loop]")
{
	// Zero iterations run and every carry delivers its SEED — the fold over an empty sequence,
	// exactly as a map's N == 0 gathers an empty vector (a value). An UNPAIRED output has no last
	// iteration to report, so it stays empty.
	Fold f{10, 0};
	const NodeId body = f.inner().add<AddOne>();
	f.carryThrough(body);

	const PortId stepPin = f.inner().boundaryOutputNode().addBoundary<int>("step");
	const NodeId pass = f.inner().add<PassInt>();
	REQUIRE(f.inner().connect(PortAddress{f.innerIn(), f.loop().indexPin()},
							  PortAddress{pass, f.inner().node(pass).input(0).id()}) == Connection::Ok);
	REQUIRE(f.inner().connect(PortAddress{pass, f.inner().node(pass).output(0).id()},
							  PortAddress{f.innerOut(), stepPin}) == Connection::Ok);
	f.close();

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	REQUIRE(f.out(evaluation, "value").get<int>() == 10); // the seed, untouched
	REQUIRE(f.iterations(evaluation).get<int>() == 0);
	REQUIRE(f.out(evaluation, "step").empty());
}

TEST_CASE("a wired condition stops the loop early, and iterations says when", "[flow][loop]")
{
	// A while loop is the condition with the count as its BOUND. `iterations` is what makes
	// "converged at 3" distinguishable from "hit the bound at 100" — a fact the graph reads, not a
	// policy the engine invents.
	Fold f{0, 100};
	const NodeId body = f.inner().add<AddOne>();
	f.carryThrough(body);

	const NodeId cond = f.inner().add<Below>(3);
	REQUIRE(f.inner().connect(PortAddress{body, f.inner().node(body).output(0).id()},
							  PortAddress{cond, f.inner().node(cond).input(0).id()}) == Connection::Ok);
	REQUIRE(f.inner().connect(PortAddress{cond, f.inner().node(cond).output(0).id()},
							  PortAddress{f.innerOut(), f.loop().continuePin()}) == Connection::Ok);
	f.close();

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	REQUIRE(f.out(evaluation, "value").get<int>() == 3);
	REQUIRE(f.iterations(evaluation).get<int>() == 3); // far short of the bound
}

TEST_CASE("a condition that is false at once still runs one iteration", "[flow][loop]")
{
	// `continue` is read AFTER a pass, so it answers "run another?" rather than "run at all?".
	Fold f{0, 100};
	const NodeId body = f.inner().add<AddOne>();
	f.carryThrough(body);

	const NodeId stop = f.inner().add<ConstantNode<bool>>(false);
	REQUIRE(f.inner().connect(PortAddress{stop, f.inner().node(stop).output(0).id()},
							  PortAddress{f.innerOut(), f.loop().continuePin()}) == Connection::Ok);
	f.close();

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	REQUIRE(f.out(evaluation, "value").get<int>() == 1);
	REQUIRE(f.iterations(evaluation).get<int>() == 1);
}

TEST_CASE("the bound holds when the condition never goes false", "[flow][loop]")
{
	// Bounded BY CONSTRUCTION: there is no unbounded state to represent, which is what keeps the
	// staging loop's termination argument intact (ADR-0021).
	Fold f{0, 4};
	const NodeId body = f.inner().add<AddOne>();
	f.carryThrough(body);

	const NodeId cond = f.inner().add<Below>(1000);
	REQUIRE(f.inner().connect(PortAddress{body, f.inner().node(body).output(0).id()},
							  PortAddress{cond, f.inner().node(cond).input(0).id()}) == Connection::Ok);
	REQUIRE(f.inner().connect(PortAddress{cond, f.inner().node(cond).output(0).id()},
							  PortAddress{f.innerOut(), f.loop().continuePin()}) == Connection::Ok);
	f.close();

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	REQUIRE(f.out(evaluation, "value").get<int>() == 4);
	REQUIRE(f.iterations(evaluation).get<int>() == 4);
}

TEST_CASE("an iteration that suppresses a carry clears the whole fold", "[flow][loop]")
{
	// Suppression is FAILURE, not a break: a partial fold presented as a finished one is a
	// plausible answer nothing downstream can tell from a converged one, so everything clears and
	// ADR-0007 carries the emptiness onward.
	Fold f{0, 5};
	const NodeId body = f.inner().add<StopAt>(2);
	f.carryThrough(body);
	f.close();

	const NodeId consumer = f.parent.add<PassInt>();
	REQUIRE(f.parent.connect(PortAddress{f.loopId, outputNamed(f.parent.node(f.loopId), "value")},
							 PortAddress{consumer, f.parent.node(consumer).input(0).id()}) == Connection::Ok);

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	REQUIRE(f.out(evaluation, "value").empty());
	REQUIRE(f.iterations(evaluation).empty());
	REQUIRE(evaluation.value(PortAddress{consumer, f.parent.node(consumer).output(0).id()}).empty());
}

TEST_CASE("a loop that cannot run never runs its body", "[flow][loop]")
{
	// Its seed comes from something that produced nothing, so a REQUIRED input is empty and the
	// node does not compute at all — the ordinary readiness gate, asked at the exit rather than
	// remembered from the preparation that noticed it.
	int calls = 0;
	Fold f{0, 5};
	const NodeId body = f.inner().add<AddOne>(&calls);
	f.carryThrough(body);
	f.close(false); // the seed input is left for the suppressed source below

	const NodeId nothing = f.parent.add<Nothing>();
	REQUIRE(f.parent.connect(PortAddress{nothing, f.parent.node(nothing).output(0).id()},
							 PortAddress{f.loopId, inputNamed(f.parent.node(f.loopId), "value")}) == Connection::Ok);

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	REQUIRE(calls == 0);
	REQUIRE(f.out(evaluation, "value").empty());
	REQUIRE(f.iterations(evaluation).empty());
}

TEST_CASE("an unpaired input is an invariant that holds for the whole fold", "[flow][loop]")
{
	// Only a CARRIED pin is rebound between iterations; everything else stays as it was seeded, so
	// an unpaired input is exactly what "an invariant" means (and needs no declaration to say so).
	Fold f{0, 4};
	const NodeId body = f.inner().add<test::AddInt>();
	f.carryThrough(body); // the carry through input 0 / output 0

	const PortId kPin = f.inner().boundaryInputNode().addBoundary<int>("k");
	REQUIRE(f.inner().connect(PortAddress{f.innerIn(), kPin},
							  PortAddress{body, f.inner().node(body).input(1).id()}) == Connection::Ok);
	f.close();

	const NodeId k = f.parent.add<ConstantNode<int>>(3);
	REQUIRE(f.parent.connect(PortAddress{k, f.parent.node(k).output(0).id()},
							 PortAddress{f.loopId, inputNamed(f.parent.node(f.loopId), "k")}) == Connection::Ok);

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	REQUIRE(f.out(evaluation, "value").get<int>() == 12); // 0 + 3 four times
}

TEST_CASE("a loop keeps its result when a later run has nothing stale", "[flow][loop]")
{
	// A fold is not incremental INSIDE — iteration k's inputs are iteration k-1's outputs — but it
	// is incremental from outside: nothing stale means nothing runs, and a changed seed re-folds.
	int calls = 0;
	Fold f{10, 5};
	const NodeId body = f.inner().add<AddOne>(&calls);
	f.carryThrough(body);
	f.close();

	Evaluation evaluation{f.parent};
	SerialScheduler scheduler;
	scheduler.run(f.parent, evaluation);
	REQUIRE(f.out(evaluation, "value").get<int>() == 15);
	REQUIRE(calls == 5);

	scheduler.run(f.parent, evaluation);
	REQUIRE(calls == 5); // nothing changed, so no iteration ran
	REQUIRE(f.out(evaluation, "value").get<int>() == 15);

	static_cast<ConstantNode<int>&>(f.parent.node(f.seedId)).setValue(20);
	scheduler.run(f.parent, evaluation);
	REQUIRE(calls == 10); // the whole fold re-runs from the new seed
	REQUIRE(f.out(evaluation, "value").get<int>() == 25);
}

TEST_CASE("a loop inside a loop folds at both levels", "[flow][loop]")
{
	// Stages multiply rather than add, and every level's frontier is addressed independently.
	Fold outer{0, 3};
	Graph& body = outer.inner();

	const NodeId innerId = body.add<LoopNode>();
	auto& innerLoop = static_cast<LoopNode&>(body.node(innerId));
	const LoopNode::Carry innerCarry = innerLoop.addCarry<int>("value");
	const NodeId add = innerLoop.inner().add<AddOne>();
	REQUIRE(innerLoop.inner().connect(PortAddress{innerLoop.inner().boundaryInputNode().id(), innerCarry.innerIn},
									  PortAddress{add, innerLoop.inner().node(add).input(0).id()}) == Connection::Ok);
	REQUIRE(innerLoop.inner().connect(PortAddress{add, innerLoop.inner().node(add).output(0).id()},
									  PortAddress{innerLoop.inner().boundaryOutputNode().id(), innerCarry.innerOut}) ==
			Connection::Ok);
	edit::syncGroupPorts(body, innerId);

	const NodeId innerBound = body.add<ConstantNode<int>>(2);
	REQUIRE(body.connect(PortAddress{innerBound, body.node(innerBound).output(0).id()},
						 PortAddress{innerId, innerLoop.countPort()}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{outer.innerIn(), outer.carry.innerIn},
						 PortAddress{innerId, inputNamed(body.node(innerId), "value")}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{innerId, outputNamed(body.node(innerId), "value")},
						 PortAddress{outer.innerOut(), outer.carry.innerOut}) == Connection::Ok);
	outer.close();

	Evaluation evaluation{outer.parent};
	SerialScheduler{}.run(outer.parent, evaluation);

	REQUIRE(outer.out(evaluation, "value").get<int>() == 6); // three outer passes of two inner ones
	REQUIRE(outer.iterations(evaluation).get<int>() == 3);
}

TEST_CASE("a map inside a loop maps over the new value every iteration", "[flow][loop]")
{
	// A loop reuses ONE child evaluation, so a map inside it has the same frontier address every
	// pass. Seeding a new iteration therefore forgets what is staged inside the interior — without
	// that, the map would be expanded against the previous iteration's children and quietly serve
	// its previous gather.
	registerLoopTypes();

	Fold f{1, 2};
	Graph& body = f.inner();
	const NodeId pair = body.add<PairOf>();
	const NodeId sum = body.add<SumInts>();

	const NodeId mapId = body.add<MapNode>();
	auto& map = static_cast<MapNode&>(body.node(mapId));
	const PortId mapIn = map.inner().boundaryInputNode().addBoundary<int>("x");
	const PortId mapOut = map.inner().boundaryOutputNode().addBoundary<int>("x");
	const NodeId add = map.inner().add<AddOne>();
	REQUIRE(map.inner().connect(PortAddress{map.inner().boundaryInputNode().id(), mapIn},
								PortAddress{add, map.inner().node(add).input(0).id()}) == Connection::Ok);
	REQUIRE(map.inner().connect(PortAddress{add, map.inner().node(add).output(0).id()},
								PortAddress{map.inner().boundaryOutputNode().id(), mapOut}) == Connection::Ok);
	edit::syncGroupPorts(body, mapId);

	REQUIRE(body.connect(PortAddress{f.innerIn(), f.carry.innerIn},
						 PortAddress{pair, body.node(pair).input(0).id()}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{pair, body.node(pair).output(0).id()},
						 PortAddress{mapId, inputNamed(body.node(mapId), "x")}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{mapId, outputNamed(body.node(mapId), "x")},
						 PortAddress{sum, body.node(sum).input(0).id()}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{sum, body.node(sum).output(0).id()},
						 PortAddress{f.innerOut(), f.carry.innerOut}) == Connection::Ok);
	f.close();

	Evaluation evaluation{f.parent};
	SerialScheduler{}.run(f.parent, evaluation);

	// 1 -> {1,1} -> {2,2} -> 4 -> {4,4} -> {5,5} -> 10
	REQUIRE(f.out(evaluation, "value").get<int>() == 10);
}

TEST_CASE("a loop inside a map lets each element stop at its own iteration", "[flow][loop][map]")
{
	// Each element is its own EVALUATION of one definition, so each raises its own frontier — which
	// is what the {definition, evaluation, node} address exists for, and the first case in the tree
	// where two frontiers sharing a node id genuinely diverge.
	registerLoopTypes();

	Graph parent;
	const NodeId mapId = parent.add<MapNode>();
	auto& map = static_cast<MapNode&>(parent.node(mapId));
	Graph& body = map.inner();
	const PortId rowIn = body.boundaryInputNode().addBoundary<int>("value");
	const PortId rowOut = body.boundaryOutputNode().addBoundary<int>("value");

	const NodeId loopId = body.add<LoopNode>();
	auto& loop = static_cast<LoopNode&>(body.node(loopId));
	const LoopNode::Carry carry = loop.addCarry<int>("value");
	const NodeId add = loop.inner().add<AddOne>();
	REQUIRE(loop.inner().connect(PortAddress{loop.inner().boundaryInputNode().id(), carry.innerIn},
								 PortAddress{add, loop.inner().node(add).input(0).id()}) == Connection::Ok);
	REQUIRE(loop.inner().connect(PortAddress{add, loop.inner().node(add).output(0).id()},
								 PortAddress{loop.inner().boundaryOutputNode().id(), carry.innerOut}) == Connection::Ok);
	const NodeId cond = loop.inner().add<Below>(4);
	REQUIRE(loop.inner().connect(PortAddress{add, loop.inner().node(add).output(0).id()},
								 PortAddress{cond, loop.inner().node(cond).input(0).id()}) == Connection::Ok);
	REQUIRE(loop.inner().connect(PortAddress{cond, loop.inner().node(cond).output(0).id()},
								 PortAddress{loop.inner().boundaryOutputNode().id(), loop.continuePin()}) ==
			Connection::Ok);
	edit::syncGroupPorts(body, loopId);

	const NodeId bound = body.add<ConstantNode<int>>(100);
	REQUIRE(body.connect(PortAddress{bound, body.node(bound).output(0).id()},
						 PortAddress{loopId, loop.countPort()}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{body.boundaryInputNode().id(), rowIn},
						 PortAddress{loopId, inputNamed(body.node(loopId), "value")}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{loopId, outputNamed(body.node(loopId), "value")},
						 PortAddress{body.boundaryOutputNode().id(), rowOut}) == Connection::Ok);
	edit::syncGroupPorts(parent, mapId);

	const NodeId source = parent.add<ConstantNode<std::vector<int>>>(std::vector<int>{1, 5});
	REQUIRE(parent.connect(PortAddress{source, parent.node(source).output(0).id()},
						   PortAddress{mapId, inputNamed(parent.node(mapId), "value")}) == Connection::Ok);

	Evaluation evaluation{parent};
	SerialScheduler{}.run(parent, evaluation);

	// Row 0 climbs 1 -> 4 in three passes; row 1 is already past the limit and stops after one.
	const std::vector<int> expected{4, 6};
	REQUIRE(evaluation.value(PortAddress{mapId, outputNamed(parent.node(mapId), "value")}).get<std::vector<int>>() ==
			expected);
}

TEST_CASE("a loop inside a group delivers through the group's face", "[flow][loop]")
{
	// The ordinary nesting: a group's exit publishes what its interior finally settled on.
	Graph parent;
	const NodeId groupId = parent.add<InlineGroupNode>();
	auto& group = static_cast<InlineGroupNode&>(parent.node(groupId));
	Graph& body = group.inner();

	const NodeId loopId = body.add<LoopNode>();
	auto& loop = static_cast<LoopNode&>(body.node(loopId));
	const LoopNode::Carry carry = loop.addCarry<int>("value");
	const NodeId add = loop.inner().add<AddOne>();
	REQUIRE(loop.inner().connect(PortAddress{loop.inner().boundaryInputNode().id(), carry.innerIn},
								 PortAddress{add, loop.inner().node(add).input(0).id()}) == Connection::Ok);
	REQUIRE(loop.inner().connect(PortAddress{add, loop.inner().node(add).output(0).id()},
								 PortAddress{loop.inner().boundaryOutputNode().id(), carry.innerOut}) == Connection::Ok);
	edit::syncGroupPorts(body, loopId);

	const NodeId seed = body.add<ConstantNode<int>>(10);
	const NodeId bound = body.add<ConstantNode<int>>(3);
	REQUIRE(body.connect(PortAddress{seed, body.node(seed).output(0).id()},
						 PortAddress{loopId, inputNamed(body.node(loopId), "value")}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{bound, body.node(bound).output(0).id()},
						 PortAddress{loopId, loop.countPort()}) == Connection::Ok);

	const PortId resultPin = body.boundaryOutputNode().addBoundary<int>("result");
	REQUIRE(body.connect(PortAddress{loopId, outputNamed(body.node(loopId), "value")},
						 PortAddress{body.boundaryOutputNode().id(), resultPin}) == Connection::Ok);
	edit::syncGroupPorts(parent, groupId);

	Evaluation evaluation{parent};
	SerialScheduler{}.run(parent, evaluation);

	REQUIRE(evaluation.value(PortAddress{groupId, outputNamed(parent.node(groupId), "result")}).get<int>() == 13);
}

TEST_CASE("a group holding an iterating loop publishes once, not once per stage", "[flow][loop]")
{
	// A group's exit used to be emitted unconditionally, even when the recursive expansion of its
	// interior had deferred something — so a group containing a mid-fold loop republished the
	// PREVIOUS stage's value onto its outer outputs on every intermediate stage, and everything
	// downstream recomputed on it. The final value was right, which is exactly why no value
	// assertion ever caught this: the only observable is how often a downstream node ran.
	//
	// The rule now: a node does not publish while its interior has deferred.
	Graph parent;
	const NodeId groupId = parent.add<InlineGroupNode>();
	auto& group = static_cast<InlineGroupNode&>(parent.node(groupId));
	Graph& body = group.inner();

	const NodeId loopId = body.add<LoopNode>();
	auto& loop = static_cast<LoopNode&>(body.node(loopId));
	const LoopNode::Carry carry = loop.addCarry<int>("value");
	const NodeId add = loop.inner().add<AddOne>();
	REQUIRE(loop.inner().connect(PortAddress{loop.inner().boundaryInputNode().id(), carry.innerIn},
								 PortAddress{add, loop.inner().node(add).input(0).id()}) == Connection::Ok);
	REQUIRE(loop.inner().connect(PortAddress{add, loop.inner().node(add).output(0).id()},
								 PortAddress{loop.inner().boundaryOutputNode().id(), carry.innerOut}) == Connection::Ok);
	edit::syncGroupPorts(body, loopId);

	// The seed arrives from the PARENT, through the group's own face and one node inside that reads
	// it. That node is what makes the entry step's ordering observable: it is not downstream of the
	// deferred loop, so it runs in the very stage the group is deferred in, and it must not read the
	// boundary before the entry has published into it.
	const PortId seedPin = body.boundaryInputNode().addBoundary<int>("seed");
	const NodeId relay = body.add<AddOne>();
	REQUIRE(body.connect(PortAddress{body.boundaryInputNode().id(), seedPin},
						 PortAddress{relay, body.node(relay).input(0).id()}) == Connection::Ok);
	const NodeId bound = body.add<ConstantNode<int>>(4);
	REQUIRE(body.connect(PortAddress{relay, body.node(relay).output(0).id()},
						 PortAddress{loopId, inputNamed(body.node(loopId), "value")}) == Connection::Ok);
	REQUIRE(body.connect(PortAddress{bound, body.node(bound).output(0).id()},
						 PortAddress{loopId, loop.countPort()}) == Connection::Ok);

	const PortId resultPin = body.boundaryOutputNode().addBoundary<int>("result");
	REQUIRE(body.connect(PortAddress{loopId, outputNamed(body.node(loopId), "value")},
						 PortAddress{body.boundaryOutputNode().id(), resultPin}) == Connection::Ok);
	edit::syncGroupPorts(parent, groupId);

	const NodeId seed = parent.add<ConstantNode<int>>(9);
	REQUIRE(parent.connect(PortAddress{seed, parent.node(seed).output(0).id()},
						   PortAddress{groupId, inputNamed(parent.node(groupId), "seed")}) == Connection::Ok);

	// The observable: a consumer of the GROUP's output, counting how often it was handed one.
	// Atomic because the parallel section below runs it on a worker.
	struct Counted : Node
	{
		std::atomic<int>& calls;
		PortId in, out;
		explicit Counted(std::atomic<int>& c)
			: Node("Counted")
			, calls(c)
		{
			in = addInput<int>("x");
			out = addOutput<int>("x");
		}
		void compute(NodeEvaluation& evaluation) const override
		{
			++calls;
			evaluation.output(out).set(evaluation.input(in).get<int>());
		}
	};

	std::atomic<int> consumed{0};
	const NodeId consumer = parent.add<Counted>(consumed);
	REQUIRE(parent.connect(PortAddress{groupId, outputNamed(parent.node(groupId), "result")},
						   PortAddress{consumer, parent.node(consumer).input(0).id()}) == Connection::Ok);

	// Both strategies, because the entry step must be ORDERED against the inner boundary it feeds
	// whether or not the group goes on to publish — a serial walk hides a missing edge there, the
	// parallel backend does not.
	lain::task::Executor executor;
	bool parallel = false;
	SECTION("serial") {}
	SECTION("parallel")
	{
		parallel = true;
	}
	Evaluation evaluation{parent};
	const auto run = [&]
	{
		if (parallel)
			ParallelScheduler{executor}.run(parent, evaluation);
		else
			SerialScheduler{}.run(parent, evaluation);
	};

	run();
	REQUIRE(evaluation.value(PortAddress{consumer, parent.node(consumer).output(0).id()}).get<int>() == 14);

	// It has to be the SECOND run that is measured, and that is the whole subtlety of this bug. On a
	// first run the group's outer output is still empty, so an early publish hands the consumer an
	// empty slot, ADR-0007 suppresses it, and nothing runs on nonsense. Once the group HOLDS a
	// value, an early publish hands over the PREVIOUS run's answer — indistinguishable from a
	// finished one — and the consumer computes on it once per intermediate stage.
	consumed = 0;
	static_cast<ConstantNode<int>&>(parent.node(seed)).setValue(19);
	run();

	REQUIRE(evaluation.value(PortAddress{consumer, parent.node(consumer).output(0).id()}).get<int>() == 24);
	REQUIRE(consumed == 1);
}

TEST_CASE("the pull path folds a loop the same way", "[flow][loop]")
{
	// evaluate() plans the stale upstream cone instead of the stale closure, but staging is shared:
	// a loop reached by a pull iterates exactly as it does under a push run.
	Fold f{10, 5};
	const NodeId body = f.inner().add<AddOne>();
	f.carryThrough(body);
	f.close();

	Evaluation evaluation{f.parent};
	SerialScheduler{}.evaluate(f.parent, evaluation, f.loopId);

	REQUIRE(f.out(evaluation, "value").get<int>() == 15);
	REQUIRE(f.iterations(evaluation).get<int>() == 5);
}
