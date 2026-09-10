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
#include <lain/flow/example/convertnode.h>
#include <lain/flow/example/frameatnode.h>
#include <lain/flow/example/opensequencenode.h>
#include <lain/flow/graph.h>
#include <lain/image/convert.h>
#include <lain/image/image.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/io/video/open.h>
#include <lain/io/video/save.h>
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
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
		const fs::path dir = lain::testing::scratchDir() / ("sweep-" + name);
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

// --- rendering to a video ------------------------------------------------------
//
// These are M10 slice 6c's claim: one writer per video-valued output, opened on the first frame
// that produced something, held across the whole range, and finished on EVERY exit path. The cases
// that need an encoder skip in a build with no video codec plugin; the ones that are about names
// and rates run in both configurations, which is where the interesting refusals are anyway.

namespace
{
	// Is there a video writer in this build at all? Asked through the production seam rather than
	// by testing a CMake flag, because what matters is whether a backend REGISTERED itself.
	bool haveVideoWriter()
	{
		ensureCodecs();
		const fs::path probe = lain::testing::scratchPath("sweep-probe", ".mkv");
		lain::media::FrameSpec spec;
		spec.extent = {2, 2};
		spec.pixelFormat = lain::image::PixelFormat::Gray8;
		spec.colorSpace = lain::image::ColorSpace::sRGB;
		spec.rate = {24, 1};

		lain::io::video::VideoWriterOptions options;
		options.codec = lain::io::video::VideoCodec::FFV1;
		auto writer = lain::io::video::openWriter(probe.string(), spec, options);
		const bool have = writer != nullptr;
		if (writer)
			(void)writer->finish();
		std::error_code error;
		fs::remove(probe, error);
		return have;
	}

	// stills : FrameSequence ─► FrameAt ─► Convert ─► result : Image
	//                              ▲
	// frame : FramePosition ───────┘
	//
	// The Convert is not decoration: lain's PNG writer records no colour chunk, so a still saved
	// and reloaded comes back Unspecified — and the video writer refuses Unspecified rather than
	// guessing, because writing a guess into a file outlives the guess. Convert is how a graph
	// complies, which is the whole reason it exists.
	std::string buildVideoGraph(const fs::path& dir, const core::Factory<flow::Node>& factory)
	{
		flow::Graph graph;
		flow::GroupInputNode& in = graph.boundaryInputNode();
		const flow::PortId stills = in.addBoundary<media::FrameSequence>("stills");
		const flow::PortId frame = in.addBoundary<media::FramePosition>("frame");

		flow::GroupOutputNode& out = graph.boundaryOutputNode();
		const flow::PortId result = out.addBoundary<lain::image::Image>("result");

		const flow::NodeId at = graph.add<flow::example::FrameAtNode>();
		const flow::NodeId convert = graph.add<flow::example::ConvertNode>();
		const flow::Node& atNode = graph.node(at);
		const flow::Node& convertNode = graph.node(convert);

		REQUIRE(graph.connect(flow::PortAddress{in.id(), stills},
							  flow::PortAddress{at, atNode.input(0).id()}) == flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{in.id(), frame},
							  flow::PortAddress{at, atNode.input(1).id()}) == flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{at, atNode.output(0).id()},
							  flow::PortAddress{convert, convertNode.input(0).id()}) == flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{convert, convertNode.output(0).id()},
							  flow::PortAddress{out.id(), result}) == flow::Connection::Ok);

		const std::string path = (dir / "video.json").string();
		REQUIRE(flowview::saveGraph(path, graph, factory));
		return path;
	}

	// The same graph WITHOUT the Convert — stills : FrameSequence -> FrameAt -> result : Image.
	//
	// This is only buildable because the PNG writer records the ColorSpace (ADR-0020): writeGrey
	// saves an sRGB still, so the sequence opens as sRGB and io::video::canEncode accepts it. Before
	// that, a reloaded still was Unspecified and the writer refused, which is the entire reason
	// ConvertNode sits in the graph above.
	std::string buildVideoGraphWithoutConvert(const fs::path& dir, const core::Factory<flow::Node>& factory)
	{
		flow::Graph graph;
		flow::GroupInputNode& in = graph.boundaryInputNode();
		const flow::PortId stills = in.addBoundary<media::FrameSequence>("stills");
		const flow::PortId frame = in.addBoundary<media::FramePosition>("frame");

		flow::GroupOutputNode& out = graph.boundaryOutputNode();
		const flow::PortId result = out.addBoundary<lain::image::Image>("result");

		const flow::NodeId at = graph.add<flow::example::FrameAtNode>();
		const flow::Node& atNode = graph.node(at);

		REQUIRE(graph.connect(flow::PortAddress{in.id(), stills},
							  flow::PortAddress{at, atNode.input(0).id()}) == flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{in.id(), frame},
							  flow::PortAddress{at, atNode.input(1).id()}) == flow::Connection::Ok);
		REQUIRE(graph.connect(flow::PortAddress{at, atNode.output(0).id()},
							  flow::PortAddress{out.id(), result}) == flow::Connection::Ok);

		const std::string path = (dir / "video-noconvert.json").string();
		REQUIRE(flowview::saveGraph(path, graph, factory));
		return path;
	}

	flowview::RunOptions videoOptions(const std::string& graphPath, const fs::path& stills, const fs::path& out,
									  lain::core::Range range)
	{
		flowview::RunOptions options = sweepOptions(graphPath, stills, out, range);
		options.videoCodec = "ffv1"; // lossless, and the only family a byte-exact check can use
		// Stills genuinely have no rate of their own, so a video output must be told one — which is
		// ADR-0018's "required explicitly when there is no sequence input", reached from the other
		// direction: there IS a sequence, and it still cannot say.
		options.outputRate = lain::media::FrameRate{24, 1};
		return options;
	}
} // namespace

