// Unit tests for the lain::io::data seam (decode/load + encode/save + the registries). Tests the
// production dispatch path with FAKE in-memory codecs — no real parser — covering: dispatch by key,
// the load/save facades end-to-end (temp file, key by extension), and the failure paths.

#include "lain/io/data/load.h"
#include "lain/io/data/reader.h"
#include "lain/io/data/save.h"
#include "lain/io/data/writer.h"

#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

using lain::data::Value;
using lain::io::data::decode;
using lain::io::data::encode;
using lain::io::data::load;
using lain::io::data::save;

// A fake reader: decodes non-empty bytes to an Object { "size": <byte count> } so a test can assert
// the exact bytes reached it. Empty input decodes to nullopt — the seam's "reader rejected" path.
class FakeReader : public lain::io::data::DataReader
{
public:
	std::optional<Value> decode(const lain::memory::Buffer& bytes) const override
	{
		if (bytes.empty())
			return std::nullopt;
		Value v = Value::object();
		v.set("size", Value(static_cast<std::int64_t>(bytes.size())));
		return v;
	}
};

// A fake writer: encodes any value to a fixed 5-byte buffer, so the save path is observable on disk.
class FakeWriter : public lain::io::data::DataWriter
{
public:
	std::optional<lain::memory::Buffer> encode(const Value&) const override
	{
		return lain::memory::Buffer(5);
	}
};

static void registerFake(const std::string& key)
{
	lain::io::data::readerRegistry().registerType<FakeReader>(key);
	lain::io::data::writerRegistry().registerType<FakeWriter>(key);
}

// A uniquely-named temp file removed on destruction.
class TempPath
{
public:
	explicit TempPath(const std::string& extension)
	{
		m_path = lain::testing::scratchPath("iodata", "." + extension);
	}
	~TempPath()
	{
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
	}
	TempPath(const TempPath&) = delete;
	TempPath& operator=(const TempPath&) = delete;

	std::string path() const { return m_path.string(); }

private:
	std::filesystem::path m_path;
};

TEST_CASE("decode dispatches to the reader for a format key", "[io-data]")
{
	registerFake("fake");
	const lain::memory::Buffer bytes(7);

	const auto v = decode("fake", bytes);
	REQUIRE(v.has_value());
	REQUIRE(v->find("size")->asInt64() == 7);
}

TEST_CASE("decode returns nullopt for an unregistered format key", "[io-data]")
{
	const lain::memory::Buffer bytes(4);
	REQUIRE_FALSE(decode("no-such-format", bytes).has_value());
}

TEST_CASE("decode returns nullopt when the reader rejects the bytes", "[io-data]")
{
	registerFake("fake");
	const lain::memory::Buffer empty; // FakeReader -> nullopt
	REQUIRE_FALSE(decode("fake", empty).has_value());
}

TEST_CASE("encode dispatches to the writer for a format key", "[io-data]")
{
	registerFake("fake");
	const auto bytes = encode("fake", Value(42));
	REQUIRE(bytes.has_value());
	REQUIRE(bytes->size() == 5);
}

TEST_CASE("encode returns nullopt for an unregistered format key", "[io-data]")
{
	REQUIRE_FALSE(encode("no-such-format", Value(1)).has_value());
}

TEST_CASE("save then load round-trips through a file keyed by extension", "[io-data]")
{
	registerFake("fake");
	const TempPath file("fake");

	REQUIRE(save(file.path(), Value(1))); // FakeWriter -> 5 bytes on disk
	REQUIRE(std::filesystem::file_size(file.path()) == 5);

	const auto v = load(file.path()); // FakeReader keyed off ".fake" -> { size: 5 }
	REQUIRE(v.has_value());
	REQUIRE(v->find("size")->asInt64() == 5);
}

TEST_CASE("save returns false for an unknown extension", "[io-data]")
{
	const TempPath file("unknownformat");
	REQUIRE_FALSE(save(file.path(), Value(1)));
}

TEST_CASE("load returns nullopt when the uri has no extension", "[io-data]")
{
	REQUIRE_FALSE(load("/tmp/no_extension_here").has_value());
}
