// The frame-sequence nodes (M10 slice 2): OpenSequence, FrameAt, ClipSequence.
//
// Real files through the production opener, decoded by a FAKE reader registered into the real
// registry — the same discipline test_nodes.cpp uses for LoadImageNode. No codec, no driver, and
// the path under test is the one a graph actually takes.

#include <lain/flow/evaluation.h>
#include <lain/flow/example/clipsequencenode.h>
#include <lain/flow/example/frameatnode.h>
#include <lain/flow/example/opensequencenode.h>
#include <lain/flow/graph.h>
#include <lain/flow/nodes/constant.h>
#include <lain/flow/scheduler.h>
#include <lain/io/image/load.h>
#include <lain/io/image/reader.h>
#include <lain/io/sequence/open.h>
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace lain;
using namespace lain::flow;

namespace
{
	constexpr const char* extension = "seqnode";

	// One byte in, a 4x1 image whose first pixel carries it — so a test can name which file it got.
	class TagReader : public io::image::ImageReader
	{
	public:
		lain::image::Image decode(const memory::Buffer& bytes) const override
		{
			if (bytes.empty())
				return {};
			lain::image::Image image{4, 1, lain::image::PixelFormat::RGBA8};
			image.data()[0] = static_cast<std::uint8_t>(*bytes.data());
			return image;
		}
	};

	std::uint8_t tagOf(const lain::image::Image& image) { return image.valid() ? image.data()[0] : 0; }

	class TempDir
	{
	public:
		explicit TempDir(int frames)
		{
			static int counter = 0;
			m_path = std::filesystem::temp_directory_path() / ("lain_seqnode_" + std::to_string(counter++));
			std::filesystem::remove_all(m_path);
			std::filesystem::create_directories(m_path);
			for (int i = 0; i < frames; ++i)
			{
				std::ofstream out(m_path / ("f" + std::to_string(i) + "." + extension), std::ios::binary | std::ios::trunc);
				const char byte = static_cast<char>(10 + i); // distinguishable, and not 0
				out.write(&byte, 1);
			}
		}

		~TempDir()
		{
			std::error_code error;
			std::filesystem::remove_all(m_path, error);
		}

		TempDir(const TempDir&) = delete;
		TempDir& operator=(const TempDir&) = delete;

		std::string string() const { return m_path.string(); }

	private:
		std::filesystem::path m_path;
	};

	struct Registration
	{
		Registration()
		{
			io::image::readerRegistry().registerType<TagReader>(extension);
			// The opener registry has to be wired too, or io::sequence::open has nothing to
			// dispatch to: OpenSequence names a MEDIUM-neutral entry point, and which media exist
			// is the host's to say (WORK.md M10 slice 5).
			io::sequence::registerSequenceOpeners();
		}
	};
	const Registration registration;

	// The value on a node's single output, after pulling it.
	const PortValue& pull(Graph& graph, Evaluation& evaluation, NodeId node, std::size_t output = 0)
	{
		SerialScheduler{}.evaluate(graph, evaluation, node);
		return evaluation.value(PortAddress{node, graph.node(node).output(output).id()});
	}
} // namespace

TEST_CASE("OpenSequence opens a folder as a sequence", "[flow][sequence]")
{
	TempDir dir{3};

	Graph graph;
	const NodeId open = graph.add<example::OpenSequenceNode>(dir.string());
	Evaluation evaluation{graph};

	const PortValue& out = pull(graph, evaluation, open);
	REQUIRE(out.holds<media::FrameSequence>());
	CHECK(out.get<media::FrameSequence>().size() == 3);
}

TEST_CASE("OpenSequence distinguishes a missing folder from an empty one", "[flow][sequence]")
{
	SECTION("a folder that is not there suppresses")
	{
		Graph graph;
		const NodeId open = graph.add<example::OpenSequenceNode>("/nonexistent/lain/seqnode");
		Evaluation evaluation{graph};

		// No value at all, so everything downstream is unready and does not run (ADR-0007) —
		// which is what "there is no footage" should mean.
		CHECK(pull(graph, evaluation, open).empty());
	}

	SECTION("a folder holding nothing readable is an empty sequence, which is a value")
	{
		TempDir dir{0};
		Graph graph;
		const NodeId open = graph.add<example::OpenSequenceNode>(dir.string());
		Evaluation evaluation{graph};

		const PortValue& out = pull(graph, evaluation, open);
		REQUIRE(out.holds<media::FrameSequence>());
		CHECK(out.get<media::FrameSequence>().empty());
	}
}

