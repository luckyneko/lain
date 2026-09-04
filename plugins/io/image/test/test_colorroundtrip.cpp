// The colour round-trip matrix, over every codec the build enabled — ADR-0020's claim as one
// table, driven through the production facade (registerImageCodecs then encode/decode).
//
// WHY THIS LIVES HERE RATHER THAN IN EACH PLUGIN. What went wrong before ADR-0020 was not any one
// codec's logic: it was that the three disagreed about the same question, and nothing ever asked
// them together. Each codec's own tests pin its arms; this one pins the PROPERTY they must share —
// a tag survives, or it is refused — so a codec that quietly stops recording cannot pass by having
// its own expectations updated alongside it.
//
// Note what is deliberately NOT asserted: that every codec keeps every space. They cannot, and
// pretending otherwise is what produced the silent relabels. The assertion is the disjunction.

#include <lain/image/image.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/memory/buffer.h>
#include <lain/meta/enums.h> // enums::name, for the INFO messages

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

using lain::image::AlphaMode;
using lain::image::ColorSpace;
using lain::image::Image;
using lain::image::PixelFormat;

// A flat mid-grey: JPEG is lossy, so the matrix asserts TAGS rather than pixels, and a flat
// colour keeps the encoders from having anything interesting to lose.
static Image flatImage(PixelFormat format, ColorSpace space, AlphaMode alpha)
{
	Image image(4, 4, format, space, alpha);
	for (std::size_t i = 0; i < image.byteSize(); ++i)
		image.data()[i] = 128;
	return image;
}

// The format keys the build actually enabled. A codec that is not compiled in simply is not
// examined, which is what keeps this test honest in a stripped configuration.
static std::vector<std::string> enabledFormats()
{
	lain::io::image::registerImageCodecs();
	std::vector<std::string> keys;
	for (const char* key : {"png", "tiff", "jpg"})
	{
		if (lain::io::image::readerRegistry().contains(key) && lain::io::image::writerRegistry().contains(key))
			keys.push_back(key);
	}
	return keys;
}

TEST_CASE("every codec either keeps a ColorSpace or refuses it - never silently drops one",
		  "[io-image-codecs][color]")
{
	// The property, stated once. Before ADR-0020 EVERY cell of this table failed: no image writer
	// recorded a colour tag at all, so an sRGB image came back Unspecified and a Linear one came
	// back either Unspecified or — through JPEG — relabelled sRGB over uncorrected pixels.
	for (const std::string& format : enabledFormats())
	{
		for (const ColorSpace space :
			 {ColorSpace::Unspecified, ColorSpace::sRGB, ColorSpace::Linear, ColorSpace::BT709})
		{
			const Image original = flatImage(PixelFormat::RGB8, space, AlphaMode::Unspecified);

			if (!lain::io::image::canEncode(format, original))
			{
				// A refusal is a pass: the codec cannot state this space, and says so instead of
				// writing a file that reads back as something else.
				INFO(format << " refuses " << original.toString());
				REQUIRE_FALSE(lain::io::image::encode(format, original).has_value());
				continue;
			}

			const auto encoded = lain::io::image::encode(format, original);
			INFO(format << " accepted " << original.toString());
			REQUIRE(encoded.has_value());
			const auto decoded = lain::io::image::decode(format, *encoded);
			REQUIRE(decoded.has_value());

			// The one licensed exception, and it is the FORMAT speaking rather than lain: a JPEG
			// always carries a JFIF header, so an image written with no claim comes back carrying
			// the convention that header implies. Every other accepted cell must be exact.
			if (space == ColorSpace::Unspecified && decoded->colorSpace() == ColorSpace::sRGB)
			{
				REQUIRE(format == "jpg");
				continue;
			}
			REQUIRE(decoded->colorSpace() == space);
		}
	}
}

TEST_CASE("every codec either keeps an AlphaMode or refuses it", "[io-image-codecs][color]")
{
	// The same property on the other tracked tag. Two real defects lived in this table: PNG
	// accepted Premultiplied and handed it back as Straight over already-scaled colour, and TIFF
	// wrote "unassociated" for an image whose alpha mode lain had never been told.
	for (const std::string& format : enabledFormats())
	{
		for (const AlphaMode mode : {AlphaMode::Unspecified, AlphaMode::Straight, AlphaMode::Premultiplied})
		{
			const Image original = flatImage(PixelFormat::RGBA8, ColorSpace::sRGB, mode);

			if (!lain::io::image::canEncode(format, original))
			{
				INFO(format << " refuses " << original.toString());
				continue;
			}

			const auto encoded = lain::io::image::encode(format, original);
			INFO(format << " accepted " << original.toString());
			REQUIRE(encoded.has_value());
			const auto decoded = lain::io::image::decode(format, *encoded);
			REQUIRE(decoded.has_value());

			// PNG's alpha is unassociated by specification, so an image written with no claim
			// comes back Straight — the format supplying a fact it guarantees. Premultiplied is
			// refused above rather than reaching this point.
			if (mode == AlphaMode::Unspecified && decoded->alphaMode() == AlphaMode::Straight)
			{
				REQUIRE(format == "png");
				continue;
			}
			REQUIRE(decoded->alphaMode() == mode);
		}
	}
}

TEST_CASE("at least one still codec can carry every ColorSpace lain has", "[io-image-codecs][color]")
{
	// Refusing is only an honest answer while some format accepts. If a ColorSpace is added and no
	// codec can state it, this fails — which is the moment to decide where it will live, rather
	// than discovering later that saving it has quietly been impossible all along.
	for (const ColorSpace space : {ColorSpace::Unspecified, ColorSpace::sRGB, ColorSpace::Linear, ColorSpace::BT709})
	{
		bool carried = false;
		for (const std::string& format : enabledFormats())
		{
			const Image original = flatImage(PixelFormat::RGB8, space, AlphaMode::Unspecified);
			if (!lain::io::image::canEncode(format, original))
				continue;
			const auto encoded = lain::io::image::encode(format, original);
			REQUIRE(encoded.has_value());
			const auto decoded = lain::io::image::decode(format, *encoded);
			REQUIRE(decoded.has_value());
			if (decoded->colorSpace() == space)
				carried = true;
		}
		INFO("no enabled codec round-trips " << lain::meta::enums::name(space));
		REQUIRE(carried);
	}
}
