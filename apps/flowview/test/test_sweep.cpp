// The frame sweep end to end: a folder of real stills rendered to numbered stills, through the
// production `run` path (runGraph), with real PNGs on disk and no driver.
//
// This is M10 slice 2's claim in one test — the host owns the frame loop, one Evaluation is retained
// across the range, and memory is one frame at any range length. What it can assert directly is the
// observable half: the right files appear, they differ from one another, and the missing-frame
// policy does what ADR-0018 says.

#include "clibinders.h"
#include "graphio.h"
#include "runmode.h"
#include "scene.h"

#include <lain/core/range.h>
#include <lain/flow/boundary.h>
#include <lain/flow/example/frameatnode.h>
#include <lain/flow/example/opensequencenode.h>
#include <lain/flow/graph.h>
#include <lain/image/image.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/open.h>
#include <lain/io/video/codecs.h>
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace lain;
namespace fs = std::filesystem;

namespace
{
	// Registered before anything writes a fixture, not merely before the graph runs: writeGrey
	// saves a real PNG through the production encoder, so it needs the codec too.
	void ensureCodecs()
	{
		static const bool once = []
		{
			io::image::registerImageCodecs();
			io::video::registerVideoCodecs();
			io::sequence::registerSequenceOpeners();
			flowview::registerSceneSerialization();
			return true;
		}();
		(void)once;
	}

	fs::path scratchDir(const std::string& name)
	{
		const fs::path dir = fs::temp_directory_path() / ("lain_sweep_" + name);
		fs::remove_all(dir);
		fs::create_directories(dir);
		return dir;
	}

	// A real PNG whose single pixel value identifies the frame — so "the outputs differ" is an
	// assertion about which frame reached which file, not merely about file size.
	void writeGrey(const fs::path& file, std::uint8_t level)
	{
		ensureCodecs();
		lain::image::Image image{2, 2, lain::image::PixelFormat::Gray8, lain::image::ColorSpace::sRGB};
		for (std::size_t i = 0; i < image.byteSize(); ++i)
			image.data()[i] = level;
		REQUIRE(io::image::save(file.string(), image));
	}

	core::Factory<flow::Node> sweepFactory()
	{
		ensureCodecs();
		core::Factory<flow::Node> factory;
		flowview::registerExampleNodes(factory, 8);
		return factory;
	}

	// stills : FrameSequence ─► FrameAt ─► result : Image
	//                              ▲
	// frame : FramePosition ───────┘        (bound once per iteration by the sweep)
	std::string buildSweepGraph(const fs::path& dir, const core::Factory<flow::Node>& factory)
	{
		flow::Graph graph;
		flow::GroupInputNode& in = graph.boundaryInputNode();
		const flow::PortId stills = in.addBoundary<media::FrameSequence>("stills");
		const flow::PortId frame = in.addBoundary<media::FramePosition>("frame");

		flow::GroupOutputNode& out = graph.boundaryOutputNode();
		const flow::PortId result = out.addBoundary<lain::image::Image>("result");

		const flow::NodeId at = graph.add<flow::example::FrameAtNode>();
		const flow::Node& node = graph.node(at);
		REQUIRE(graph.connect(flow::PortAddress{in.id(), stills},
							  flow::PortAddress{at, node.input(0).id()}) == flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{in.id(), frame},
							  flow::PortAddress{at, node.input(1).id()}) == flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{at, node.output(0).id()},
							  flow::PortAddress{out.id(), result}) == flow::Connection::Ok);

		const std::string path = (dir / "sweep.json").string();
		REQUIRE(flowview::saveGraph(path, graph, factory));
		return path;
	}

	// `range` is already a core::Range, not a string: `--frame` is a typed cli option, so a
	// malformed range never reaches runGraph — the command line refuses it first. That is why there
	// is no "a range that is not one" case here any more; it lives in core's Range::parse tests,
	// where it is now the only place it can happen.
	flowview::RunOptions sweepOptions(const std::string& graphPath, const fs::path& stills, const fs::path& outPattern,
									  std::optional<lain::core::Range> range)
	{
		flowview::RunOptions options;
		options.graphPath = graphPath;
		options.frameRange = std::move(range);
		options.bindings = {"--stills", stills.string(), "--result", outPattern.string()};
		return options;
	}
} // namespace

TEST_CASE("a folder of stills sweeps to numbered stills", "[flowview][sweep]")
{
	const fs::path dir = scratchDir("basic");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 4; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(40 + i * 20));

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const std::string graphPath = buildSweepGraph(dir, factory);
	const fs::path pattern = dir / "out.####.png";

	REQUIRE(flowview::runGraph(sweepOptions(graphPath, stills, pattern, lain::core::Range{0, 3}), factory, binders) == 0);

	// One numbered file per frame, and each carries the pixel value of the still it came from —
	// so the sweep really did rebind the position each iteration rather than rendering frame 0
	// four times.
	for (int i = 0; i < 4; ++i)
	{
		const fs::path file = dir / ("out.000" + std::to_string(i) + ".png");
		REQUIRE(fs::exists(file));

		const auto loaded = io::image::load(file.string());
		REQUIRE(loaded.has_value());
		CHECK(loaded->data()[0] == static_cast<std::uint8_t>(40 + i * 20));
	}
}

