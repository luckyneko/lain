#include "pngcolor.h"

namespace lain::io::image::png
{
	lain::image::ColorSpace colorSpaceFromPng(png_structp png, png_infop info)
	{
		int srgbIntent = 0;
		if (png_get_sRGB(png, info, &srgbIntent) != 0)
			return lain::image::ColorSpace::sRGB; // definitive; the rendering intent is not modelled

		png_charp iccName = nullptr;
		int iccCompression = 0;
		png_bytep iccProfile = nullptr;
		png_uint_32 iccLength = 0;
		if (png_get_iCCP(png, info, &iccName, &iccCompression, &iccProfile, &iccLength) != 0)
			return lain::image::ColorSpace::Unspecified; // a profile lain can't represent

		double fileGamma = 0.0;
		if (png_get_gAMA(png, info, &fileGamma) != 0)
		{
			if (fileGamma > 0.44 && fileGamma < 0.47) // ~1/2.2, sRGB-ish encoding
				return lain::image::ColorSpace::sRGB;
			if (fileGamma > 0.99 && fileGamma < 1.01) // linear light — and what lain writes for Linear
				return lain::image::ColorSpace::Linear;
			return lain::image::ColorSpace::Unspecified; // some other gamma lain can't track
		}
		return lain::image::ColorSpace::Unspecified; // untagged -> honestly unknown
	}

	bool pngCanStateSpace(lain::image::ColorSpace space)
	{
		// Exhaustive with no default arm, so a new ColorSpace fails the build here rather than
		// silently inheriting whichever answer happened to sit last (-Werror,-Wswitch).
		switch (space)
		{
			case lain::image::ColorSpace::Unspecified: // no chunk; "no claim" round-trips exactly
			case lain::image::ColorSpace::sRGB:		   // the sRGB chunk
			case lain::image::ColorSpace::Linear:	   // gAMA 1.0
				return true;

			case lain::image::ColorSpace::BT709:
				return false; // see the header: gAMA 0.45 reads back as sRGB
		}
		return false;
	}

	bool pngCanStateAlpha(lain::image::AlphaMode mode)
	{
		switch (mode)
		{
			case lain::image::AlphaMode::Unspecified: // PNG's alpha is straight by definition
			case lain::image::AlphaMode::Straight:
				return true;

			case lain::image::AlphaMode::Premultiplied:
				return false; // PNG has no associated alpha; the reader would call these Straight
		}
		return false;
	}

	void applyColorSpaceToPng(png_structp png, png_infop info, lain::image::ColorSpace space)
	{
		switch (space)
		{
			case lain::image::ColorSpace::sRGB:
				// The gAMA/cHRM companions matter for readers that predate or ignore the sRGB
				// chunk; lain's own reader takes the sRGB chunk first regardless.
				png_set_sRGB_gAMA_and_cHRM(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
				return;

			case lain::image::ColorSpace::Linear:
				png_set_gAMA(png, info, 1.0);
				return;

			case lain::image::ColorSpace::Unspecified:
				return; // no chunk — the honest absence of a claim

			case lain::image::ColorSpace::BT709:
				return; // unreachable: pngCanStateSpace refuses it at canEncode
		}
	}
} // namespace lain::io::image::png
