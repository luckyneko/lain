// Unit tests for lain::io::openStream / createStream — the incremental transport. Exercises
// the production path against real files in a temp dir (a file transport is tested by
// reading and writing files), and reads written bytes back through io::read so the two
// shapes of the seam are checked against each other. No driver.
//
// Two cases carry the design rather than its surface: a released handle resumes at the
// logical position, and a released WRITE handle re-acquires WITHOUT truncating. Those are
// the promises a future handle pool is built on, and both fail loudly if the backend starts
// tracking its own position or reopens with std::ios::trunc.

#include "lain/io/read.h"
#include "lain/io/stream.h"
#include "lain/io/uri.h"

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
using lain::io::SeekOrigin;

// Writes given bytes to a uniquely-named file in the temp dir and removes it on
// destruction — so each test owns an isolated, self-cleaning fixture on disk.
class TempFile
{
public:
	explicit TempFile(const std::vector<std::uint8_t>& bytes)
	{
		static int counter = 0;
		m_path = std::filesystem::temp_directory_path() /
				 ("lain_iostream_test_" + std::to_string(counter++) + ".bin");
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

// A uniquely-named temp path removed on destruction (createStream creates the file).
class TempPath
{
public:
	TempPath()
	{
		static int counter = 0;
		m_path = std::filesystem::temp_directory_path() /
				 ("lain_iostreamw_test_" + std::to_string(counter++) + ".bin");
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

// The bytes 0..n-1, so any misplaced read or write shows up as a wrong VALUE rather than
// only a wrong length.
static std::vector<std::uint8_t> ramp(std::size_t n)
{
	std::vector<std::uint8_t> bytes(n);
	for (std::size_t i = 0; i < n; ++i)
		bytes[i] = static_cast<std::uint8_t>(i);
	return bytes;
}

static std::vector<std::uint8_t> asBytes(const std::string& text)
{
	return std::vector<std::uint8_t>(text.begin(), text.end());
}

// Whatever is on disk at `path` right now, read through the production whole-asset path.
static std::vector<std::uint8_t> contentsOf(const std::string& path)
{
	const auto buffer = lain::io::read(path);
	REQUIRE(buffer.has_value());
	const auto* data = reinterpret_cast<const std::uint8_t*>(buffer->data());
	return std::vector<std::uint8_t>(data, data + buffer->size());
}

// --- reading ---------------------------------------------------------------

TEST_CASE("a read stream reports its size and delivers the file's bytes", "[stream]")
{
	const auto content = ramp(64);
	const TempFile file(content);

	auto stream = openStream(file.path());
	REQUIRE(stream != nullptr);
	REQUIRE(stream->size() == std::uint64_t{64});
	REQUIRE(stream->position() == std::uint64_t{0});

	std::vector<std::uint8_t> got(64);
	REQUIRE(stream->read(reinterpret_cast<std::byte*>(got.data()), got.size()) == std::size_t{64});
	REQUIRE(got == content);
	REQUIRE(stream->position() == std::uint64_t{64});
	REQUIRE(stream->atEnd());
}

TEST_CASE("sequential reads continue where the last one stopped", "[stream]")
{
	const auto content = ramp(10);
	const TempFile file(content);

	auto stream = openStream(file.path());
	REQUIRE(stream != nullptr);

	std::vector<std::uint8_t> first(4);
	REQUIRE(stream->read(reinterpret_cast<std::byte*>(first.data()), first.size()) == std::size_t{4});
	REQUIRE(stream->position() == std::uint64_t{4});

	std::vector<std::uint8_t> rest(6);
	REQUIRE(stream->read(reinterpret_cast<std::byte*>(rest.data()), rest.size()) == std::size_t{6});

	first.insert(first.end(), rest.begin(), rest.end());
	REQUIRE(first == content);
}

TEST_CASE("a read past the end is short, then empty — not a failure", "[stream]")
{
	const TempFile file(ramp(4));

	auto stream = openStream(file.path());
	REQUIRE(stream != nullptr);

	std::vector<std::uint8_t> got(16);
	// A short count means the end of the resource; nullopt would mean the transport broke,
	// and a decoder must be able to tell those apart.
	const auto read = stream->read(reinterpret_cast<std::byte*>(got.data()), got.size());
	REQUIRE(read == std::size_t{4});
	REQUIRE(stream->atEnd());

	const auto again = stream->read(reinterpret_cast<std::byte*>(got.data()), got.size());
	REQUIRE(again.has_value());
	REQUIRE(*again == std::size_t{0});
}

TEST_CASE("seek moves the logical position from every origin", "[stream]")
{
	const auto content = ramp(32);
	const TempFile file(content);

	auto stream = openStream(file.path());
	REQUIRE(stream != nullptr);

	std::uint8_t byte = 0;
	auto readOne = [&]
	{
		return stream->read(reinterpret_cast<std::byte*>(&byte), 1);
	};

	REQUIRE(stream->seek(20) == std::uint64_t{20});
	REQUIRE(readOne() == std::size_t{1});
	REQUIRE(byte == 20);

	REQUIRE(stream->seek(4, SeekOrigin::Current) == std::uint64_t{25});
	REQUIRE(readOne() == std::size_t{1});
	REQUIRE(byte == 25);

	REQUIRE(stream->seek(-2, SeekOrigin::End) == std::uint64_t{30});
	REQUIRE(readOne() == std::size_t{1});
	REQUIRE(byte == 30);
}

TEST_CASE("a seek before the start fails and leaves the position alone", "[stream]")
{
	const TempFile file(ramp(8));

	auto stream = openStream(file.path());
	REQUIRE(stream != nullptr);
	REQUIRE(stream->seek(4) == std::uint64_t{4});

	REQUIRE_FALSE(stream->seek(-5, SeekOrigin::Current).has_value());
	REQUIRE(stream->position() == std::uint64_t{4});
}

TEST_CASE("seeking past the end is allowed and reads nothing there", "[stream]")
{
	const TempFile file(ramp(8));

	auto stream = openStream(file.path());
	REQUIRE(stream != nullptr);
	REQUIRE(stream->seek(100) == std::uint64_t{100});

	std::uint8_t byte = 0;
	const auto read = stream->read(reinterpret_cast<std::byte*>(&byte), 1);
	REQUIRE(read.has_value());
	REQUIRE(*read == std::size_t{0});
}

TEST_CASE("a released read handle re-acquires at the logical position", "[stream]")
{
	// The whole point of the seam: the base owns the position, so dropping the OS handle
	// (what a handle pool does when it evicts) is invisible to the caller.
	const auto content = ramp(32);
	const TempFile file(content);

	auto stream = openStream(file.path());
	REQUIRE(stream != nullptr);

	std::vector<std::uint8_t> first(8);
	REQUIRE(stream->read(reinterpret_cast<std::byte*>(first.data()), first.size()) == std::size_t{8});

	stream->release();
	REQUIRE(stream->position() == std::uint64_t{8});
	REQUIRE(stream->size() == std::uint64_t{32});

	std::vector<std::uint8_t> rest(8);
	REQUIRE(stream->read(reinterpret_cast<std::byte*>(rest.data()), rest.size()) == std::size_t{8});
	REQUIRE(rest == std::vector<std::uint8_t>(content.begin() + 8, content.begin() + 16));

	// And a release with nothing open is a no-op, not an error.
	stream->release();
	stream->release();
	REQUIRE(stream->position() == std::uint64_t{16});
}

TEST_CASE("a read stream reports its uri canonically", "[stream]")
{
	const TempFile file(ramp(4));

	auto bare = openStream(file.path());
	auto scheme = openStream("file://" + file.path());
	REQUIRE(bare != nullptr);
	REQUIRE(scheme != nullptr);
	REQUIRE(bare->uri() == lain::io::canonicalUri(file.path()));
	REQUIRE(bare->uri() == scheme->uri());
}

TEST_CASE("openStream refuses what it cannot read", "[stream]")
{
	const auto missing = std::filesystem::temp_directory_path() / "lain_iostream_does_not_exist.bin";
	REQUIRE(openStream(missing.string()) == nullptr);

	// A directory opens perfectly well on some platforms and reads nothing — the regular-file
	// guard is what keeps that from looking like an empty file.
	REQUIRE(openStream(std::filesystem::temp_directory_path().string()) == nullptr);

	REQUIRE(openStream("http://example.com/clip.mp4") == nullptr);
	REQUIRE(openStream("s3://bucket/key") == nullptr);
}

// --- writing ---------------------------------------------------------------

TEST_CASE("a write stream pushes bytes and finishes", "[stream]")
{
	const TempPath path;

	auto stream = createStream(path.string());
	REQUIRE(stream != nullptr);
	REQUIRE_FALSE(stream->finished());

	const auto head = asBytes("lain");
	const auto tail = asBytes("stream");
	REQUIRE(stream->write(reinterpret_cast<const std::byte*>(head.data()), head.size()));
	REQUIRE(stream->write(reinterpret_cast<const std::byte*>(tail.data()), tail.size()));
	REQUIRE(stream->position() == std::uint64_t{10});
	REQUIRE(stream->size() == std::uint64_t{10});

	REQUIRE(stream->finish());
	REQUIRE(stream->finished());
	// Idempotent: a second finish repeats the first answer rather than closing twice.
	REQUIRE(stream->finish());

	REQUIRE(contentsOf(path.string()) == asBytes("lainstream"));
}

TEST_CASE("a write stream seeks back and overwrites earlier bytes", "[stream]")
{
	// The muxer shape: write a placeholder header, write the payload, then seek back and
	// patch the header once the sizes are known.
	const TempPath path;

	auto stream = createStream(path.string());
	REQUIRE(stream != nullptr);

	const auto placeholder = asBytes("0000payload");
	REQUIRE(stream->write(reinterpret_cast<const std::byte*>(placeholder.data()), placeholder.size()));

	REQUIRE(stream->seek(0) == std::uint64_t{0});
	const auto patched = asBytes("SIZE");
	REQUIRE(stream->write(reinterpret_cast<const std::byte*>(patched.data()), patched.size()));
	REQUIRE(stream->finish());

	REQUIRE(contentsOf(path.string()) == asBytes("SIZEpayload"));
}

TEST_CASE("a released write handle re-acquires without truncating", "[stream]")
{
	// The one failure this suite exists to catch: re-opening with std::ios::trunc would
	// throw away everything written before the release, and nothing would report it.
	const TempPath path;

	auto stream = createStream(path.string());
	REQUIRE(stream != nullptr);

	const auto head = asBytes("first");
	REQUIRE(stream->write(reinterpret_cast<const std::byte*>(head.data()), head.size()));

	stream->release();
	REQUIRE(stream->position() == std::uint64_t{5});

	const auto tail = asBytes("second");
	REQUIRE(stream->write(reinterpret_cast<const std::byte*>(tail.data()), tail.size()));
	REQUIRE(stream->finish());

	REQUIRE(contentsOf(path.string()) == asBytes("firstsecond"));
}

TEST_CASE("a write stream left unfinished still leaves its bytes on disk", "[stream]")
{
	// Not an endorsement — finish() is how a caller learns the close worked — but a
	// destructor that silently dropped buffered bytes would be a data-loss trap.
	const TempPath path;
	{
		auto stream = createStream(path.string());
		REQUIRE(stream != nullptr);
		const auto content = asBytes("unfinished");
		REQUIRE(stream->write(reinterpret_cast<const std::byte*>(content.data()), content.size()));
	}
	REQUIRE(contentsOf(path.string()) == asBytes("unfinished"));
}

TEST_CASE("a write stream past the end extends the resource", "[stream]")
{
	const TempPath path;

	auto stream = createStream(path.string());
	REQUIRE(stream != nullptr);
	REQUIRE(stream->seek(4) == std::uint64_t{4});

	const auto content = asBytes("end");
	REQUIRE(stream->write(reinterpret_cast<const std::byte*>(content.data()), content.size()));
	REQUIRE(stream->size() == std::uint64_t{7});
	REQUIRE(stream->finish());

	REQUIRE(contentsOf(path.string()).size() == std::size_t{7});
}

TEST_CASE("createStream refuses what it cannot create", "[stream]")
{
	const auto missing = std::filesystem::temp_directory_path() / "lain_iostream_no_such_dir" / "out.bin";
	REQUIRE(createStream(missing.string()) == nullptr);

	REQUIRE(createStream("http://example.com/clip.mp4") == nullptr);
	REQUIRE(createStream("s3://bucket/key") == nullptr);
}