TEST_CASE("a folder of stills renders into one video file", "[flowview][sweep]")
{
	if (!haveVideoWriter())
		SKIP("this build has no video codec plugin");

	const fs::path dir = scratchDir("tovideo");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 4; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(40 + i * 20));

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const std::string graphPath = buildVideoGraph(dir, factory);
	const fs::path out = dir / "out.mkv";

	// A video output needs NO #### field: one container holds the whole range.
	REQUIRE(flowview::runGraph(videoOptions(graphPath, stills, out, lain::core::Range{0, 3}), factory, binders) == 0);
	REQUIRE(fs::exists(out));

	// Reopened through the production reader — the round trip is the assertion, and the frames
	// must be in order and carry the value of the still each came from.
	const auto sequence = io::video::open(out.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->size() == 4);

	// The rate SURVIVES, but this deliberately does not assert its exact value. Matroska's own
	// timebase is what a rate has to fit into, and over a clip this short libavformat's rate
	// estimate is visibly approximate (a 4-frame file reopens at 24.08 rather than 24). Exact
	// round-trip fidelity is pinned where it belongs — test_ffmpegwriter.cpp's 6-frame case, which
	// checks the declaration itself — and asserting it a second time here, more weakly, would only
	// record a heuristic.
	CHECK(sequence->spec().rate.specified());

	// The expected value is COMPUTED THROUGH THE SAME PUBLIC CONVERSION rather than written down:
	// the graph's Convert declares the untagged still sRGB and converts it to BT709, which really
	// does move the pixel values, and a hardcoded number here would either duplicate the colour
	// math or quietly pin whatever the code happened to produce.
	for (std::size_t i = 0; i < 4; ++i)
	{
		const lain::image::Image frame = sequence->image(i);
		REQUIRE(frame.valid());

		lain::image::Image still{2, 2, lain::image::PixelFormat::Gray8, lain::image::ColorSpace::sRGB};
		for (std::size_t b = 0; b < still.byteSize(); ++b)
			still.data()[b] = static_cast<std::uint8_t>(40 + i * 20);
		// Format then space, which is the order ConvertNode applies them in.
		const lain::image::Image expected =
			lain::image::convert(lain::image::convert(still, lain::image::PixelFormat::RGB8),
								 lain::image::ColorSpace::BT709);

		CHECK(static_cast<int>(frame.data()[0]) == static_cast<int>(expected.data()[0]));
	}
}

TEST_CASE("a video output refuses a #### field, which asks for one file per frame", "[flowview][sweep]")
{
	// The exact inverse of the still rule, and it needs no codec: it is about the NAME.
	const fs::path dir = scratchDir("videopattern");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 2; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(30 + i));

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const std::string graphPath = buildVideoGraph(dir, factory);
	CHECK(flowview::runGraph(videoOptions(graphPath, stills, dir / "out.####.mkv", lain::core::Range{0, 1}), factory,
							 binders) != 0);
	CHECK_FALSE(fs::exists(dir / "out.0000.mkv"));
}

TEST_CASE("a video render with no rate to take is refused, naming --rate", "[flowview][sweep]")
{
	// A folder of stills has no rate of its own, so nothing can supply one — ADR-0018's "required
	// explicitly when there is no sequence input", and it must be a refusal rather than a guess,
	// because a container states a timebase whether or not anyone chose it. No codec needed: the
	// rate is established before a writer is opened.
	const fs::path dir = scratchDir("norate");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 2; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(30 + i));

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const std::string graphPath = buildVideoGraph(dir, factory);
	flowview::RunOptions options = sweepOptions(graphPath, stills, dir / "out.mkv", lain::core::Range{0, 1});
	options.videoCodec = "ffv1";
	CHECK(flowview::runGraph(options, factory, binders) != 0);
}

