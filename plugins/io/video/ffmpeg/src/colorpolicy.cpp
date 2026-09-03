#include "colorpolicy.h"

extern "C"
{
#include <libavutil/pixdesc.h> // av_color_*_name, for naming a refusal
}

namespace lain::io::video::ffmpeg
{
	// --- what each axis says, one question each ---------------------------------
	//
	// THESE ARE ALLOWLISTS, AND THE INVERSION IS THE POINT. They began as denylists naming the
	// standards ADR-0018 refuses, which meant every value nobody had thought about fell through to
	// "BT709" — and the set that fell through was not small or harmless: the LOG and LOG_SQRT
	// transfers (V-Log/S-Log footage, linearised with a 709 curve is not a subtle error),
	// AVCOL_TRC_LINEAR (which the write seam already refuses for exactly this reason), DCI-P3 and
	// Display P3 primaries, CIE XYZ, and every extension FFmpeg adds after this was written.
	//
	// A denylist defaults to CLAIMING and an allowlist defaults to REFUSING, and only the second
	// matches what ADR-0018 says the policy does: report and refuse, never relabel. It is also the
	// shape io::video::canEncode already has on the write side — "anything that is not BT709 or
	// sRGB is refused" — so the two directions now fail the same way.
	//
	// The `default:` arm is correct here, unlike the exhaustive ColorSpace switches elsewhere in
	// lain: there the default would silently accept a new enumerator, here it refuses one.

	static bool acceptedTransfer(AVColorTransferCharacteristic transfer)
	{
		switch (transfer)
		{
			case AVCOL_TRC_BT709:		 // the transfer video is encoded against
			case AVCOL_TRC_UNSPECIFIED:	 // treated as BT709, with a log line (ADR-0018)
			case AVCOL_TRC_IEC61966_2_1: // sRGB — believed, because lain has that space exactly
				return true;
			default:
				return false;
		}
	}

	static bool acceptedPrimaries(AVColorPrimaries primaries)
	{
		switch (primaries)
		{
			case AVCOL_PRI_BT709: // which sRGB shares, so both accepted spaces have these
			case AVCOL_PRI_UNSPECIFIED:
				return true;
			default:
				return false;
		}
	}

	static bool acceptedMatrix(AVColorSpace matrix)
	{
		switch (matrix)
		{
			case AVCOL_SPC_RGB: // no matrix was applied at all — what lain writes for RGB streams
			case AVCOL_SPC_BT709:
			case AVCOL_SPC_UNSPECIFIED:
				return true;
			default:
				return false;
		}
	}

	// --- the policy -------------------------------------------------------------

	std::optional<lain::image::ColorSpace> colorSpaceFor(AVColorTransferCharacteristic transfer,
														 AVColorPrimaries primaries, AVColorSpace matrix)
	{
		if (!acceptedTransfer(transfer) || !acceptedPrimaries(primaries) || !acceptedMatrix(matrix))
			return std::nullopt;

		// Believed because it is explicit, and because lain can represent it exactly. Note this is
		// checked AFTER the refusals: a file claiming an sRGB curve over BT.2020 primaries is still
		// refused, since the curve is only half of what makes a colour space.
		if (transfer == AVCOL_TRC_IEC61966_2_1)
			return lain::image::ColorSpace::sRGB;

		// BT709, and everything unspecified. The log line for the second case belongs to the caller,
		// which knows which file it is talking about.
		return lain::image::ColorSpace::BT709;
	}

	AVColorSpace decodeMatrixFor(AVColorSpace matrix)
	{
		// Untagged means BT.601 HERE, while an untagged TRANSFER means BT709 one function up, and
		// the two answers differ on purpose rather than by oversight.
		//
		// The transfer decision is safe either way: the BT.601 and BT.709 OETFs are the same curve
		// to within rounding, so ADR-0018's guess costs nothing whichever standard the footage
		// really is. The MATRIX genuinely differs — measured on a saturated green, the two sets are
		// 17% apart — and an encoder that writes no tag is overwhelmingly one that used BT.601,
		// because that is what swscale and the encoders built on it default to. Verified rather
		// than assumed: lain's own untagged fixture decodes back to its source colour exactly under
		// BT.601 and is 10 counts out under BT.709, at 720p as well as at 64x48.
		//
		// So this returns what was already happening — swscale's own default — but as a DECISION
		// that is stated, tested and logged, rather than a library default inherited by passing the
		// raw tag straight through. That is the actual defect it fixes: nobody had chosen this.
		//
		// The AVColorSpace enumerators are numerically the SWS_CS_* macros (FFmpeg aligns them
		// deliberately), which is what lets a caller hand this straight to sws_getCoefficients.
		if (matrix == AVCOL_SPC_UNSPECIFIED)
			return AVCOL_SPC_BT470BG; // = SWS_CS_ITU601
		return matrix;
	}

	std::string refusedTagName(AVColorTransferCharacteristic transfer, AVColorPrimaries primaries,
							   AVColorSpace matrix)
	{
		// The transfer first, because when several axes disagree with lain the curve is the one
		// that decides what the pixels mean.
		if (!acceptedTransfer(transfer))
		{
			const char* name = av_color_transfer_name(transfer);
			return std::string{"transfer "} + (name != nullptr ? name : "?");
		}
		if (!acceptedPrimaries(primaries))
		{
			const char* name = av_color_primaries_name(primaries);
			return std::string{"primaries "} + (name != nullptr ? name : "?");
		}
		if (!acceptedMatrix(matrix))
		{
			const char* name = av_color_space_name(matrix);
			return std::string{"matrix "} + (name != nullptr ? name : "?");
		}
		return {};
	}

	std::optional<ColorTags> colorTagsFor(lain::image::ColorSpace space, bool rgbPixelFormat)
	{
		// The matrix an RGB-coded stream carries is RGB, not BT709: no matrix was applied, and
		// claiming one states a conversion that never happened. colorSpaceFor accepts AVCOL_SPC_RGB
		// (it is not among the refused matrices), so the round trip closes either way.
		const AVColorSpace matrix = rgbPixelFormat ? AVCOL_SPC_RGB : AVCOL_SPC_BT709;

		switch (space)
		{
			case lain::image::ColorSpace::BT709:
				return ColorTags{AVCOL_TRC_BT709, AVCOL_PRI_BT709, matrix};

			case lain::image::ColorSpace::sRGB:
				// The one tag colorSpaceFor believes over BT709, so writing anything else here
				// would make a file lain wrote read back as a different space than it holds.
				return ColorTags{AVCOL_TRC_IEC61966_2_1, AVCOL_PRI_BT709, matrix};

			case lain::image::ColorSpace::Linear:
			case lain::image::ColorSpace::Unspecified:
				// Never reached: io::video::canEncode refuses both at the seam, where the refusal
				// belongs, because neither is a fact about THIS backend. Answered exhaustively all
				// the same, so adding a ColorSpace fails the build here rather than silently
				// writing whatever the last arm returned.
				return std::nullopt;
		}
		return std::nullopt;
	}
} // namespace lain::io::video::ffmpeg
