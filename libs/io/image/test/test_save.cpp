// Unit tests for the lain::io::image write seam (encode/save + the writer registry). Tests
// the production path with a FAKE writer — the seam is verified without any real codec.
// Covers: encode by key, the save() facade end-to-end (encode + io::write, read back to
// confirm the bytes), and the failure paths (unknown format, invalid image, missing
// extension).

#include "lain/io/image/save.h"
#include "lain/io/image/writer.h"

#include <lain/image/image.h>
#include <lain/io/read.h>
#include <lain/memory/buffer.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

using lain::io::image::encode;
using lain::io::image::ImageWriter;
using lain::io::image::save;
using lain::io::image::writerRegistry;

// A fake writer: encodes an Image to a Buffer that is a byte-for-byte copy of its pixels, so
// a test can confirm the exact bytes save() wrote (read them back with io::read). An invalid
// image encodes to nullopt — the writer's "can't encode this" path.
class FakeWriter : public ImageWriter
{
public:
	bool canEncode(const lain::image::Image& image) const override { return image.valid(); }

	std::optional<lain::memory::Buffer> encode(const lain::image::Image& image) const override
	{
		if (!image.valid())
			return std::nullopt;
		lain::memory::Buffer buffer(image.byteSize());
		std::memcpy(buffer.data(), image.data(), image.byteSize());
		return buffer;
	}
};

// A uniquely-named temp path removed on destruction (save creates the file).
class TempPath
{
public:
	explicit TempPath(const std::string& extension)
	{
		static int counter = 0;
		m_path = std::filesystem::temp_directory_path() /
				 ("lain_iosave_" + std::to_string(counter++) + "." + extension);
	}
	~TempPath()
	{
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
	}
	TempPath(const TempPath&) = delete;
	TempPath& operator=(const TempPath&) = delete;
	std::string string() const { return m_path.string(); }

private:
	std::filesystem::path m_path;
};

// A 2x2 RGBA8 image with a recognisable byte ramp.
static lain::image::Image rampImage()
{
	lain::image::Image image(2, 2, lain::image::PixelFormat::RGBA8);
	std::uint8_t* px = image.data();
	for (std::size_t i = 0; i < image.byteSize(); ++i)
		px[i] = static_cast<std::uint8_t>(i);
	return image;
}

TEST_CASE("encode dispatches to the writer registered for a format key", "[io-image-save]")
{
	writerRegistry().registerType<FakeWriter>("fake");
	const lain::image::Image image = rampImage();

	const auto bytes = encode("fake", image);
	REQUIRE(bytes.has_value());
	REQUIRE(bytes->size() == image.byteSize()); // 2*2*4 = 16
}

TEST_CASE("encode returns nullopt for an unregistered format", "[io-image-save]")
{
	REQUIRE_FALSE(encode("no-such-format", rampImage()).has_value());
}

TEST_CASE("save encodes then writes the exact bytes to the file", "[io-image-save]")
{
	writerRegistry().registerType<FakeWriter>("fake");
	const lain::image::Image image = rampImage();
	const TempPath path("fake");

	REQUIRE(save(path.string(), image));

	// Read the file back and confirm it holds the encoded bytes.
	const auto readBack = lain::io::read(path.string());
	REQUIRE(readBack.has_value());
	REQUIRE(readBack->size() == image.byteSize());
	const auto* got = reinterpret_cast<const std::uint8_t*>(readBack->data());
	for (std::size_t i = 0; i < image.byteSize(); ++i)
		REQUIRE(got[i] == static_cast<std::uint8_t>(i));
}

TEST_CASE("save refuses an invalid image", "[io-image-save]")
{
	writerRegistry().registerType<FakeWriter>("fake");
	REQUIRE_FALSE(save("/tmp/lain_iosave_invalid.fake", lain::image::Image{}));
}

TEST_CASE("save fails when the uri has no extension", "[io-image-save]")
{
	writerRegistry().registerType<FakeWriter>("fake");
	const TempPath path(""); // trailing dot -> empty extension
	REQUIRE_FALSE(save(path.string(), rampImage()));
}
