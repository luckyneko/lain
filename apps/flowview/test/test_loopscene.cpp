// M11's end-to-end claim, driver-free: a subgraph run once per ITERATION, each pass's carried
// output seeding the next one's input — through the production save/load facade and the production
// scheduler, with the same factory keys the real binary registers.
//
// This is the milestone's deliverable. ADR-0021 says out loud that a loop has no production caller
// — "there is no direct use case as much as this is a feature that really tests the node approach"
// — which makes the vertical the ONLY evidence the design works, and the standard stricter than
// M8's rather than looser. So both shapes the owner asked for are here:
//
//   COUNT — gradient -> carry -> blur, five times. The fold with nothing else in it: no condition,
//           no new node, and an answer an independent chain of five blurs can be compared against
//           byte for byte.
//   WHILE — blur until it stops changing. The condition path, driven: the body MEASURES how far
//           this pass moved (ImageDifference), DECIDES (Compare), and writes the reserved
//           `continue` pin. `iterations` is what makes "converged at k" distinguishable from "hit
//           the bound", which is the whole reason it exists.
//
// Both documents are written to a stable scratch path, so `flowview run --graph <it>` is a
// copy-paste — the binary run is what proves the app can open what this built.

#include "graphio.h"
#include "scene.h" // registerExampleNodes — the PRODUCTION keys, so the binary reads what this writes

#include <lain/core/factory.h>
#include <lain/flow/boundary.h>
#include <lain/flow/edit.h>
#include <lain/flow/evaluation.h>
#include <lain/flow/example/comparenode.h> // Comparison + the payload-type name the retype addresses
#include <lain/flow/graph.h>
#include <lain/flow/group.h>
#include <lain/flow/param.h>
#include <lain/flow/scheduler.h>
#include <lain/image/image.h>
#include <lain/task/task.h> // task::Executor — the parallel scheduler is injected one

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

using lain::core::Factory;
using namespace lain::flow;
using lain::image::Image;

namespace
{
	constexpr std::uint32_t kSize = 32; // the gradient the example factory makes
	constexpr int kCountIterations = 5; // the count vertical's trip count
	constexpr int kWhileBound = 100;	// the while vertical's safety bound
	// "Stopped changing" is LITERAL here: a clamped Gaussian blur of an 8-bit image reaches an exact
	// fixed point, so the measured difference eventually becomes zero and `> 0` is an exact test
	// needing no invented tolerance. A positive tolerance is the same mechanism stopping earlier —
	// which is what the bounded section below demonstrates from the other end.
	constexpr float kWhileThreshold = 0.0f;

	Factory<Node> sceneFactory()
	{
		flowview::registerSceneSerialization(); // port types + the json codec
		Factory<Node> factory;
		// The app's OWN registration, not a copy of it: these documents have to be openable by the
		// real binary, and a second key list is how that quietly stops being true.
		flowview::registerExampleNodes(factory, kSize);
		return factory;
	}

	std::filesystem::path scratchDir()
	{
		// Stable, so the manual `flowview run --graph …` this milestone verifies with is a
		// copy-paste rather than a path to go hunting for.
		const std::filesystem::path dir = std::filesystem::temp_directory_path() / "flowview-loopscene-test";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		return dir;
	}

	PortId portNamed(const Node& node, Port::Direction side, const std::string& name)
	{
		const std::size_t count = side == Port::Direction::Input ? node.inputCount() : node.outputCount();
		for (std::size_t i = 0; i < count; ++i)
		{
			const Port& port = side == Port::Direction::Input ? node.input(i) : node.output(i);
			if (port.name() == name)
				return port.id();
		}
		return PortId{};
	}

	// Set the param behind a DEFAULTED input, addressed by the PORT it stands for — which is the
	// handle a caller holds; `defaultOf` is what pairs the two halves.
	template <typename T>
	void setDefault(Node& node, PortId port, T value)
	{
		const Param* fallback = node.defaultOf(port);
		REQUIRE(fallback != nullptr);
		REQUIRE(node.setParam<T>(fallback->id(), std::move(value)));
	}