TEST_CASE("FrameAt decodes the frame its position names", "[flow][sequence]")
{
	TempDir dir{4};

	Graph graph;
	const NodeId open = graph.add<example::OpenSequenceNode>(dir.string());
	const NodeId at = graph.add<example::FrameAtNode>();
	REQUIRE(graph.connect(open, 0, at, 0) == Connection::Ok);

	Evaluation evaluation{graph};

	SECTION("the default position renders frame 0 without anything driving it")
	{
		// Default{FramePosition{0}}: a sequence graph shows something the moment it is opened,
		// rather than sitting suppressed until a host binds a position.
		const PortValue& image = pull(graph, evaluation, at, 0);
		REQUIRE(image.holds<lain::image::Image>());
		CHECK(tagOf(image.get<lain::image::Image>()) == 10);
	}

	SECTION("the frame pin carries identity beside the pixels")
	{
		SerialScheduler{}.evaluate(graph, evaluation, at);
		const PortValue& ref = evaluation.value(PortAddress{at, graph.node(at).output(1).id()});
		REQUIRE(ref.holds<media::FrameRef>());
		CHECK(ref.get<media::FrameRef>().ordinal == 0);
		CHECK_FALSE(ref.get<media::FrameRef>().source.empty());
	}

	SECTION("a wired position overrides the default")
	{
		const NodeId position = graph.add<ConstantNode<media::FramePosition>>();
		Node& node = graph.node(position);
		REQUIRE(node.setParam(node.param(0).id(), media::FramePosition{2}));
		REQUIRE(graph.connect(position, 0, at, 1) == Connection::Ok);

		Evaluation wired{graph};
		const PortValue& image = pull(graph, wired, at, 0);
		REQUIRE(image.holds<lain::image::Image>());
		CHECK(tagOf(image.get<lain::image::Image>()) == 12); // the third file
	}

	SECTION("a position past the end yields an invalid image, not a cleared output")
	{
		const NodeId position = graph.add<ConstantNode<media::FramePosition>>();
		Node& node = graph.node(position);
		REQUIRE(node.setParam(node.param(0).id(), media::FramePosition{99}));
		REQUIRE(graph.connect(position, 0, at, 1) == Connection::Ok);

		Evaluation wired{graph};
		const PortValue& image = pull(graph, wired, at, 0);

		// The sequence answered, and what it said was "not that one". That is different from
		// nothing upstream, which clears.
		REQUIRE(image.holds<lain::image::Image>());
		CHECK_FALSE(image.get<lain::image::Image>().valid());
	}
}

TEST_CASE("FrameAt clears both outputs when nothing is upstream", "[flow][sequence]")
{
	Graph graph;
	const NodeId at = graph.add<example::FrameAtNode>();
	Evaluation evaluation{graph};

	SerialScheduler{}.evaluate(graph, evaluation, at);
	CHECK(evaluation.value(PortAddress{at, graph.node(at).output(0).id()}).empty());
	CHECK(evaluation.value(PortAddress{at, graph.node(at).output(1).id()}).empty());
}

TEST_CASE("ClipSequence re-bases position and preserves identity", "[flow][sequence]")
{
	TempDir dir{6};

	Graph graph;
	const NodeId open = graph.add<example::OpenSequenceNode>(dir.string());
	const NodeId clip = graph.add<example::ClipSequenceNode>(2, 3);
	REQUIRE(graph.connect(open, 0, clip, 0) == Connection::Ok);

	Evaluation evaluation{graph};
	const PortValue& out = pull(graph, evaluation, clip);
	REQUIRE(out.holds<media::FrameSequence>());

	const media::FrameSequence& clipped = out.get<media::FrameSequence>();
	REQUIRE(clipped.size() == 3);

	// Position 0 of the clip is still ordinal 2 of its source — the distinction the whole model
	// rests on — and the pixels follow identity, not position.
	CHECK(clipped.frame(0).ordinal == 2);
	CHECK(tagOf(clipped.image(0)) == 12);

	// Asking past the end clamps rather than refusing: an ordinary thing to do at the end of a
	// timeline, and the honest answer is what there was.
	const NodeId greedy = graph.add<example::ClipSequenceNode>(4, 100);
	REQUIRE(graph.connect(open, 0, greedy, 0) == Connection::Ok);
	Evaluation second{graph};
	CHECK(pull(graph, second, greedy).get<media::FrameSequence>().size() == 2);
}
