// Unit tests for the lain::io::image seam (decode/load + the reader registry). Tests
// the production dispatch path with a FAKE in-memory reader — the seam is verified
// without any real codec, exactly as intended. Covers: decode by key, the load()
// facade end-to-end (read a temp file, key by extension, decode), and the failure
// paths (unknown format, missing extension, missing file, a reader that rejects bytes).

#include "lain/io/image/load.h"
#include "lain/io/image/reader.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using lain::io::image::decode;
using lain::io::image::ImageReader;
using lain::io::image::load;
using lain::io::image::readerRegistry;

// A fake reader: decodes to a 1-row RGBA8 image whose width is the input byte count,
// so a test can assert the exact bytes reached the reader. An empty input decodes to
// an invalid Image (width 0) — the seam's "reader rejected the bytes" path.
class FakeReader : public ImageReader
{
public:
	lain::image::Image decode(const lain::memory::Buffer& bytes) const override
	{
		return lain::image::Image(static_cast<int>(bytes.size()), 1, lain::image::PixelFormat::RGBA8);
	}
};

// Writes bytes to a uniquely-named temp file with the given extension and removes it
// on destruction — an isolated, self-cleaning on-disk fixture per test.
class TempFile
{
public:
	TempFile(const std::string& extension, const std::vector<std::uint8_t>& bytes)
	{
		static int counter = 0;
		m_path = std::filesystem::temp_directory_path() /
				 ("lain_ioimage_" + std::to_string(counter++) + "." + extension);
		std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}

	~TempFile()
	{
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
	}

	TempFile(const TempFile&) = delete;
	TempFile& operator=(const TempFile&) = delete;

	std::string path() const { return m_path.string(); }

private:
	std::filesystem::path m_path;
};

// Registers the fake reader under a test key once (registerType replaces, so repeat
// registration across TEST_CASEs is harmless — the global registry is process-wide).
static void registerFake(const std::string& key)
{
	readerRegistry().registerType<FakeReader>(key);
}

TEST_CASE("decode dispatches to the reader registered for a format key", "[io-image]")
{
	registerFake("fake");
	const lain::memory::Buffer bytes(7);

	const auto image = decode("fake", bytes);
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 7);
	REQUIRE(image->height() == 1);
}

TEST_CASE("decode returns nullopt for an unregistered format key", "[io-image]")
{
	const lain::memory::Buffer bytes(4);
	REQUIRE_FALSE(decode("no-such-format", bytes).has_value());
}

TEST_CASE("decode returns nullopt when the reader rejects the bytes", "[io-image]")
{
	registerFake("fake");
	const lain::memory::Buffer empty; // FakeReader -> width 0 -> invalid Image
	REQUIRE_FALSE(decode("fake", empty).has_value());
}

TEST_CASE("load reads a file and keys the reader by its extension", "[io-image]")
{
	registerFake("fake");
	const std::vector<std::uint8_t> content{1, 2, 3, 4, 5};
	const TempFile file("fake", content);

	const auto image = load(file.path());
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 5); // the five bytes reached the reader
}

TEST_CASE("load matches the extension case-insensitively", "[io-image]")
{
	registerFake("fake");
	const std::vector<std::uint8_t> content{9, 9, 9};
	const TempFile file("FAKE", content);

	const auto image = load(file.path());
	REQUIRE(image.has_value());
	REQUIRE(image->width() == 3);
}

TEST_CASE("load returns nullopt for an unknown format", "[io-image]")
{
	const std::vector<std::uint8_t> content{1, 2, 3};
	const TempFile file("unknownformat", content);
	REQUIRE_FALSE(load(file.path()).has_value());
}

TEST_CASE("load returns nullopt when the uri has no extension", "[io-image]")
{
	const std::vector<std::uint8_t> content{1, 2, 3};
	const TempFile file("", content); // trailing dot, empty extension
	REQUIRE_FALSE(load(file.path()).has_value());
}

TEST_CASE("load returns nullopt when the file is missing", "[io-image]")
{
	registerFake("fake");
	const auto missing = std::filesystem::temp_directory_path() / "lain_ioimage_absent.fake";
	REQUIRE_FALSE(load(missing.string()).has_value());
}
