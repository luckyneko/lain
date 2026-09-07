// Unit: the example nodes produce/consume a lain::image::Image through the graph's pull
// path, and an edge carries an image between them. Pure CPU — no device, no [gpu] SKIP.

#include "lain/flow/evaluation.h"
#include "lain/flow/graph.h"
#include "lain/flow/nodes/constant.h"
#include "lain/flow/scheduler.h"

#include <lain/flow/example/blurnode.h>
#include <lain/flow/example/comparenode.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/example/imagedifferencenode.h>
#include <lain/flow/example/loadimagenode.h>
#include <lain/flow/example/tintnode.h>
#include <lain/image/color.h> // image::ColorRGBf (the tint param type, in tests)
#include <lain/image/image.h>
#include <lain/io/image/load.h>
#include <lain/io/image/reader.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

struct Rgba
{
	std::uint8_t r, g, b, a;
};

// The (r,g,b,a) at a texel index into an RGBA8 image.
static Rgba pixel(const lain::image::Image& img, std::size_t texel)
{
	const auto* px = img.data();
	return {px[texel * 4 + 0], px[texel * 4 + 1], px[texel * 4 + 2], px[texel * 4 + 3]};
}

TEST_CASE("GradientNode emits an RGBA gradient image on its port", "[flow]")
{
	constexpr int kSize = 64;

	using namespace lain::flow;
	Graph graph;
	const NodeId id = graph.add<example::GradientNode>(kSize, kSize);

	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, id); // pull: runs compute() — fills the image

	const lain::flow::PortValue& out = e.value(PortAddress{id, graph.node(id).output(0).id()});
	REQUIRE(out.holds<lain::image::Image>());
	const lain::image::Image& img = out.get<lain::image::Image>();
	REQUIRE(img.width() == kSize);
	REQUIRE(img.height() == kSize);

	// RGBA8 order [R, G, B, A]. Top-left (0,0): ramps start at 0; B constant 128.
	const Rgba tl = pixel(img, 0);
	CHECK(tl.r == 0);
	CHECK(tl.g == 0);
	CHECK(tl.b == 128);
	CHECK(tl.a == 255);

	// Bottom-right (kSize-1, kSize-1): both ramps at full.
	const Rgba br = pixel(img, static_cast<std::size_t>(kSize) * kSize - 1);
	CHECK(br.r == 255);
	CHECK(br.g == 255);
	CHECK(br.b == 128);
}

TEST_CASE("an image flows through an edge: gradient -> tint", "[flow]")
{
	constexpr int kSize = 64;

	using namespace lain::flow;
	Graph graph;
	const NodeId gradient = graph.add<example::GradientNode>(kSize, kSize);
	const NodeId tint = graph.add<example::TintNode>(1.0f, 0.5f, 0.5f); // keep R, halve G/B
	REQUIRE(graph.connect(gradient, 0, tint, 0) == Connection::Ok);

	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, tint); // pulls gradient upstream, then tint

	const lain::flow::PortValue& out = e.value(PortAddress{tint, graph.node(tint).output(0).id()});
	REQUIRE(out.holds<lain::image::Image>());
	const lain::image::Image& img = out.get<lain::image::Image>();

	// Gradient TL=(0,0,128), BR=(255,255,128). Tint (1.0, 0.5, 0.5) keeps R, halves G/B:
	// TL -> (0, 0, 64), BR -> (255, 127, 64). Alpha preserved at 255.
	const Rgba tl = pixel(img, 0);
	CHECK(tl.r == 0);
	CHECK(tl.g == 0);
	CHECK(tl.b == 64);
	CHECK(tl.a == 255);

	const Rgba br = pixel(img, static_cast<std::size_t>(kSize) * kSize - 1);
	CHECK(br.r == 255);
	CHECK(br.g == 127); // 255 * 0.5 = 127.5 -> 127
	CHECK(br.b == 64);	// 128 * 0.5 = 64
}

