// Unit tests for io::image::openSequence — the image medium's frame-sequence opener (M10).
//
// Same discipline as test_load.cpp: a FAKE reader registered into the production registry, and
// REAL temp files, so the production dispatch path is exercised with no codec and no driver.
// The fake decodes a fixed-size image whose first pixel carries the file's first byte, so a test
// can say which frame it got back; two sentinel bytes let it produce an off-spec frame or fail.

#include "lain/io/image/load.h"
#include "lain/io/image/reader.h"
#include "lain/io/image/sequence.h"

#include <lain/io/uri.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using lain::io::image::ImageReader;
using lain::io::image::openSequence;
using lain::io::image::readerRegistry;

namespace fs = std::filesystem;

namespace
{
	constexpr std::uint8_t offSpecTag = 0xFE; // decodes at the wrong size
	constexpr std::uint8_t failTag = 0xFF;	  // fails to decode

	// Keyed to its own extension so it cannot collide with the fake reader test_load.cpp
	// registers into the same process-wide registry.
	constexpr const char* extension = "seqfake";

	class SequenceReader : public ImageReader
	{
	public:
		lain::image::Image decode(const lain::memory::Buffer& bytes) const override
		{
			if (bytes.empty())
				return {};

			const std::uint8_t tag = static_cast<std::uint8_t>(*bytes.data());
			if (tag == failTag)
				return {};

			const int width = tag == offSpecTag ? 8 : 4;
			lain::image::Image image{width, 1, lain::image::PixelFormat::RGBA8};
			image.data()[0] = tag;
			return image;
		}
	};

	std::uint8_t tagOf(const lain::image::Image& image) { return image.valid() ? image.data()[0] : 0; }

	// A uniquely-named temp directory, removed on destruction.
	class TempDir
	{
	public:
		TempDir()
		{
			static int counter = 0;
			m_path = fs::temp_directory_path() / ("lain_seq_" + std::to_string(counter++));
			fs::remove_all(m_path);
			fs::create_directories(m_path);
		}

		~TempDir()
		{
			std::error_code error;
			fs::remove_all(m_path, error);
		}

		TempDir(const TempDir&) = delete;
		TempDir& operator=(const TempDir&) = delete;

		// Write a one-byte file: the byte is the frame's tag.
		void write(const std::string& name, std::uint8_t tag) const
		{
			std::ofstream out(m_path / name, std::ios::binary | std::ios::trunc);
			const char byte = static_cast<char>(tag);
			out.write(&byte, 1);
		}

		const fs::path& path() const { return m_path; }
		std::string string() const { return m_path.string(); }

	private:
		fs::path m_path;
	};

	// Registered once for the whole executable, like the other io::image test file does.
	struct Registration
	{
		Registration() { readerRegistry().registerType<SequenceReader>(extension); }
	};
	const Registration registration;
} // namespace

TEST_CASE("a directory opens as a sequence of its readable images", "[io::image][sequence]")
{
	TempDir dir;
	dir.write(std::string("a.") + extension, 10);
	dir.write(std::string("b.") + extension, 20);
	dir.write(std::string("c.") + extension, 30);
	dir.write("notes.txt", 99); // no reader for .txt, so not a frame

	const std::optional<lain::media::FrameSequence> sequence = openSequence(dir.string());
	REQUIRE(sequence.has_value());
	REQUIRE(sequence->size() == 3);

	// Sorted by filename, and the pixels prove which file each position came from.
	CHECK(tagOf(sequence->image(0)) == 10);
	CHECK(tagOf(sequence->image(1)) == 20);
	CHECK(tagOf(sequence->image(2)) == 30);

	// The spec comes from the first frame and describes them all.
	CHECK(sequence->spec().extent == lain::math::Vec2i{4, 1});
	CHECK_FALSE(sequence->spec().rate.specified()); // stills declare no rate
}

TEST_CASE("a missing directory fails, an empty one is a value", "[io::image][sequence]")
{
	// The distinction flow's ListDir already draws, arriving from the other end: absent is a
	// failure, empty is zero frames.
	CHECK_FALSE(openSequence((fs::temp_directory_path() / "lain_seq_does_not_exist").string()).has_value());

	TempDir empty;
	const std::optional<lain::media::FrameSequence> sequence = openSequence(empty.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->empty());
}

TEST_CASE("a directory of nothing readable is empty, not a failure", "[io::image][sequence]")
{
	TempDir dir;
	dir.write("notes.txt", 1);
	dir.write("data.bin", 2);

	const std::optional<lain::media::FrameSequence> sequence = openSequence(dir.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->empty());
}

