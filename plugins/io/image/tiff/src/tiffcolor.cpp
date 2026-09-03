#include "tiffcolor.h"

#include <lain/image/colormath.h> // toLinear — the curve the table samples

#include <cmath>
#include <cstring>
#include <vector>

namespace lain::io::image::tiff
{
	std::vector<std::uint16_t> transferTable(lain::image::ColorSpace space, std::uint16_t bits)
	{
		const std::size_t entries = static_cast<std::size_t>(1) << bits;
		std::vector<std::uint16_t> table(entries);
		const float last = static_cast<float>(entries - 1);
		for (std::size_t i = 0; i < entries; ++i)
		{
			const float linear = lain::image::toLinear(space, static_cast<float>(i) / last);
			table[i] = static_cast<std::uint16_t>(std::lround(linear * 65535.0f));
		}
		return table;
	}

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// The spaces a TransferFunction can name, in the order they are tried. Unspecified is absent
	// because it is the answer when nothing matches, not a curve to compare against.
	static constexpr lain::image::ColorSpace kNamedSpaces[] = {
		lain::image::ColorSpace::sRGB,
		lain::image::ColorSpace::Linear,
		lain::image::ColorSpace::BT709,
	};

	lain::image::ColorSpace colorSpaceFromTiff(TIFF* tif, std::uint16_t bits, std::uint16_t colorChannels)
	{
		// An embedded profile outranks the curve: it is the more specific statement, and it is one
		// lain cannot represent, so the honest answer is "you decide" rather than a curve that may
		// only describe part of it.
		std::uint32_t iccLength = 0;
		void* iccProfile = nullptr;
		if (TIFFGetField(tif, TIFFTAG_ICCPROFILE, &iccLength, &iccProfile) != 0 && iccLength > 0)
			return lain::image::ColorSpace::Unspecified;

		if (bits != 8 && bits != 16)
			return lain::image::ColorSpace::Unspecified; // the reader accepts no other depth anyway

		// THREE POINTERS, ALWAYS — libtiff's getter and setter are ASYMMETRIC here, and getting it
		// wrong corrupts the stack rather than failing. TIFFSetField consumes one array for a
		// single-colour-channel image and three otherwise; TIFFGetField always consumes three
		// uint16_t**, writing NULL into the second and third when there is only one channel
		// (tif_dir.c, the else arm). Passing one pointer to the getter therefore lets libtiff write
		// two NULLs past the end of the argument list — which a debug build absorbed and a release
		// build turned into a SIGSEGV on the first grayscale image.
		const std::uint16_t* red = nullptr;
		const std::uint16_t* green = nullptr;
		const std::uint16_t* blue = nullptr;
		if (TIFFGetField(tif, TIFFTAG_TRANSFERFUNCTION, &red, &green, &blue) == 0 || red == nullptr)
			return lain::image::ColorSpace::Unspecified; // no curve stated

		const std::size_t bytes = (static_cast<std::size_t>(1) << bits) * sizeof(std::uint16_t);
		for (const lain::image::ColorSpace space : kNamedSpaces)
		{
			const std::vector<std::uint16_t> expected = transferTable(space, bits);
			if (std::memcmp(red, expected.data(), bytes) != 0)
				continue;
			// A colour image states one curve per channel; lain only names a space when all three
			// agree, since a per-channel curve is not a ColorSpace at all.
			if (colorChannels > 1 && (green == nullptr || blue == nullptr ||
									  std::memcmp(green, expected.data(), bytes) != 0 ||
									  std::memcmp(blue, expected.data(), bytes) != 0))
				continue;
			return space;
		}
		return lain::image::ColorSpace::Unspecified; // a curve, but not one lain has a name for
	}

	bool tiffCanStateSpace(lain::image::ColorSpace space)
	{
		// Exhaustive with no default arm, so a new ColorSpace fails the build here rather than
		// silently inheriting whichever answer happened to sit last (-Werror,-Wswitch). A new
		// standard only needs a curve in image::toLinear for this arm to keep being true.
		switch (space)
		{
			case lain::image::ColorSpace::Unspecified: // no tag; "no claim" round-trips exactly
			case lain::image::ColorSpace::sRGB:
			case lain::image::ColorSpace::Linear:
			case lain::image::ColorSpace::BT709:
				return true;
		}
		return false;
	}

	void applyColorSpaceToTiff(TIFF* tif, lain::image::ColorSpace space, std::uint16_t bits,
							   std::uint16_t colorChannels)
	{
		if (space == lain::image::ColorSpace::Unspecified)
			return; // no tag — the honest absence of a claim

		const std::vector<std::uint16_t> table = transferTable(space, bits);
		if (colorChannels > 1)
			TIFFSetField(tif, TIFFTAG_TRANSFERFUNCTION, table.data(), table.data(), table.data());
		else
			TIFFSetField(tif, TIFFTAG_TRANSFERFUNCTION, table.data());
	}
} // namespace lain::io::image::tiff
