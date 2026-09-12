// Unit tests for the transport ENTRY POINTS — lain::io::read / write / openStream / createStream.
// Exercises the production path against real files written to a temp dir (a file transport is
// tested by reading and writing files); covers the exact byte/size round-trip, the empty-file and
// binary cases, the bare-path and file:// spellings, and what each of the four refuses. No driver.
//
// The Stream CONTRACT those last two hand out — seek, release and resume, finish — is
// test_stream.cpp's, the way this file pairs with transport.cpp and that one with stream.cpp.

#include "lain/io/transport.h"

#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using lain::io::createStream;
using lain::io::openStream;
using lain::io::read;
using lain::io::write;

// Writes given bytes to a uniquely-named file in the temp dir and removes it on
// destruction — so each test owns an isolated, self-cleaning fixture on disk.
class TempFile
{
public:
	explicit TempFile(const std::vector<std::uint8_t>& bytes)
	{
		m_path = lain::testing::scratchPath("iotransport", ".bin");
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

// A uniquely-named temp path removed on destruction (write() creates the file).
class TempPath
{
public:
	explicit TempPath(const std::string& extension)
	{
		m_path = lain::testing::scratchPath("iotransport", "." + extension);
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

// A Buffer holding a copy of the given bytes.
static lain::memory::Buffer bufferFrom(const std::vector<std::uint8_t>& bytes)
{
	lain::memory::Buffer buffer(bytes.size());
	if (!bytes.empty())
		std::memcpy(buffer.data(), bytes.data(), bytes.size());
	return buffer;
}

// True when a Buffer's bytes equal the expected sequence.
static bool bytesEqual(const lain::memory::Buffer& buffer, const std::vector<std::uint8_t>& expected)
{
	if (buffer.size() != expected.size())
		return false;
	const auto* data = reinterpret_cast<const std::uint8_t*>(buffer.data());
	for (std::size_t i = 0; i < expected.size(); ++i)
	{
		if (data[i] != expected[i])
			return false;
	}
	return true;
}

// --- the whole-asset pair --------------------------------------------------

TEST_CASE("read returns a file's exact bytes and size", "[transport]")
{
	const std::vector<std::uint8_t> content{'l', 'a', 'i', 'n', 0x00, 0xFF, 0x42};
	const TempFile file(content);

	const auto buffer = read(file.path());
	REQUIRE(buffer.has_value());
	REQUIRE(buffer->size() == content.size());
	REQUIRE(bytesEqual(*buffer, content));
}

TEST_CASE("read of an empty file yields a valid empty Buffer", "[transport]")
{
	const TempFile file({});

	const auto buffer = read(file.path());
	REQUIRE(buffer.has_value());
	REQUIRE(buffer->size() == 0);
	REQUIRE(buffer->empty());
}

TEST_CASE("read round-trips every byte value", "[transport]")
{
	std::vector<std::uint8_t> content(256);
	for (int i = 0; i < 256; ++i)
		content[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
	const TempFile file(content);

	const auto buffer = read(file.path());
	REQUIRE(buffer.has_value());
	REQUIRE(bytesEqual(*buffer, content));
}

TEST_CASE("a bare path and a file:// uri read the same file", "[transport]")
{
	const std::vector<std::uint8_t> content{1, 2, 3, 4};
	const TempFile file(content);

	const auto bare = read(file.path());
	const auto scheme = read("file://" + file.path());
	REQUIRE(bare.has_value());
	REQUIRE(scheme.has_value());
	REQUIRE(bytesEqual(*bare, content));
	REQUIRE(bytesEqual(*scheme, content));
}

TEST_CASE("read of a missing file returns nullopt", "[transport]")
{
	const auto missing = lain::testing::scratchPath("absent", ".bin");
	REQUIRE_FALSE(read(missing.string()).has_value());
}

TEST_CASE("read of an unsupported scheme returns nullopt", "[transport]")
{
	REQUIRE_FALSE(read("http://example.com/image.png").has_value());
	REQUIRE_FALSE(read("s3://bucket/key").has_value());
}

TEST_CASE("write then read round-trips the exact bytes", "[transport]")
{
	const std::vector<std::uint8_t> content{'l', 'a', 'i', 'n', 0x00, 0xFF, 0x42};
	const TempPath path("bin");

	REQUIRE(write(path.string(), bufferFrom(content)));

	const auto readBack = read(path.string());
	REQUIRE(readBack.has_value());
	REQUIRE(bytesEqual(*readBack, content));
}

TEST_CASE("write of an empty buffer makes a zero-length file", "[transport]")
{
	const TempPath path("bin");
	REQUIRE(write(path.string(), lain::memory::Buffer{}));

	const auto readBack = read(path.string());
	REQUIRE(readBack.has_value());
	REQUIRE(readBack->size() == 0);
}

TEST_CASE("write of an unsupported scheme fails", "[transport]")
{
	REQUIRE_FALSE(write("s3://bucket/key", bufferFrom({1, 2, 3})));
}

TEST_CASE("write into a missing directory fails", "[transport]")
{
	const auto missing = lain::testing::scratchPath("absent-dir") / "x.bin";
	REQUIRE_FALSE(write(missing.string(), bufferFrom({1, 2, 3})));
}

// --- handing out a stream --------------------------------------------------
//
// What an opened stream then DOES is test_stream.cpp's; these two are about the dispatch that
// decides whether there is one to hand out at all, which is this file's.

TEST_CASE("openStream refuses what it cannot read", "[transport]")
{
	const auto missing = lain::testing::scratchPath("absent", ".bin");
	REQUIRE(openStream(missing.string()) == nullptr);

	// A directory opens perfectly well on some platforms and reads nothing — the regular-file
	// guard is what keeps that from looking like an empty file.
	REQUIRE(openStream(std::filesystem::temp_directory_path().string()) == nullptr);

	REQUIRE(openStream("http://example.com/clip.mp4") == nullptr);
	REQUIRE(openStream("s3://bucket/key") == nullptr);
}

TEST_CASE("createStream refuses what it cannot create", "[transport]")
{
	const auto missing = lain::testing::scratchPath("absent-dir") / "out.bin";
	REQUIRE(createStream(missing.string()) == nullptr);

	REQUIRE(createStream("http://example.com/clip.mp4") == nullptr);
	REQUIRE(createStream("s3://bucket/key") == nullptr);
}