TEST_CASE("editing a TintNode param changes its output", "[flow]")
{
	using namespace lain::flow;
	Graph graph;
	const NodeId gradient = graph.add<example::GradientNode>(2, 2);
	const NodeId tint = graph.add<example::TintNode>(1.0f, 1.0f, 1.0f); // identity
	REQUIRE(graph.connect(gradient, 0, tint, 0) == Connection::Ok);

	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, tint);
	CHECK(pixel(e.value(PortAddress{tint, graph.node(tint).output(0).id()}).get<lain::image::Image>(), 0).b == 128); // gradient blue, unchanged

	// Edit the single "tint" ColorRGBf param (halve blue), then re-eval. No markDirty: setParam
	// commits and invalidates as one operation, which is the whole point of the seam.
	lain::flow::Node& tintNode = graph.node(tint);
	REQUIRE(tintNode.setParam(tintNode.param(0).id(), lain::image::ColorRGBf(1.0f, 1.0f, 0.5f)));
	SerialScheduler{}.evaluate(graph, e, tint); // the same evaluation notices the version bump
	CHECK(pixel(e.value(PortAddress{tint, graph.node(tint).output(0).id()}).get<lain::image::Image>(), 0).b == 64);
}

TEST_CASE("transform nodes handle a non-RGBA8 input (e.g. a loaded RGB8 image)", "[flow]")
{
	using namespace lain::flow;
	// An RGB8 image — 3 bytes/pixel, like a decoded JPEG. A node that assumes RGBA8 would
	// index past the buffer and crash; both must normalise first.
	const lain::image::Image rgb(4, 4, lain::image::PixelFormat::RGB8, lain::image::ColorSpace::sRGB);

	// Driven through a graph rather than by hand: a node's values live in an evaluation now, so
	// feeding one means binding at the boundary and letting the scheduler populate its input.
	const auto through = [&rgb](Graph& graph, NodeId id)
	{
		GroupInputNode& boundary = graph.boundaryInputNode();
		const PortId pin = boundary.addBoundary<lain::image::Image>("source");
		REQUIRE(graph.connect(PortAddress{boundary.id(), pin}, PortAddress{id, graph.node(id).input(0).id()}) == Connection::Ok);

		Evaluation e{graph};
		PortValue v;
		v.set<lain::image::Image>(rgb);
		e.bind(PortAddress{boundary.id(), pin}, std::move(v));
		SerialScheduler{}.evaluate(graph, e, id);
		return e.value(PortAddress{id, graph.node(id).output(0).id()}).get<lain::image::Image>();
	};

	Graph tintGraph;
	const lain::image::Image tinted = through(tintGraph, tintGraph.add<example::TintNode>(1.0f, 0.5f, 0.5f));
	REQUIRE(tinted.valid()); // must not have overrun the RGB8 buffer
	REQUIRE(tinted.pixelFormat() == lain::image::PixelFormat::RGBA8);

	Graph blurGraph;
	REQUIRE(through(blurGraph, blurGraph.add<example::BlurNode>(1, 1.0f)).valid());
}

TEST_CASE("BlurNode runs the op catalog through an edge: gradient -> blur", "[flow]")
{
	constexpr int kSize = 64;

	using namespace lain::flow;
	Graph graph;
	const NodeId gradient = graph.add<example::GradientNode>(kSize, kSize);
	const NodeId blur = graph.add<example::BlurNode>(2, 1.5f);
	REQUIRE(graph.connect(gradient, 0, blur, 0) == Connection::Ok);

	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, blur); // pulls gradient upstream, then blurs

	const lain::flow::PortValue& out = e.value(PortAddress{blur, graph.node(blur).output(0).id()});
	REQUIRE(out.holds<lain::image::Image>());
	const lain::image::Image& img = out.get<lain::image::Image>();
	REQUIRE(img.width() == kSize);
	REQUIRE(img.height() == kSize);

	// The gradient's bottom-right R is the maximum (255); a Gaussian blur pulls it toward
	// its lower-valued neighbours, so it softens but stays bright. Alpha (opaque) survives
	// the premultiply round-trip. This exercises convert(space)/convert(alpha)/convolve.
	const Rgba br = pixel(img, static_cast<std::size_t>(kSize) * kSize - 1);
	CHECK(br.r < 255);
	CHECK(br.r > 245);
	CHECK(br.a == 255);
}

