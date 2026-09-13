// The encoder-options matrix, over every codec the build enabled — M13 slice 5's claim as one
// table, driven through the production facade (registerImageCodecs then encode/decode).
//
// WHY THIS LIVES HERE RATHER THAN IN EACH PLUGIN, which is the same argument test_colorroundtrip.cpp
// makes: what a knob can go wrong at is not one codec's mapping — each plugin's own cases pin those
// — but the three codecs disagreeing about what a setting MEANS. A Compression that quietly lost
// pixels in one of them, or a quality that changed a lossless file, would pass every per-codec test
// that was written alongside it. So the assertion here is the property they must share.
//
// Note what is deliberately NOT asserted: that a setting changes the bytes. A codec is allowed to
// ignore either field — jpeg ignores Compression, png and tiff ignore quality — and pretending
// otherwise would be inventing a requirement none of them agreed to.

#include <lain/image/image.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/load.h>
#include <lain/io/image/save.h>
#include <lain/memory/buffer.h>
#include <lain/meta/enums.h> // enums::name, for the INFO messages

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using lain::image::AlphaMode;
using lain::image::ColorSpace;
using lain::image::Image;
using lain::image::PixelFormat;
using lain::io::image::Compression;
using lain::io::image::ImageWriterOptions;

// A gradient carrying a small deterministic jitter: enough structure that compression has work to
// do and quality has detail to spend, which a flat colour gives neither of.
static Image textureImage()
{
	Image image(32, 32, PixelFormat::RGB8, ColorSpace::sRGB, AlphaMode::Unspecified);
	for (std::size_t i = 0; i < image.byteSize(); ++i)
	{
		const std::size_t x = (i / 3) % 32;
		const std::size_t y = (i / 3) / 32;
		image.data()[i] = static_cast<std::uint8_t>((x * 2 + y * 3) ^ ((i * 37) & 0x1f));
	}
	return image;
}

// The format keys the build actually enabled — a codec that is not compiled in is not examined,
// which is what keeps this honest in a stripped configuration.
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

static const Compression kEverySetting[] = {Compression::Default, Compression::None, Compression::Fast,
											Compression::Small};

TEST_CASE("every codec reads back what it wrote, at every Compression setting", "[io-image-codecs][options]")
{
	// The floor under the whole knob: a setting that produced a file its own codec cannot reopen
	// would be worse than no setting at all, and libtiff in particular will happily write
	// compressions this build was not configured to read.
	const Image original = textureImage();

	for (const std::string& format : enabledFormats())
	{
		for (const Compression compression : kEverySetting)
		{
			ImageWriterOptions options;
			options.compression = compression;

			INFO(format << " at " << lain::meta::enums::name(compression));
			const auto encoded = lain::io::image::encode(format, original, options);
			REQUIRE(encoded.has_value());

			const auto decoded = lain::io::image::decode(format, *encoded);
			REQUIRE(decoded.has_value());
			REQUIRE(decoded->width() == original.width());
			REQUIRE(decoded->height() == original.height());
			REQUIRE(decoded->pixelFormat() == original.pixelFormat());
		}
	}
}

TEST_CASE("Compression never costs a pixel in a codec that calls itself lossless", "[io-image-codecs][options]")
{
	// The sharp half. Compression trades TIME for SIZE; quality is the field that trades fidelity,
	// and a codec that let the two blur would lose data on a request that never mentioned it.
	const Image original = textureImage();

	for (const std::string& format : enabledFormats())
	{
		if (lain::io::image::isLossy(format))
			continue; // it loses detail by being itself; the per-codec cases measure how much

		for (const Compression compression : kEverySetting)
		{
			ImageWriterOptions options;
			options.compression = compression;

			INFO(format << " at " << lain::meta::enums::name(compression));
			const auto encoded = lain::io::image::encode(format, original, options);
			REQUIRE(encoded.has_value());
			const auto decoded = lain::io::image::decode(format, *encoded);
			REQUIRE(decoded.has_value());
			for (std::size_t i = 0; i < original.byteSize(); ++i)
				REQUIRE(decoded->data()[i] == original.data()[i]);
		}
	}
}

TEST_CASE("a lossless codec ignores a quality rather than spending it on something else",
		  "[io-image-codecs][options]")
{
	// isLossy() is what a host asks before offering a quality control, so it has to be the truth
	// about what the codec then does with one. A codec answering false while quietly acting on a
	// quality would make that control invisible and effective at once.
	const Image original = textureImage();

	for (const std::string& format : enabledFormats())
	{
		if (lain::io::image::isLossy(format))
			continue;

		ImageWriterOptions asked;
		asked.quality = 5; // about as far from a default as a caller can ask

		INFO(format << " says it is lossless");
		const auto plain = lain::io::image::encode(format, original);
		const auto withQuality = lain::io::image::encode(format, original, asked);
		REQUIRE(plain.has_value());
		REQUIRE(withQuality.has_value());
		REQUIRE(plain->size() == withQuality->size());
		REQUIRE(std::memcmp(plain->data(), withQuality->data(), plain->size()) == 0);
	}
}

TEST_CASE("exactly one still format here is lossy, and it is the one that takes a quality",
		  "[io-image-codecs][options]")
{
	// A census rather than a behaviour, and it earns its place by failing loudly the day a codec
	// changes its mind: isLossy is what the gui gates its quality control on and what the cli warns
	// from, so a wrong answer is silent in both.
	for (const std::string& format : enabledFormats())
	{
		INFO(format);
		REQUIRE(lain::io::image::isLossy(format) == (format == "jpg"));
	}

	// An unregistered key is not lossy, because there is nothing there to lose anything — the same
	// answer canEncode gives, and the reason both are quiet predicates.
	REQUIRE_FALSE(lain::io::image::isLossy("no-such-format"));
}
