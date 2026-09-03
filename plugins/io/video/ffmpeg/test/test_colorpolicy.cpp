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
using lain::io::video::ffmpeg::decodeMatrixFor;
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
	// Linear is the sharp one. AVCOL_TRC_LINEAR exists and the file would encode fine — and it
	// would have read back as BT709 with every value off by the ~2.2 gamma, invisibly, until the
	// read side became an allowlist and started refusing it too (see the case below). It is still
	// refused at io::video::canEncode, where the refusal is about the MEDIUM rather than this
	// backend, and answered here as well so no arm can quietly write a guess.
	CHECK_FALSE(lain::io::video::ffmpeg::colorTagsFor(lain::image::ColorSpace::Linear, false).has_value());
	CHECK_FALSE(lain::io::video::ffmpeg::colorTagsFor(lain::image::ColorSpace::Unspecified, false).has_value());
}

TEST_CASE("a tag lain was not built to handle is refused, not claimed as BT709", "[video][color]")
{
	// The allowlist's whole purpose. Each of these fell through a denylist to "BT709" — they are
	// not exotic corners, they are the values nobody had got round to naming.

	// LOG footage read as BT709 is the loudest of them: a log curve exists precisely because it is
	// not a display curve, and linearising it with a 709 OETF is not a subtle error.
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_LOG, AVCOL_PRI_BT709, AVCOL_SPC_BT709).has_value());
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_LOG_SQRT, AVCOL_PRI_BT709, AVCOL_SPC_BT709).has_value());

	// Linear, which the WRITE seam already refused for exactly this reason while the read side let
	// it through — the two directions disagreeing about the same value.
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_LINEAR, AVCOL_PRI_BT709, AVCOL_SPC_BT709).has_value());

	// Wide-gamut primaries lain has no model for. Display P3 in particular is not a curiosity.
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_BT709, AVCOL_PRI_SMPTE432, AVCOL_SPC_BT709).has_value()); // Display P3
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_BT709, AVCOL_PRI_SMPTE431, AVCOL_SPC_BT709).has_value()); // DCI-P3
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_BT709, AVCOL_PRI_SMPTE428, AVCOL_SPC_BT709).has_value()); // CIE XYZ
	CHECK_FALSE(colorSpaceFor(AVCOL_TRC_BT709, AVCOL_PRI_BT470M, AVCOL_SPC_BT709).has_value());	  // NTSC 1953

	// And an enumerator this code has never heard of. A denylist accepts one by construction; an
	// allowlist refuses it, which is the direction a policy that "reports and refuses" needs.
	CHECK_FALSE(colorSpaceFor(static_cast<AVColorTransferCharacteristic>(200), AVCOL_PRI_BT709,
							  AVCOL_SPC_BT709)
					.has_value());

	// Each still names the axis, so the refusal is actionable rather than a blanket "unsupported".
	CHECK(refusedTagName(AVCOL_TRC_LOG, AVCOL_PRI_BT709, AVCOL_SPC_BT709).find("transfer") == 0);
	CHECK(refusedTagName(AVCOL_TRC_BT709, AVCOL_PRI_SMPTE432, AVCOL_SPC_BT709).find("primaries") == 0);
}

TEST_CASE("the decode matrix is a stated decision, and differs from the transfer's", "[video][color]")
{
	// Two axes, two answers, and the difference is the point. An unspecified TRANSFER is treated as
	// BT709 because the BT.601 and BT.709 OETFs are the same curve to within rounding, so the guess
	// costs nothing. An unspecified MATRIX is BT.601, because the coefficient sets really do differ
	// (17% on a saturated green) and an encoder that wrote no tag used BT.601 — measured on lain's
	// own untagged fixture, which decodes back to its source colour exactly one way and 10 counts
	// out the other.
	CHECK(decodeMatrixFor(AVCOL_SPC_UNSPECIFIED) == AVCOL_SPC_BT470BG); // = SWS_CS_ITU601
	CHECK(colorSpaceFor(AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_UNSPECIFIED, AVCOL_SPC_UNSPECIFIED) ==
		  ColorSpace::BT709);

	// A stated matrix is used as stated — the guess applies only where the file said nothing.
	CHECK(decodeMatrixFor(AVCOL_SPC_BT709) == AVCOL_SPC_BT709);
	CHECK(decodeMatrixFor(AVCOL_SPC_RGB) == AVCOL_SPC_RGB);
}