TEST_CASE("ImageDifferenceNode measures how far two images are apart", "[flow]")
{
	// The measurement half of a while loop's condition (M11): "has this stopped changing?" is
	// something the GRAPH asks, so the answer has to be a value on a pin.
	constexpr int kSize = 16;

	using namespace lain::flow;
	Graph graph;
	const NodeId a = graph.add<example::GradientNode>(kSize, kSize);
	const NodeId b = graph.add<example::GradientNode>(kSize, kSize);
	const NodeId diff = graph.add<example::ImageDifferenceNode>();
	REQUIRE(graph.connect(a, 0, diff, 0) == Connection::Ok);
	REQUIRE(graph.connect(b, 0, diff, 1) == Connection::Ok);

	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, diff);

	const PortAddress out{diff, graph.node(diff).output(0).id()};
	REQUIRE(e.value(out).holds<float>());
	// Two identical gradients: exactly zero, which is what makes "converged" expressible at all.
	CHECK(e.value(out).get<float>() == 0.0f);

	SECTION("and a blurred copy differs from its source")
	{
		const NodeId blur = graph.add<example::BlurNode>(2, 1.5f);
		REQUIRE(graph.connect(a, 0, blur, 0) == Connection::Ok);
		REQUIRE(graph.disconnect(diff, 1)); // an input takes one source; free it before rewiring
		REQUIRE(graph.connect(blur, 0, diff, 1) == Connection::Ok);
		e.requestRecomputeAll();
		SerialScheduler{}.evaluate(graph, e, diff);

		REQUIRE(e.value(out).holds<float>());
		const float d = e.value(out).get<float>();
		CHECK(d > 0.0f);
		CHECK(d < 1.0f); // a blur moves the pixels; it does not invert them
	}

	SECTION("images of differing size are refused, not reconciled")
	{
		// CombineNode's call: a difference across differing extents would silently mean something
		// the caller did not ask for. No value, so ADR-0007 suppresses — inside a loop, an
		// iteration that FAILED.
		const NodeId small = graph.add<example::GradientNode>(kSize / 2, kSize / 2);
		REQUIRE(graph.disconnect(diff, 1));
		REQUIRE(graph.connect(small, 0, diff, 1) == Connection::Ok);
		e.requestRecomputeAll();
		SerialScheduler{}.evaluate(graph, e, diff);
		CHECK(e.value(out).empty());
	}
}

TEST_CASE("CompareNode turns a measurement into the bool a condition needs", "[flow]")
{
	// The deciding half. `b` is a DEFAULTED input, so the threshold is typed on the node in the
	// common case and driven by the graph when something is wired to it.
	using namespace lain::flow;
	Graph graph;
	const NodeId value = graph.add<ConstantNode<float>>(0.5f);
	const NodeId compare = graph.add<example::CompareNode>(example::Comparison::Greater, 0.25f);
	REQUIRE(graph.connect(value, 0, compare, 0) == Connection::Ok);

	Node& node = graph.node(compare);
	const PortAddress out{compare, node.output(0).id()};
	// By NAME, not by position: `b`'s Default declares a param of its own first, so the operator is
	// not param 0 — which is exactly the trap a positional handle sets (M6 step 2).
	const PortId op = [&]
	{
		for (std::size_t i = 0; i < node.paramCount(); ++i)
		{
			if (node.param(i).name() == "op")
				return node.param(i).id();
		}
		return PortId{};
	}();
	REQUIRE(op != PortId{});

	const auto answer = [&](example::Comparison comparison)
	{
		REQUIRE(node.setParam<example::Comparison>(op, comparison));
		Evaluation e{graph};
		SerialScheduler{}.evaluate(graph, e, compare);
		REQUIRE(e.value(out).holds<bool>());
		return e.value(out).get<bool>();
	};

	CHECK(answer(example::Comparison::Greater));		 // 0.5 >  0.25
	CHECK(answer(example::Comparison::GreaterEqual));	 // 0.5 >= 0.25
	CHECK_FALSE(answer(example::Comparison::Less));		 // 0.5 <  0.25
	CHECK_FALSE(answer(example::Comparison::LessEqual)); // 0.5 <= 0.25

	SECTION("a wired threshold beats the typed one")
	{
		const NodeId threshold = graph.add<ConstantNode<float>>(0.75f);
		REQUIRE(graph.connect(threshold, 0, compare, 1) == Connection::Ok);
		CHECK_FALSE(answer(example::Comparison::Greater)); // 0.5 > 0.75 is false
	}
}