TEST_CASE("a stride renders only the frames it names", "[flowview][sweep]")
{
	const fs::path dir = scratchDir("stride");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 6; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(10 + i));

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const std::string graphPath = buildSweepGraph(dir, factory);
	REQUIRE(flowview::runGraph(sweepOptions(graphPath, stills, dir / "s.####.png", lain::core::Range{0, 5, 2}), factory, binders) == 0);

	CHECK(fs::exists(dir / "s.0000.png"));
	CHECK(fs::exists(dir / "s.0002.png"));
	CHECK(fs::exists(dir / "s.0004.png"));
	CHECK_FALSE(fs::exists(dir / "s.0001.png"));
	CHECK_FALSE(fs::exists(dir / "s.0005.png"));
}

TEST_CASE("the missing-frame policy decides whether a render stops", "[flowview][sweep]")
{
	const fs::path dir = scratchDir("missing");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 2; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(60 + i));

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	const std::string graphPath = buildSweepGraph(dir, factory);

	SECTION("stop is the default, and it exits non-zero")
	{
		// Frames 2 and 3 are past the end of a two-frame sequence, so they render nothing. A
		// truncated render must be VISIBLY truncated: the two frames that worked are on disk, and
		// the exit code says the rest are missing.
		const int status = flowview::runGraph(sweepOptions(graphPath, stills, dir / "stop.####.png", lain::core::Range{0, 3}), factory, binders);
		CHECK(status != 0);

		CHECK(fs::exists(dir / "stop.0000.png"));
		CHECK(fs::exists(dir / "stop.0001.png"));
		CHECK_FALSE(fs::exists(dir / "stop.0002.png"));
	}

	SECTION("skip is opt-in, and the gap in the numbering is the record")
	{
		flowview::RunOptions options = sweepOptions(graphPath, stills, dir / "skip.####.png", lain::core::Range{0, 3});
		options.skipMissingFrames = true;

		CHECK(flowview::runGraph(options, factory, binders) == 0);
		CHECK(fs::exists(dir / "skip.0000.png"));
		CHECK(fs::exists(dir / "skip.0001.png"));

		// Numbered stills CAN represent a hole, and this is it. A video cannot, which is why the
		// same flag will have to mean something stricter there.
		CHECK_FALSE(fs::exists(dir / "skip.0002.png"));
	}
}

TEST_CASE("a render refuses what could not name its frames", "[flowview][sweep]")
{
	const fs::path dir = scratchDir("refuse");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	writeGrey(stills / "f0.png", 90);

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	const std::string graphPath = buildSweepGraph(dir, factory);

	SECTION("a multi-frame output with no #### field")
	{
		// Writing every frame to one path exits reporting success while having destroyed the
		// correspondence between input and output frames — so it is refused BEFORE anything runs,
		// which is why nothing is written even though frame 0 would have rendered.
		CHECK(flowview::runGraph(sweepOptions(graphPath, stills, dir / "flat.png", lain::core::Range{0, 1}), factory, binders) != 0);
		CHECK_FALSE(fs::exists(dir / "flat.png"));
	}

	SECTION("but one frame may write a plain path")
	{
		CHECK(flowview::runGraph(sweepOptions(graphPath, stills, dir / "solo.png", lain::core::Range{0, 0}), factory, binders) == 0);
		CHECK(fs::exists(dir / "solo.png"));
	}
}

TEST_CASE("one frame is a range of one, and needs no pattern", "[flowview][sweep]")
{
	const fs::path dir = scratchDir("single");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	writeGrey(stills / "f0.png", 77);
	writeGrey(stills / "f1.png", 88);

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	const std::string graphPath = buildSweepGraph(dir, factory);

	// `--frame 1` is a one-frame range, which is how a caller renders a single chosen frame. It is
	// also the ONLY way to set a frame position from the cli, and deliberately so: a boundary pin
	// of this type is found by TYPE, so its name never has to be typed — which matters because the
	// natural name for one is `frame`, and `--frame` is this option.
	flowview::RunOptions options = sweepOptions(graphPath, stills, dir / "one.png", lain::core::Range{1, 1});
	CHECK(flowview::runGraph(options, factory, binders) == 0);

	// No #### field required: one write cannot collide with itself.
	REQUIRE(fs::exists(dir / "one.png"));
	const auto loaded = io::image::load((dir / "one.png").string());
	REQUIRE(loaded.has_value());
	CHECK(loaded->data()[0] == 88); // frame 1, not frame 0
}

TEST_CASE("an unbound frame position suppresses rather than guessing", "[flowview][sweep]")
{
	const fs::path dir = scratchDir("unbound");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	writeGrey(stills / "f0.png", 55);

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	const std::string graphPath = buildSweepGraph(dir, factory);

	// With no --frame at all, the frame-position boundary pin carries no value. FrameAt's position
	// input is CONNECTED to it, so its default does not apply (a default seeds an unconnected input
	// only — ADR-0005, which is what stops a gated-off upstream being silently replaced), the input
	// is Required, and the node does not run. Nothing is written, and that is the correct answer
	// rather than a render of frame 0.
	flowview::RunOptions options = sweepOptions(graphPath, stills, dir / "none.png", std::nullopt);
	CHECK(flowview::runGraph(options, factory, binders) == 0);
	CHECK_FALSE(fs::exists(dir / "none.png"));
}
