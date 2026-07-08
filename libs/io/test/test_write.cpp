// Unit tests for lain::io::write. Writes real files to a temp dir (the production path) and
// reads them back via io::read to confirm the round-trip; covers the empty file, an
// unsupported scheme, and a write into a missing directory. No driver.

#include "lain/io/read.h"
#include "lain/io/write.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using lain::io::read;
using lain::io::write;

// A Buffer holding a copy of the given bytes.
static lain::memory::Buffer bufferFrom(const std::vector<std::uint8_t>& bytes)
{
	lain::memory::Buffer buffer(bytes.size());
	if (!bytes.empty())
		std::memcpy(buffer.data(), bytes.data(), bytes.size());
	return buffer;
}

// A uniquely-named temp path removed on destruction (write() creates the file).
class TempPath
{
public:
	explicit TempPath(const std::string& extension)
	{
		static int counter = 0;
		m_path = std::filesystem::temp_directory_path() /
				 ("lain_iowrite_" + std::to_string(counter++) + "." + extension);
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

TEST_CASE("write then read round-trips the exact bytes", "[write]")
{
	const std::vector<std::uint8_t> content{'l', 'a', 'i', 'n', 0x00, 0xFF, 0x42};
	const TempPath path("bin");

	REQUIRE(write(path.string(), bufferFrom(content)));

	const auto readBack = read(path.string());
	REQUIRE(readBack.has_value());
	REQUIRE(bytesEqual(*readBack, content));
}

TEST_CASE("write of an empty buffer makes a zero-length file", "[write]")
{
	const TempPath path("bin");
	REQUIRE(write(path.string(), lain::memory::Buffer{}));

	const auto readBack = read(path.string());
	REQUIRE(readBack.has_value());
	REQUIRE(readBack->size() == 0);
}

TEST_CASE("write of an unsupported scheme fails", "[write]")
{
	REQUIRE_FALSE(write("s3://bucket/key", bufferFrom({1, 2, 3})));
}

TEST_CASE("write into a missing directory fails", "[write]")
{
	const auto missing = std::filesystem::temp_directory_path() / "lain_no_such_dir" / "x.bin";
	REQUIRE_FALSE(write(missing.string(), bufferFrom({1, 2, 3})));
}
