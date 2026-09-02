#include "colorpolicy.h"

extern "C"
{
#include <libavutil/pixdesc.h> // av_color_*_name, for naming a refusal
}

namespace lain::io::video::ffmpeg
{
	// --- what each axis says, one question each ---------------------------------

	// A transfer lain refuses outright: an HDR curve, or an SD/UHD standard whose primaries differ.
	static bool refusedTransfer(AVColorTransferCharacteristic transfer)
	{
		switch (transfer)
		{
			case AVCOL_TRC_SMPTE170M:	 // BT.601 (525/625 line)
			case AVCOL_TRC_GAMMA22:		 // BT.470 System M
			case AVCOL_TRC_GAMMA28:		 // BT.470 System B/G
			case AVCOL_TRC_BT2020_10:	 // BT.2020, 10-bit
			case AVCOL_TRC_BT2020_12:	 // BT.2020, 12-bit
			case AVCOL_TRC_SMPTE2084:	 // PQ
			case AVCOL_TRC_ARIB_STD_B67: // HLG
				return true;
			default:
				return false;
		}
	}

	static bool refusedPrimaries(AVColorPrimaries primaries)
	{
		switch (primaries)
		{
			case AVCOL_PRI_BT470BG:	  // BT.601 625
			case AVCOL_PRI_SMPTE170M: // BT.601 525
			case AVCOL_PRI_BT2020:
				return true;
			default:
				return false;
		}
	}

	static bool refusedMatrix(AVColorSpace matrix)
	{
		switch (matrix)
		{
			case AVCOL_SPC_BT470BG:
			case AVCOL_SPC_SMPTE170M:
			case AVCOL_SPC_BT2020_NCL:
			case AVCOL_SPC_BT2020_CL:
				return true;
			default:
				return false;
		}
	}

	// --- the policy -------------------------------------------------------------

	std::optional<lain::image::ColorSpace> colorSpaceFor(AVColorTransferCharacteristic transfer,
														 AVColorPrimaries primaries, AVColorSpace matrix)
	{
		if (refusedTransfer(transfer) || refusedPrimaries(primaries) || refusedMatrix(matrix))
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

	std::string refusedTagName(AVColorTransferCharacteristic transfer, AVColorPrimaries primaries,
							   AVColorSpace matrix)
	{
		// The transfer first, because when several axes disagree with lain the curve is the one
		// that decides what the pixels mean.
		if (refusedTransfer(transfer))
		{
			const char* name = av_color_transfer_name(transfer);
			return std::string{"transfer "} + (name != nullptr ? name : "?");
		}
		if (refusedPrimaries(primaries))
		{
			const char* name = av_color_primaries_name(primaries);
			return std::string{"primaries "} + (name != nullptr ? name : "?");
		}
		if (refusedMatrix(matrix))
		{
			const char* name = av_color_space_name(matrix);
			return std::string{"matrix "} + (name != nullptr ? name : "?");
		}
		return {};
	}
} // namespace lain::io::video::ffmpeg
