// The colour policy as a pure function (M10 slice 5b).
//
// It is tested directly rather than only through files because encoding a fixture for every arm
// would need a container that carries each tag — and the tags a real encoder will actually write
// turn out to be a subset (the PQ fixture kept its matrix and needed a filter to keep its
// transfer). The policy is where ADR-0018's rule lives, so it is asked all of its questions here,
// and the fixture proves the wiring.

#include "colorpolicy.h"

#include <catch2/catch_test_macros.hpp>

using lain::image::ColorSpace;
using lain::io::video::ffmpeg::colorSpaceFor;
using lain::io::video::ffmpeg::refusedTagName;

TEST_CASE("BT709 and untagged footage both decode as BT709", "[video][color]")
{
	CHECK(colorSpaceFor(AVCOL_TRC_BT709, AVCOL_PRI_BT709, AVCOL_SPC_BT709) == ColorSpace::BT709);

	// The case that matters most in practice: real footage very often carries no colour tags at
	// all. FFmpeg's own convention would guess BT.601 by frame size, which under the refusal rule
	// below would reject perfectly ordinary material for a tag it never had (ADR-0018).
	CHECK(colorSpaceFor(AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_UNSPECIFIED, AVCOL_SPC_UNSPECIFIED) == ColorSpace::BT709);

	// Partly tagged is still not a refusal: nothing here disagrees with BT709.
	CHECK(colorSpaceFor(AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_BT709, AVCOL_SPC_UNSPECIFIED) == ColorSpace::BT709);
}

TEST_CASE("an explicit sRGB transfer is believed", "[video][color]")
{
	// lain HAS that space and convert() composes through Linear, so honouring the tag costs
	// nothing — and it is the same rule the refusals follow: believe what the file says.
	CHECK(colorSpaceFor(AVCOL_TRC_IEC61966_2_1, AVCOL_PRI_BT709, AVCOL_SPC_BT709) == ColorSpace::sRGB);
}

TEST_CASE("explicitly tagged BT.601, BT.2020, PQ and HLG are refused", "[video][color]")
{
	// Refused rather than relabelled: BT.601 and BT.709 are byte-identical and visually close, so
	// a mislabel is silent — and silently wrong in a calibration report.
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_SMPTE170M, AVCOL_PRI_SMPTE170M, AVCOL_SPC_SMPTE170M).has_value());
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_BT2020_10, AVCOL_PRI_BT2020, AVCOL_SPC_BT2020_NCL).has_value());
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_SMPTE2084, AVCOL_PRI_BT2020, AVCOL_SPC_BT2020_NCL).has_value());
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_ARIB_STD_B67, AVCOL_PRI_BT2020, AVCOL_SPC_BT2020_NCL).has_value());
}

TEST_CASE("every axis is examined, not just the transfer", "[video][color]")
{
	// How the PQ fixture actually came out of the encoder: the matrix survived into the container
	// and the transfer did not. A policy that read only the transfer would have accepted BT.2020
	// material as BT709 — which is why all three are asked, and why this case exists.
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_UNSPECIFIED, AVCOL_SPC_BT2020_NCL).has_value());
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_BT2020, AVCOL_SPC_UNSPECIFIED).has_value());

	// And a believable curve over impossible primaries is still refused: a curve is only half of
	// what makes a colour space.
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_IEC61966_2_1, AVCOL_PRI_BT2020, AVCOL_SPC_BT709).has_value());
}

TEST_CASE("a refusal names the tag that caused it", "[video][color]")
{
	// A refusal that said only "unsupported colour" would leave a user guessing which of three
	// axes to look at, on a file they cannot see inside.
	CHECK(refusedTagName(AVCOL_TRC_SMPTE2084, AVCOL_PRI_BT709, AVCOL_SPC_BT709).find("transfer") == 0);
	CHECK(refusedTagName(AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_BT2020, AVCOL_SPC_BT709).find("primaries") == 0);
	CHECK(refusedTagName(AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_UNSPECIFIED, AVCOL_SPC_BT2020_NCL).find("matrix") == 0);

	// Nothing to name when nothing is refused, so the caller cannot print an empty accusation.
	CHECK(refusedTagName(AVCOL_TRC_BT709, AVCOL_PRI_BT709, AVCOL_SPC_BT709).empty());
}

TEST_CASE("what lain writes, lain reads back as the same space", "[color][video]")
{
	// THE property that keeps the two directions from drifting. If colorTagsFor and colorSpaceFor
	// ever disagree, a file lain wrote comes back tagged as a space it does not hold — the silent
	// wrong answer ADR-0018 refuses on the way in, arriving on the way out. Neither arm can be
	// edited alone without this failing, which is the only form in which the property survives.
	for (const lain::image::ColorSpace space : {lain::image::ColorSpace::BT709, lain::image::ColorSpace::sRGB})
	{
		for (const bool rgb : {false, true})
		{
			const auto tags = lain::io::video::ffmpeg::colorTagsFor(space, rgb);
			REQUIRE(tags.has_value());

			// Nothing lain writes may be a tag lain refuses: it would be unable to read its own
			// output at all.
			CHECK(lain::io::video::ffmpeg::refusedTagName(tags->transfer, tags->primaries, tags->matrix).empty());

			const auto readBack = lain::io::video::ffmpeg::colorSpaceFor(tags->transfer, tags->primaries,
																		 tags->matrix);
			REQUIRE(readBack.has_value());
			CHECK(*readBack == space);
		}
	}
}

TEST_CASE("the spaces a container cannot state produce no tags", "[color][video]")
{
	// Linear is the sharp one. AVCOL_TRC_LINEAR exists and the file would encode fine — but
	// colorSpaceFor does not refuse it, so the result would read back as BT709 with every value
	// off by the ~2.2 gamma, invisibly. Refused at io::video::canEncode, where the refusal is about
	// the MEDIUM rather than this backend; answered here too so no arm can quietly write a guess.
	CHECK_FALSE(lain::io::video::ffmpeg::colorTagsFor(lain::image::ColorSpace::Linear, false).has_value());
	CHECK_FALSE(lain::io::video::ffmpeg::colorTagsFor(lain::image::ColorSpace::Unspecified, false).has_value());
}
