// Unit: the example nodes produce/consume a lain::image::Image through the graph's pull
// path, and an edge carries an image between them. Pure CPU — no device, no [gpu] SKIP.

#include "lain/flow/graph.h"
#include "lain/flow/scheduler.h"

#include <lain/flow/example/blurnode.h>
#include <lain/flow/example/gradientnode.h>
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

	SerialScheduler{}.evaluate(graph, id); // pull: runs compute() — fills the image

	const Port& out = graph.node(id).output(0);
	REQUIRE(out.value().holds<lain::image::Image>());
	const lain::image::Image& img = out.value().get<lain::image::Image>();
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

	SerialScheduler{}.evaluate(graph, tint); // pulls gradient upstream, then tint

	const Port& out = graph.node(tint).output(0);
	REQUIRE(out.value().holds<lain::image::Image>());
	const lain::image::Image& img = out.value().get<lain::image::Image>();

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

	SerialScheduler{}.evaluate(graph, tint);
	CHECK(pixel(graph.node(tint).output(0).value().get<lain::image::Image>(), 0).b == 128); // gradient blue, unchanged

	// Edit the single "tint" ColorRGBf param (halve blue), then dirty + re-eval.
	graph.node(tint).param(0).set<lain::image::ColorRGBf>(lain::image::ColorRGBf(1.0f, 1.0f, 0.5f));
	graph.node(tint).markDirty();
	SerialScheduler{}.evaluate(graph, tint);
	CHECK(pixel(graph.node(tint).output(0).value().get<lain::image::Image>(), 0).b == 64);
}

TEST_CASE("transform nodes handle a non-RGBA8 input (e.g. a loaded RGB8 image)", "[flow]")
{
	using namespace lain::flow;
	// An RGB8 image — 3 bytes/pixel, like a decoded JPEG. A node that assumes RGBA8 would
	// index past the buffer and crash; both must normalise first.
	const lain::image::Image rgb(4, 4, lain::image::PixelFormat::RGB8, lain::image::ColorSpace::sRGB);

	example::TintNode tint(1.0f, 0.5f, 0.5f);
	tint.input(0).set<lain::image::Image>(rgb);
	tint.compute(); // must not overrun the RGB8 buffer
	REQUIRE(tint.output(0).value().get<lain::image::Image>().valid());
	REQUIRE(tint.output(0).value().get<lain::image::Image>().pixelFormat() == lain::image::PixelFormat::RGBA8);

	example::BlurNode blur(1, 1.0f);
	blur.input(0).set<lain::image::Image>(rgb);
	blur.compute();
	REQUIRE(blur.output(0).value().get<lain::image::Image>().valid());
}

TEST_CASE("BlurNode runs the op catalog through an edge: gradient -> blur", "[flow]")
{
	constexpr int kSize = 64;

	using namespace lain::flow;
	Graph graph;
	const NodeId gradient = graph.add<example::GradientNode>(kSize, kSize);
	const NodeId blur = graph.add<example::BlurNode>(2, 1.5f);
	REQUIRE(graph.connect(gradient, 0, blur, 0) == Connection::Ok);

	SerialScheduler{}.evaluate(graph, blur); // pulls gradient upstream, then blurs

	const Port& out = graph.node(blur).output(0);
	REQUIRE(out.value().holds<lain::image::Image>());
	const lain::image::Image& img = out.value().get<lain::image::Image>();
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
	SerialScheduler{}.evaluate(graph, id);

	const Port& out = graph.node(id).output(0);
	REQUIRE(out.value().holds<lain::image::Image>());
	const lain::image::Image& img = out.value().get<lain::image::Image>();
	REQUIRE(img.valid());
	REQUIRE(img.width() == 5);
}

TEST_CASE("LoadImageNode emits an invalid image when the file can't be read", "[flow]")
{
	using namespace lain::flow;
	Graph graph;
	const NodeId id = graph.add<example::LoadImageNode>("/no/such/file.fake");
	SerialScheduler{}.evaluate(graph, id);

	const Port& out = graph.node(id).output(0);
	REQUIRE(out.value().holds<lain::image::Image>());
	REQUIRE_FALSE(out.value().get<lain::image::Image>().valid());
}

TEST_CASE("LoadImageNode's path is an editable filesystem::path param", "[flow]")
{
	using namespace lain::flow;
	lain::io::image::readerRegistry().registerType<FakeReader>("fake");
	const TempFile file("fake", {1, 2, 3});

	Graph graph;
	const NodeId id = graph.add<example::LoadImageNode>(); // empty default path
	SerialScheduler{}.evaluate(graph, id);
	REQUIRE_FALSE(graph.node(id).output(0).value().get<lain::image::Image>().valid());

	// The adapter edits the path param (its only param) then marks the node dirty so the
	// pull re-runs it — a clean node is skipped by evaluate (this is the edit contract).
	graph.node(id).param(0).set<std::filesystem::path>(std::filesystem::path(file.path()));
	graph.node(id).markDirty();
	SerialScheduler{}.evaluate(graph, id);
	const lain::image::Image& img = graph.node(id).output(0).value().get<lain::image::Image>();
	REQUIRE(img.valid());
	REQUIRE(img.width() == 3); // FakeReader makes a (byte-count)x1 image
}
