// The loop node (M11 slice 2, ADR-0021): a group whose interior is evaluated once per ITERATION,
// each pass's carried outputs seeding the next one's inputs.
//
// NOTHING RUNS YET — this slice declares the shape and nothing else, so these are structural, and
// they drive the real edit::syncGroupPorts against a real interior rather than asserting about a
// hand-built port list. The rules under test, all from ADR-0021:
//   * a loop's FACE is derived exactly as a group's, except for two refusals;
//   * the two RESERVED pins are the engine's, skipped BY ID so a rename cannot change behaviour;
//   * a CARRY is a pair, created only by a paired gesture, so half of one cannot be authored;
//   * a loop owns ports no group does, so it is the first kind where an inner pin's name can
//     collide with one — refused, and reported, rather than silently degrading (or asserting).

#include "lain/flow/edit.h"
#include "lain/flow/graph.h"
#include "lain/flow/group.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

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