	// Set a param addressed by NAME — `b`'s Default declares a param of its own first, so a
	// positional handle would find the wrong one (M6 step 2's trap).
	template <typename T>
	void setParamNamed(Node& node, const std::string& name, T value)
	{
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (node.param(i).name() == name)
			{
				REQUIRE(node.setParam<T>(node.param(i).id(), std::move(value)));
				return;
			}
		}
		FAIL("no param named " + name);
	}

	Connection wire(Graph& g, NodeId from, PortId fromPin, NodeId to, PortId toPin)
	{
		return g.connect(PortAddress{from, fromPin}, PortAddress{to, toPin});
	}

	PortId out0(const Graph& g, NodeId node) { return g.node(node).output(0).id(); }
	PortId in0(const Graph& g, NodeId node) { return g.node(node).input(0).id(); }

	// The half both verticals share: a loop carrying one image through a blur, seeded by a gradient
	// and reporting its result and its trip count at the graph's boundary.
	struct Scene
	{
		Graph graph;
		NodeId loopId;
		NodeId blur;
		LoopNode::Carry carry;

		LoopNode& loop() { return static_cast<LoopNode&>(graph.node(loopId)); }
		Graph& inner() { return loop().inner(); }
	};

	Scene buildFoldScene(const Factory<Node>& factory, int bound)
	{
		Scene scene;
		Graph& graph = scene.graph;
		const NodeId gradient = graph.add(factory.create("gradient"));
		scene.loopId = graph.add(factory.create("loop"));

		// THE carry — the one thing that makes this a loop rather than a group run N times over the
		// same input. Both inner pins in one gesture, so half of one cannot exist.
		scene.carry = scene.loop().addCarry<Image>("image");
		REQUIRE(scene.carry.innerIn != PortId{});

		Graph& inner = scene.inner();
		scene.blur = inner.add(factory.create("blur"));
		REQUIRE(wire(inner, inner.boundaryInputNode().id(), scene.carry.innerIn, scene.blur, in0(inner, scene.blur)) == Connection::Ok);
		REQUIRE(wire(inner, scene.blur, out0(inner, scene.blur), inner.boundaryOutputNode().id(), scene.carry.innerOut) == Connection::Ok);

		// Derive the loop's face from that interior, exactly as the host's per-frame sync does: the
		// carry becomes a seed input and a final output, both named `image` (a loop is the first
		// node where one name on both sides is by design rather than by accident).
		edit::syncGroupPorts(graph, scene.loopId);
		setDefault<int>(graph.node(scene.loopId), scene.loop().countPort(), bound);

		const PortId seed = portNamed(graph.node(scene.loopId), Port::Direction::Input, "image");
		const PortId result = portNamed(graph.node(scene.loopId), Port::Direction::Output, "image");
		const PortId ran = scene.loop().iterationsPort();
		REQUIRE(seed != PortId{});
		REQUIRE(result != PortId{});

		REQUIRE(wire(graph, gradient, out0(graph, gradient), scene.loopId, seed) == Connection::Ok);

		const PortId resultPin = graph.boundaryOutputNode().addBoundary<Image>("result");
		const PortId iterationsPin = graph.boundaryOutputNode().addBoundary<int>("iterations");
		REQUIRE(wire(graph, scene.loopId, result, graph.boundaryOutputNode().id(), resultPin) == Connection::Ok);
		REQUIRE(wire(graph, scene.loopId, ran, graph.boundaryOutputNode().id(), iterationsPin) == Connection::Ok);
		return scene;
	}

	// ... and the condition the WHILE vertical adds to it: measure how far this pass moved, decide,
	// and write the reserved `continue` pin. Nothing here is loop machinery — the body is ordinary
	// nodes, which is the point.
	void addConvergenceCondition(Scene& scene, const Factory<Node>& factory, float threshold)
	{
		Graph& inner = scene.inner();
		const NodeId difference = inner.add(factory.create("imageDifference"));
		const NodeId compare = inner.add(factory.create("compare"));

		// a = the value this iteration STARTED from, b = what the blur made of it.
		REQUIRE(wire(inner, inner.boundaryInputNode().id(), scene.carry.innerIn, difference, in0(inner, difference)) == Connection::Ok);
		REQUIRE(wire(inner, scene.blur, out0(inner, scene.blur), difference, inner.node(difference).input(1).id()) == Connection::Ok);
		REQUIRE(wire(inner, difference, out0(inner, difference), compare, in0(inner, compare)) == Connection::Ok);
		setDefault<float>(inner.node(compare), inner.node(compare).input(1).id(), threshold);

		// -> the reserved pin. Keep going while the change is still above the threshold; the count
		// stays as the bound, which is what every real convergence routine carries anyway.
		REQUIRE(wire(inner, compare, out0(inner, compare), inner.boundaryOutputNode().id(), scene.loop().continuePin()) == Connection::Ok);
	}

	const Image& boundaryImage(const Graph& graph, const Evaluation& evaluation, const std::string& name)
	{
		for (const BoundaryOutput& out : graph.boundaryOutputs())
		{
			if (out.name == name)
			{
				REQUIRE_FALSE(evaluation.value(out).empty());
				return evaluation.value(out).get<Image>();
			}
		}
		FAIL("no boundary output named " + name);
		static const Image none;
		return none;
	}

	int boundaryInt(const Graph& graph, const Evaluation& evaluation, const std::string& name)
	{
		for (const BoundaryOutput& out : graph.boundaryOutputs())
		{
			if (out.name == name)
			{
				REQUIRE_FALSE(evaluation.value(out).empty());
				return evaluation.value(out).get<int>();
			}
		}
		FAIL("no boundary output named " + name);
		return 0;
	}

	bool sameBytes(const Image& a, const Image& b)
	{
		if (!a.valid() || !b.valid() || a.width() != b.width() || a.height() != b.height() || a.pixelFormat() != b.pixelFormat())
			return false;
		const std::size_t bytes = static_cast<std::size_t>(a.width()) * a.height() * 4;
		for (std::size_t i = 0; i < bytes; ++i)
		{
			if (a.data()[i] != b.data()[i])
				return false;
		}
		return true;
	}

	// An INDEPENDENT implementation of what the count vertical should produce: the same blur applied
	// N times, wired as a plain chain. If the fold is right, the two agree byte for byte; if the
	// carry quietly seeds every pass from the same gradient, they do not.
	Image blurChain(const Factory<Node>& factory, int times)
	{
		Graph graph;
		const NodeId gradient = graph.add(factory.create("gradient"));
		NodeId last = gradient;
		for (int i = 0; i < times; ++i)
		{
			const NodeId blur = graph.add(factory.create("blur"));
			REQUIRE(wire(graph, last, out0(graph, last), blur, in0(graph, blur)) == Connection::Ok);
			last = blur;
		}
		Evaluation evaluation{graph};
		SerialScheduler{}.run(graph, evaluation);
		const PortValue& value = evaluation.value(PortAddress{last, out0(graph, last)});
		REQUIRE_FALSE(value.empty());
		return value.get<Image>();
	}

	std::string readAll(const std::filesystem::path& p)
	{
		std::ifstream in(p, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
} // namespace

TEST_CASE("a count loop folds a gradient through five blurs", "[flowview][loop]")
{
	const Factory<Node> factory = sceneFactory();
	Scene scene = buildFoldScene(factory, kCountIterations);

	Evaluation evaluation{scene.graph};
	SerialScheduler{}.run(scene.graph, evaluation);

	REQUIRE(boundaryInt(scene.graph, evaluation, "iterations") == kCountIterations);
	// Against an independent chain of five blurs, byte for byte. Every weaker assertion (it is an
	// image, it is 32x32, it is blurrier than the seed) passes just as well when the carry is not
	// carrying at all, which is precisely the failure this vertical exists to rule out.
	REQUIRE(sameBytes(boundaryImage(scene.graph, evaluation, "result"), blurChain(factory, kCountIterations)));
	REQUIRE_FALSE(sameBytes(boundaryImage(scene.graph, evaluation, "result"), blurChain(factory, 1)));

	SECTION("and a count of zero delivers the seed, which is the fold identity")
	{
		// Not a failure and not an empty output: zero iterations over a sequence is its identity,
		// exactly as a map's N == 0 yields an empty vector (ADR-0021).
		Scene none = buildFoldScene(factory, 0);
		Evaluation e{none.graph};
		SerialScheduler{}.run(none.graph, e);

		REQUIRE(boundaryInt(none.graph, e, "iterations") == 0);
		REQUIRE(sameBytes(boundaryImage(none.graph, e, "result"), blurChain(factory, 0)));
	}

	SECTION("and the parallel scheduler folds it identically")
	{
		lain::task::Executor executor;
		Scene same = buildFoldScene(factory, kCountIterations);
		Evaluation e{same.graph};
		ParallelScheduler{executor}.run(same.graph, e);

		REQUIRE(boundaryInt(same.graph, e, "iterations") == kCountIterations);
		REQUIRE(sameBytes(boundaryImage(same.graph, e, "result"), blurChain(factory, kCountIterations)));
	}
}

TEST_CASE("a while loop blurs until the picture stops changing", "[flowview][loop]")
{
	const Factory<Node> factory = sceneFactory();
	Scene scene = buildFoldScene(factory, kWhileBound);
	addConvergenceCondition(scene, factory, kWhileThreshold);

	Evaluation evaluation{scene.graph};
	SerialScheduler{}.run(scene.graph, evaluation);

	const int ran = boundaryInt(scene.graph, evaluation, "iterations");
	// CONVERGED, rather than hitting the bound — which is the distinction `iterations` exists to
	// make, and which no other observable can tell apart from outside.
	REQUIRE(ran > 1);
	REQUIRE(ran < kWhileBound);
	// It stopped for the RIGHT reason: one more blur of the answer moves it less than the threshold.
	REQUIRE(sameBytes(boundaryImage(scene.graph, evaluation, "result"), blurChain(factory, ran)));

	SECTION("a bound below the convergence point stops it early, and says so")
	{
		// The union of the two shapes, which ADR-0021 argues is what a real convergence routine is:
		// the condition decides when it can, the bound decides when it cannot.
		Scene bounded = buildFoldScene(factory, 2);
		addConvergenceCondition(bounded, factory, kWhileThreshold);
		Evaluation e{bounded.graph};
		SerialScheduler{}.run(bounded.graph, e);

		REQUIRE(boundaryInt(bounded.graph, e, "iterations") == 2);
		REQUIRE(sameBytes(boundaryImage(bounded.graph, e, "result"), blurChain(factory, 2)));
	}
}

TEST_CASE("both loop scenes survive save and load and fold the same", "[flowview][loop]")
{
	// Through the PRODUCTION facade — the same saveGraph / loadGraph the menu bar calls — so this
	// covers the `loop` section (the carry pairing and the reserved pin names), the interior's
	// re-derived face, and the fact that a loop's own ports are NOT stored because they are derived.
	const Factory<Node> factory = sceneFactory();
	const std::filesystem::path dir = scratchDir();

	struct Case
	{
		std::string file;
		bool converging;
	};
	const Case cases[] = {{"count-loop.json", false}, {"while-loop.json", true}};

	for (const Case& c : cases)
	{
		INFO("document: " << c.file);
		Scene scene = buildFoldScene(factory, c.converging ? kWhileBound : kCountIterations);
		if (c.converging)
			addConvergenceCondition(scene, factory, kWhileThreshold);

		Evaluation before{scene.graph};
		SerialScheduler{}.run(scene.graph, before);
		const int ranBefore = boundaryInt(scene.graph, before, "iterations");
		const Image expected = boundaryImage(scene.graph, before, "result");

		const std::filesystem::path document = dir / c.file;
		REQUIRE(flowview::saveGraph(document.string(), scene.graph, factory));

		serialize::LoadResult loaded = flowview::loadGraph(document.string(), factory, nullptr);
		REQUIRE(loaded.issues.empty());

		Evaluation after{loaded.graph};
		SerialScheduler{}.run(loaded.graph, after);
		REQUIRE(boundaryInt(loaded.graph, after, "iterations") == ranBefore);
		REQUIRE(sameBytes(boundaryImage(loaded.graph, after, "result"), expected));

		// And re-saving is byte-identical: a round trip that quietly rewrites a user's file is the
		// failure this guards, and the documents themselves are what is compared.
		const std::filesystem::path again = dir / ("again-" + c.file);
		REQUIRE(flowview::saveGraph(again.string(), loaded.graph, factory));
		REQUIRE(readAll(document) == readAll(again));
	}
}

TEST_CASE("a loop's own index drives its condition and its body", "[flowview][loop][payload]")
{
	// THE blocker M12 exists for: a loop's `index` is an `int`, and before payload types the only
	// Compare in the tree took floats — so a while loop could not be conditioned on its own index
	// from the GUI at all. Here it is, twice over:
	//
	//   index -> Compare<Int> -> continue     the condition, needing NO cast (slice 3)
	//   index -> Cast(Int -> Float) -> sigma  the body, needing one (slice 2)
	//
	// Both through the production factory and the production retype gesture.
	const Factory<Node> factory = sceneFactory();
	Scene scene = buildFoldScene(factory, 100); // a bound the condition is expected to stop short of
	Graph& inner = scene.inner();
	const PortId indexPin = scene.loop().indexPin();

	// --- the condition: index < 3, over Int ------------------------------------------------
	const NodeId compare = inner.add(factory.create("compare"));
	// The factory's preset is Float (what every document written before this carried). Retyping it
	// goes through the same gesture the Inspector's dropdown uses.
	const edit::RetypeResult retyped =
		edit::setPayloadType(inner, compare, example::CompareNode::kValuePayload, &portType<int>());
	REQUIRE(retyped.ok);
	CHECK(retyped.disconnected.empty()); // nothing wired yet, so nothing to cut
	CHECK(inner.node(compare).input(0).type() == typeid(int));

	REQUIRE(wire(inner, inner.boundaryInputNode().id(), indexPin, compare, in0(inner, compare)) == Connection::Ok);
	setDefault<int>(inner.node(compare), inner.node(compare).input(1).id(), 3);
	setParamNamed<example::Comparison>(inner.node(compare), "op", example::Comparison::Less);
	REQUIRE(wire(inner, compare, out0(inner, compare), inner.boundaryOutputNode().id(), scene.loop().continuePin()) ==
			Connection::Ok);

	// --- the body: sigma grows with the index, through a Cast -------------------------------
	const NodeId cast = inner.add(factory.create("cast")); // preset Int -> Float
	REQUIRE(wire(inner, inner.boundaryInputNode().id(), indexPin, cast, in0(inner, cast)) == Connection::Ok);
	const PortId sigma = portNamed(inner.node(scene.blur), Port::Direction::Input, "sigma");
	REQUIRE(sigma != PortId{});
	REQUIRE(wire(inner, cast, out0(inner, cast), scene.blur, sigma) == Connection::Ok);

	Evaluation evaluation{scene.graph};
	SerialScheduler{}.run(scene.graph, evaluation);

	// It stopped on the CONDITION, not on the bound of 100 — which is the fact ADR-0021 built
	// `iterations` to make readable.
	// FOUR, not 100: it stopped on the CONDITION, which is exactly the fact ADR-0021 built
	// `iterations` to make readable. The index runs 0, 1, 2, 3 and `3 < 3` is what ends it, so the
	// iteration at index 3 is the last one that ran.
	const int ran = boundaryInt(scene.graph, evaluation, "iterations");
	CHECK(ran == 4);
	const Image& result = boundaryImage(scene.graph, evaluation, "result");
	CHECK(result.width() > 0);

	SECTION("the cast really feeds the blur")
	{
		// Sabotage-shaped: with the cast's edge cut, sigma falls back to its default and the fold
		// produces a DIFFERENT picture. Every weaker assertion above passes either way.
		Image withCast = result; // a copy, since the second run rebinds the slot it came from

		REQUIRE(inner.disconnect(PortAddress{scene.blur, sigma}));
		Evaluation fresh{scene.graph};
		SerialScheduler{}.run(scene.graph, fresh);
		CHECK_FALSE(sameBytes(withCast, boundaryImage(scene.graph, fresh, "result")));
	}
}
