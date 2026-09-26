#include <lain/image/transfercurve.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// The ColorSpace transfer: that every space answers a COMPLETE pair, that each pair inverts, and
// that the two standards really are different curves. Split out of test_colormath.cpp when the
// transfer moved to its own header, so the tests follow the sources.
using namespace lain::image;
using Catch::Approx;

TEST_CASE("every ColorSpace answers a complete transfer curve", "[transfercurve]")
{
	// transferCurve is TOTAL, which is what lets every caller use both halves without a null check.
	// A census over the whole enum rather than the two curved spaces, because the arm that is easy
	// to get wrong is the one that falls through.
	//
	// TransferCurve now default-constructs to the identity, so an unfilled pair is no longer
	// reachable and this can only fail if an arm returns something explicitly broken. That is a
	// weaker case than it was, and deliberately so: the hazard moved into the type.
	for (const ColorSpace space : {ColorSpace::Unspecified, ColorSpace::Linear, ColorSpace::sRGB, ColorSpace::BT709})
	{
		const TransferCurve curve = transferCurve(space);
		REQUIRE(curve.toLinear != nullptr);
		REQUIRE(curve.fromLinear != nullptr);
		REQUIRE(curve.toLinear(0.5f) == Approx(curve.toLinear(0.5f))); // callable, not just non-null
	}
}

TEST_CASE("a space's decode and encode are inverses of each other", "[transfercurve]")
{
	// The whole compose-through-Linear scheme rests on this: if a space's pair did not invert,
	// converting A -> B -> A would drift instead of returning the value it started with. It is
	// also the guard against the one mistake the paired type still permits — an arm wired
	// backwards, since a double-decode is not an inverse.
	for (const ColorSpace space : {ColorSpace::sRGB, ColorSpace::BT709})
	{
		const TransferCurve curve = transferCurve(space);
		for (float v = 0.0f; v <= 1.0f; v += 0.05f)
		{
			REQUIRE(curve.fromLinear(curve.toLinear(v)) == Approx(v).margin(1e-5));
			REQUIRE(curve.toLinear(curve.fromLinear(v)) == Approx(v).margin(1e-5));
		}
	}
}

TEST_CASE("a decode followed by its own encode is not EXACT", "[transfercurve]")
{
	// Why image::convert(img, ColorSpace) must short-circuit srcSpace == dstSpace rather than
	// letting the curves cancel: the two segments of a spec's rounded curve do not meet exactly,
	// so a round trip lands close but not back. Swept rather than asserted at one value, because
	// WHICH values survive is a property of the platform's pow — 0.77 happens to survive here
	// while ~31% of sRGB values and ~41% of BT709 ones do not. The claim is "not everywhere",
	// which is robust; "not at this value" was a guess dressed as a test.
	for (const ColorSpace space : {ColorSpace::sRGB, ColorSpace::BT709})
	{
		const TransferCurve curve = transferCurve(space);
		int inexact = 0;
		for (int i = 0; i <= 1000; ++i)
		{
			const float v = static_cast<float>(i) / 1000.0f;
			if (curve.fromLinear(curve.toLinear(v)) != v)
				++inexact;
		}
		REQUIRE(inexact > 0);
	}
}

TEST_CASE("Linear and Unspecified carry values through unchanged", "[transfercurve]")
{
	// Linear MUST be the identity — it is the intermediate every other pairing composes
	// through. Unspecified is the documented total-function fallback, not a meaning.
	for (const ColorSpace space : {ColorSpace::Linear, ColorSpace::Unspecified})
	{
		const TransferCurve curve = transferCurve(space);
		REQUIRE(curve.toLinear(0.25f) == 0.25f);
		REQUIRE(curve.fromLinear(0.25f) == 0.25f);
	}
}

TEST_CASE("the BT709 curve is the Rec.709 OETF, not sRGB's", "[transfercurve]")
{
	const TransferCurve bt709 = transferCurve(ColorSpace::BT709);
	const TransferCurve srgb = transferCurve(ColorSpace::sRGB);

	// The linear toe below the breakpoint is an exact divide by 4.5.
	REQUIRE(bt709.toLinear(0.045f) == Approx(0.01f));
	REQUIRE(bt709.fromLinear(0.01f) == Approx(0.045f));

	// Mid-grey: the two spaces differ by ~20% of the linear value. That gap is the whole
	// reason BT709 is its own enumerator rather than footage being tagged sRGB. It is also what
	// catches a space's pair being wired to the wrong standard's curves.
	REQUIRE(bt709.toLinear(0.5f) == Approx(0.2596f).margin(0.001));
	REQUIRE(srgb.toLinear(0.5f) == Approx(0.2140f).margin(0.001));
}

TEST_CASE("the BT709 breakpoint steps by the spec's rounding", "[transfercurve]")
{
	// Rec.709's published constants leave the two segments meeting at ~2e-4 rather than
	// exactly. Pinned so the discontinuity is a recorded property, not a surprise, and so
	// "fixing" it to the continuous alpha/beta values shows up as a deliberate change.
	const TransferCurve curve = transferCurve(ColorSpace::BT709);
	const float below = curve.fromLinear(0.018f - 1e-6f);
	const float above = curve.fromLinear(0.018f + 1e-6f);
	REQUIRE(below == Approx(0.081f).margin(1e-4));
	REQUIRE(above == Approx(0.081f).margin(1e-3));
	REQUIRE(above - below < 1e-3f);
}