TEST_CASE("a #### pattern orders numerically, not lexicographically", "[io::image][sequence]")
{
	TempDir dir;
	dir.write(std::string("shot.2.") + extension, 2);
	dir.write(std::string("shot.10.") + extension, 10);
	dir.write(std::string("shot.0001.") + extension, 1);
	dir.write(std::string("other.3.") + extension, 3); // wrong prefix
	dir.write(std::string("shot.x.") + extension, 4);  // not a number

	const std::optional<lain::media::FrameSequence> sequence =
		openSequence((dir.path() / (std::string("shot.####.") + extension)).string());
	REQUIRE(sequence.has_value());
	REQUIRE(sequence->size() == 3);

	// 1, 2, 10 — the case a lexicographic directory listing gets wrong, and the reason patterns
	// exist. Padding is a writing convention, not a read filter: "shot.2" and "shot.0001" both
	// matched a four-hash run.
	CHECK(tagOf(sequence->image(0)) == 1);
	CHECK(tagOf(sequence->image(1)) == 2);
	CHECK(tagOf(sequence->image(2)) == 10);
}

TEST_CASE("a sequence names its source canonically", "[io::image][sequence]")
{
	TempDir dir;
	dir.write(std::string("a.") + extension, 1);

	// One resource, one name however spelled — what makes two FrameRefs to one frame compare
	// equal, and a saved manifest mean something.
	const std::string awkward = (dir.path() / "." / ".." / dir.path().filename()).string();
	const std::optional<lain::media::FrameSequence> sequence = openSequence(awkward);
	REQUIRE(sequence.has_value());
	REQUIRE(sequence->size() == 1);

	CHECK(sequence->frame(0).source == lain::io::canonicalUri(dir.string()));
	CHECK(sequence->frame(0).ordinal == 0);
}

TEST_CASE("a sequence declares the rate it is given", "[io::image][sequence]")
{
	TempDir dir;
	dir.write(std::string("a.") + extension, 1);
	dir.write(std::string("b.") + extension, 2);

	const std::optional<lain::media::FrameSequence> sequence =
		openSequence(dir.string(), lain::media::FrameRate{24, 1});
	REQUIRE(sequence.has_value());
	CHECK(sequence->spec().rate == lain::media::FrameRate{24, 1});

	// With a rate declared, the base class derives timestamps from it. Compared with a
	// tolerance because core::Time stores exact NANOSECONDS: 1/24 s is 41666666.67 ns, so it
	// round-trips to within a nanosecond rather than exactly. That is far below anything a
	// frame timestamp is used for, and it is why the rate itself stays rational — the exact
	// value lives in the rate, not in a time derived from it.
	CHECK_THAT(sequence->frame(1).timestamp.seconds(), Catch::Matchers::WithinAbs(1.0 / 24.0, 1e-9));
}

TEST_CASE("an undecodable first frame is a failure, a later one is not", "[io::image][sequence]")
{
	SECTION("the first frame establishes the spec, so it must decode")
	{
		TempDir dir;
		dir.write(std::string("a.") + extension, failTag);
		dir.write(std::string("b.") + extension, 2);

		CHECK_FALSE(openSequence(dir.string()).has_value());
	}

	SECTION("a later failure costs only its own frame")
	{
		TempDir dir;
		dir.write(std::string("a.") + extension, 1);
		dir.write(std::string("b.") + extension, failTag);
		dir.write(std::string("c.") + extension, 3);

		const std::optional<lain::media::FrameSequence> sequence = openSequence(dir.string());
		REQUIRE(sequence.has_value());
		REQUIRE(sequence->size() == 3);

		CHECK(tagOf(sequence->image(0)) == 1);
		CHECK_FALSE(sequence->image(1).valid()); // blocks, then yields an invalid Image
		CHECK(tagOf(sequence->image(2)) == 3);
	}

	SECTION("a frame of the wrong shape is refused, not delivered")
	{
		TempDir dir;
		dir.write(std::string("a.") + extension, 1);
		dir.write(std::string("b.") + extension, offSpecTag);

		const std::optional<lain::media::FrameSequence> sequence = openSequence(dir.string());
		REQUIRE(sequence.has_value());
		REQUIRE(sequence->size() == 2);

		// Homogeneity is a guarantee the sequence keeps, not merely one it declares: a consumer
		// handed this 8-wide frame could only cope by converting, on a frame it did not know
		// would differ.
		CHECK(sequence->image(0).valid());
		CHECK_FALSE(sequence->image(1).valid());
	}
}

TEST_CASE("a uri that is neither a directory nor a pattern is refused", "[io::image][sequence]")
{
	TempDir dir;
	dir.write(std::string("a.") + extension, 1);

	// A single still is a load(), not a sequence — saying so beats quietly returning one frame.
	CHECK_FALSE(openSequence((dir.path() / (std::string("a.") + extension)).string()).has_value());
}
