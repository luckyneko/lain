// Unit: the example nodes produce/consume a lain::image::Image through the graph's pull
// path, and an edge carries an image between them. Pure CPU — no device, no [gpu] SKIP.

#include "lain/flow/graph.h"
#include "lain/flow/scheduler.h"

#include <lain/flow/example/blurnode.h>
#include <lain/flow/example/gradientnode.h>
#include <lain/flow/example/tintnode.h>
#include <lain/image/image.h>

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

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