TEST_CASE("a render that stops leaves a valid, visibly truncated video", "[flowview][sweep]")
{
	if (!haveVideoWriter())
		SKIP("this build has no video codec plugin");

	// THE case the single-exit finishWrites exists for. The range runs past the end of the
	// sequence, so a frame produces nothing and the default policy stops — and the file must still
	// have been finalised, because one that was never finalised is not a video at all, which is a
	// worse failure than the truncation it was meant to report.
	const fs::path dir = scratchDir("videostop");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 2; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(50 + i * 30));

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const std::string graphPath = buildVideoGraph(dir, factory);
	const fs::path out = dir / "short.mkv";

	CHECK(flowview::runGraph(videoOptions(graphPath, stills, out, lain::core::Range{0, 3}), factory, binders) != 0);

	// Truncated AND openable: exactly the two frames that rendered.
	REQUIRE(fs::exists(out));
	const auto sequence = io::video::open(out.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->size() == 2);
}

TEST_CASE("skipping a missing frame closes the gap a video cannot hold", "[flowview][sweep]")
{
	if (!haveVideoWriter())
		SKIP("this build has no video codec plugin");

	// The asymmetry ADR-0018 ends on. Numbered stills record a hole as a gap in the numbering; a
	// video closes up, so the output is one frame shorter. Frame 1 is unreadable, so it produces
	// nothing, and the remaining two frames must be adjacent in the file.
	const fs::path dir = scratchDir("videoskip");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	writeGrey(stills / "f0.png", 60);
	writeGrey(stills / "f1.png", 90);
	writeGrey(stills / "f2.png", 120);

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const std::string graphPath = buildVideoGraph(dir, factory);
	const fs::path out = dir / "skipped.mkv";

	flowview::RunOptions options = videoOptions(graphPath, stills, out, lain::core::Range{0, 3});
	options.skipMissingFrames = true;
	CHECK(flowview::runGraph(options, factory, binders) == 0);

	const auto sequence = io::video::open(out.string());
	REQUIRE(sequence.has_value());
	// Three stills rendered, the fourth position produced nothing and was dropped rather than
	// leaving a hole — the file is shorter, not gappy.
	CHECK(sequence->size() == 3);
}

TEST_CASE("the codec family asked for is the one used, never a substitute", "[flowview][sweep]")
{
	if (!haveVideoWriter())
		SKIP("this build has no video codec plugin");

	// A REGRESSION TEST FOR A REAL BUG, and the shape is what makes it one. QuickTime cannot carry
	// FFV1 but carries h264 happily, so asking for ffv1 into a .mov must REFUSE — while a writer
	// that quietly fell back to the delivery default would succeed. The first --codec used a CLI11
	// CheckedTransformer, which rewrote the name to the enum's underlying number; runmode then
	// failed to parse it and defaulted to auto, so `--codec ffv1` rendered h264 and said so only in
	// a log line nobody reads. Found by driving the real binary, which is the only place it showed.
	const fs::path dir = scratchDir("videocodec");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 2; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(70 + i * 10));

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);
	const std::string graphPath = buildVideoGraph(dir, factory);

	CHECK(flowview::runGraph(videoOptions(graphPath, stills, dir / "no.mov", lain::core::Range{0, 1}), factory,
							 binders) != 0);

	// And a name that is not a family at all refuses rather than resolving to the default — the
	// same rule, reached from the other side.
	flowview::RunOptions bogus = videoOptions(graphPath, stills, dir / "bogus.mkv", lain::core::Range{0, 1});
	bogus.videoCodec = "h265"; // close enough to be a plausible typo for "hevc"
	CHECK(flowview::runGraph(bogus, factory, binders) != 0);
}

TEST_CASE("a tagged still reaches a video writer with no Convert in the graph", "[flowview][sweep]")
{
	// The end-to-end point of ADR-0020. io::video::canEncode refuses Unspecified by name, and every
	// still lain wrote used to come back Unspecified because no image writer recorded a colour tag —
	// so the stills->video path structurally required a ConvertNode to declare a space by hand. The
	// PNG writer now records it, so the tag survives the round trip and the graph is just the work.
	if (!haveVideoWriter())
		SKIP("this build has no video codec plugin");

	const fs::path dir = scratchDir("noconvert");
	const fs::path stills = dir / "frames";
	fs::create_directories(stills);
	for (int i = 0; i < 4; ++i)
		writeGrey(stills / ("f" + std::to_string(i) + ".png"), static_cast<std::uint8_t>(40 + i * 20));

	// The claim rests on this: the reloaded still carries the tag it was saved with.
	const auto reloaded = io::image::load((stills / "f0.png").string());
	REQUIRE(reloaded.has_value());
	REQUIRE(reloaded->colorSpace() == lain::image::ColorSpace::sRGB);

	const core::Factory<flow::Node> factory = sweepFactory();
	flowview::BoundaryBinders binders;
	flowview::registerBoundaryBinders(binders);

	const std::string graphPath = buildVideoGraphWithoutConvert(dir, factory);
	const fs::path out = dir / "out.mkv";
	CHECK(flowview::runGraph(videoOptions(graphPath, stills, out, lain::core::Range{0, 3}), factory, binders) == 0);
	CHECK(fs::exists(out));
	CHECK(fs::file_size(out) > 0);
}
