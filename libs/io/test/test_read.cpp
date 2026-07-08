// Unit tests for lain::io::read. Exercises the production path against real files
// written to a temp dir (a file reader is tested by reading files); covers exact
// byte/size round-trip, the empty-file and binary cases, the local/file schemes, and
// the failure paths (missing file, unsupported scheme). No driver.

#include "lain/io/read.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using lain::io::read;

// Writes given bytes to a uniquely-named file in the temp dir and removes it on
// destruction — so each test owns an isolated, self-cleaning fixture on disk.
class TempFile
{
public:
	explicit TempFile(const std::vector<std::uint8_t>& bytes)
	{
		static int counter = 0;
		m_path = std::filesystem::temp_directory_path() /
				 ("lain_io_test_" + std::to_string(counter++) + ".bin");
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

TEST_CASE("read returns a file's exact bytes and size", "[read]")
{
	const std::vector<std::uint8_t> content{'l', 'a', 'i', 'n', 0x00, 0xFF, 0x42};
	const TempFile file(content);

	const auto buffer = read(file.path());
	REQUIRE(buffer.has_value());
	REQUIRE(buffer->size() == content.size());
	REQUIRE(bytesEqual(*buffer, content));
}

TEST_CASE("read of an empty file yields a valid empty Buffer", "[read]")
{
	const TempFile file({});

	const auto buffer = read(file.path());
	REQUIRE(buffer.has_value());
	REQUIRE(buffer->size() == 0);
	REQUIRE(buffer->empty());
}

TEST_CASE("read round-trips every byte value", "[read]")
{
	std::vector<std::uint8_t> content(256);
	for (int i = 0; i < 256; ++i)
		content[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
	const TempFile file(content);

	const auto buffer = read(file.path());
	REQUIRE(buffer.has_value());
	REQUIRE(bytesEqual(*buffer, content));
}

TEST_CASE("a bare path and a file:// uri read the same file", "[read]")
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

TEST_CASE("read of a missing file returns nullopt", "[read]")
{
	const auto missing = std::filesystem::temp_directory_path() / "lain_io_does_not_exist.bin";
	REQUIRE_FALSE(read(missing.string()).has_value());
}

TEST_CASE("read of an unsupported scheme returns nullopt", "[read]")
{
	REQUIRE_FALSE(read("http://example.com/image.png").has_value());
	REQUIRE_FALSE(read("s3://bucket/key").has_value());
}