// A fake image reader for the LoadImageNode tests: decodes to a 1-row RGBA8 image whose
// width is the input byte count, so the node's success path can be checked without pulling
// a real codec into the flow tests. Registered under a private extension.
class FakeReader : public lain::io::image::ImageReader
{
public:
	lain::image::Image decode(const lain::memory::Buffer& bytes) const override
	{
		return lain::image::Image(static_cast<int>(bytes.size()), 1, lain::image::PixelFormat::RGBA8);
	}
};

// Writes bytes to a uniquely-named temp file (given extension) and removes it on destruction.
class TempFile
{
public:
	TempFile(const std::string& extension, const std::vector<std::uint8_t>& bytes)
	{
		static int counter = 0;
		m_path = std::filesystem::temp_directory_path() /
				 ("lain_loadnode_" + std::to_string(counter++) + "." + extension);
		std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	~TempFile()
	{
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
	}
	TempFile(const TempFile&) = delete;
	TempFile& operator=(const TempFile&) = delete;
	std::string path() const { return m_path.string(); }

private:
	std::filesystem::path m_path;
};

TEST_CASE("LoadImageNode loads a file and emits the decoded image", "[flow]")
{
	using namespace lain::flow;
	lain::io::image::readerRegistry().registerType<FakeReader>("fake");
	const TempFile file("fake", {1, 2, 3, 4, 5}); // 5 bytes -> FakeReader makes a 5x1 image

	Graph graph;
	const NodeId id = graph.add<example::LoadImageNode>(file.path());
	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, id);

	const lain::flow::PortValue& out = e.value(PortAddress{id, graph.node(id).output(0).id()});
	REQUIRE(out.holds<lain::image::Image>());
	const lain::image::Image& img = out.get<lain::image::Image>();
	REQUIRE(img.valid());
	REQUIRE(img.width() == 5);
}

TEST_CASE("LoadImageNode emits an invalid image when the file can't be read", "[flow]")
{
	using namespace lain::flow;
	Graph graph;
	const NodeId id = graph.add<example::LoadImageNode>("/no/such/file.fake");
	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, id);

	const PortValue& out = e.value(PortAddress{id, graph.node(id).output(0).id()});
	REQUIRE(out.holds<lain::image::Image>());
	REQUIRE_FALSE(out.get<lain::image::Image>().valid());
}

TEST_CASE("LoadImageNode's path is an editable filesystem::path param", "[flow]")
{
	using namespace lain::flow;
	lain::io::image::readerRegistry().registerType<FakeReader>("fake");
	const TempFile file("fake", {1, 2, 3});

	Graph graph;
	const NodeId id = graph.add<example::LoadImageNode>(); // empty default path
	Evaluation e{graph};
	SerialScheduler{}.evaluate(graph, e, id);
	REQUIRE_FALSE(e.value(PortAddress{id, graph.node(id).output(0).id()}).get<lain::image::Image>().valid());

	// The adapter edits the path param (its only param); setParam invalidates the node as part of
	// the write, so the pull re-runs it — a clean node is skipped by evaluate (the edit contract).
	lain::flow::Node& loader = graph.node(id);
	REQUIRE(loader.setParam(loader.param(0).id(), std::filesystem::path(file.path())));
	SerialScheduler{}.evaluate(graph, e, id); // the same evaluation notices the version bump
	const lain::image::Image& img = e.value(PortAddress{id, graph.node(id).output(0).id()}).get<lain::image::Image>();
	REQUIRE(img.valid());
	REQUIRE(img.width() == 3); // FakeReader makes a (byte-count)x1 image
}
