// The FFmpeg reader, driven through the production seam over real files (M10 slice 5b).
//
// The fixtures are embedded bytes written to a temp file, because io::video::open takes a URI and
// opens the transport itself — which is the path under test. Nothing here constructs an
// FFmpegVideoReader directly: a test that reached past io::video::open would not prove the plugin
// is REGISTERED, which is half of what a plugin has to get right.

#include "videofixtures.h"

#include <lain/io/video/ffmpeg/register.h>
#include <lain/io/video/open.h>
#include <lain/media/operations.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	// A fixture written to a real file, removed on destruction.
	class Fixture
	{
	public:
		Fixture(const unsigned char* bytes, std::size_t size, const std::string& extension = "mp4")
		{
			m_path = lain::testing::scratchPath("video-fixture", "." + extension);
			std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
			out.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
		}

		~Fixture()
		{
			std::error_code error;
			fs::remove(m_path, error);
		}

		Fixture(const Fixture&) = delete;
		Fixture& operator=(const Fixture&) = delete;

		std::string string() const { return m_path.string(); }

	private:
		fs::path m_path;
	};

	struct Registration
	{
		Registration() { lain::io::video::ffmpeg::registerCodec(); }
	};
	const Registration registration;

	// The colour a frame is flat in, read away from the moving white block. The fixtures are lossy,
	// so a frame's identity has to be read from a large uniform area rather than one marker pixel.
	struct Rgb
	{
		int r = 0;
		int g = 0;
		int b = 0;
	};

	Rgb flatColorOf(const lain::image::Image& image)
	{
		// Bottom-left, which the white block never visits (it stays on the middle rows).
		const std::size_t offset = static_cast<std::size_t>(image.width()) * 3 * (static_cast<std::size_t>(image.height()) - 2) + 3;
		return Rgb{image.data()[offset], image.data()[offset + 1], image.data()[offset + 2]};
	}

	// The generator painted frame n as (20n + 10, 128, 250 - 20n); lossy coding moves that a little.
	bool looksLikeFrame(const lain::image::Image& image, int ordinal)
	{
		const Rgb color = flatColorOf(image);
		return std::abs(color.r - (20 * ordinal + 10)) <= 12 && std::abs(color.b - (250 - 20 * ordinal)) <= 12;
	}
} // namespace

TEST_CASE("an h264 clip opens with an exact frame count and spec", "[video][ffmpeg]")
{
	const Fixture file{kClipH264_64x48_12, sizeof(kClipH264_64x48_12)};
	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());

	// EXACT, because the table is built by scanning every packet rather than estimating frames
	// from a duration — the property "frame 412 means frame 412 on every platform" rests on.
	CHECK(sequence->size() == 12);
	CHECK(sequence->spec().extent.x == 64);
	CHECK(sequence->spec().extent.y == 48);
	CHECK(sequence->spec().pixelFormat == lain::image::PixelFormat::RGB8);
	// Untagged footage: BT709 with a log line, never a guess at BT.601 by frame size (ADR-0018).
	CHECK(sequence->spec().colorSpace == lain::image::ColorSpace::BT709);

	// The exact rational the container states, not 24.0 arrived at through a double.
	CHECK(sequence->spec().rate.numerator == 24);
	CHECK(sequence->spec().rate.denominator == 1);
}

TEST_CASE("frames decode in order and are the frames they claim to be", "[video][ffmpeg]")
{
	const Fixture file{kClipH264_64x48_12, sizeof(kClipH264_64x48_12)};
	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());

	for (std::size_t i = 0; i < sequence->size(); ++i)
	{
		const lain::image::Image frame = sequence->image(i);
		REQUIRE(frame.valid());
		CHECK(frame.width() == 64);
		CHECK(looksLikeFrame(frame, static_cast<int>(i)));
	}
}

TEST_CASE("a seeked frame decodes to the same bytes as a sequential one", "[video][ffmpeg]")
{
	// The seek path's entire correctness claim, and it has to be reached by going BACKWARDS. A
	// fresh sequence asked for frame 7 decodes forward from the start and gets there without ever
	// seeking, so a test written that way passes with seeking removed entirely — which is what an
	// early version of this one did.
	const Fixture file{kClipH264_64x48_12, sizeof(kClipH264_64x48_12)};

	std::vector<std::uint8_t> sequential;
	{
		const auto sequence = lain::io::video::open(file.string());
		REQUIRE(sequence.has_value());
		for (std::size_t i = 0; i <= 7; ++i)
		{
			const lain::image::Image frame = sequence->image(i);
			REQUIRE(frame.valid());
			if (i == 7)
				sequential.assign(frame.data(), frame.data() + frame.byteSize());
		}
	}

	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());
	REQUIRE(sequence->image(11).valid()); // past it, so reaching 7 must seek back to a keyframe

	// The ring holds four frames, so 11 evicted 7 if it was ever there: this is a real decode.
	const lain::image::Image jumped = sequence->image(7);
	REQUIRE(jumped.valid());
	REQUIRE(jumped.byteSize() == sequential.size());
	// Byte-identical, not merely similar. A seek that landed on the wrong keyframe, or forgot to
	// flush the decoder, produces a plausible image that differs — which no eyeball would catch.
	CHECK(std::memcmp(jumped.data(), sequential.data(), sequential.size()) == 0);
}

TEST_CASE("seeking backwards returns the earlier frame, not the later one", "[video][ffmpeg]")
{
	const Fixture file{kClipH264_64x48_12, sizeof(kClipH264_64x48_12)};
	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());

	REQUIRE(sequence->image(9).valid());
	const lain::image::Image back = sequence->image(2);
	REQUIRE(back.valid());
	// Decoding forward from a keyframe hands back frames BEFORE the target first; matching by pts
	// rather than by counting is what makes this the frame that was asked for.
	CHECK(looksLikeFrame(back, 2));
}

TEST_CASE("B-frames: the table is display order, not demux order", "[video][ffmpeg]")
{
	// This fixture's packets arrive pts 0, 1536, 512, 1024, 3072, ... A table left in arrival order
	// would answer image(1) with the frame stored second — a wrong IMAGE rather than an error,
	// which is exactly the failure a stable sort by pts exists to prevent.
	const Fixture file{kBFramesMpeg4_64x48_12, sizeof(kBFramesMpeg4_64x48_12)};
	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());
	REQUIRE(sequence->size() == 12);

	for (std::size_t i = 0; i < sequence->size(); ++i)
	{
		const lain::image::Image frame = sequence->image(i);
		REQUIRE(frame.valid());
		CHECK(looksLikeFrame(frame, static_cast<int>(i)));
	}
}

TEST_CASE("timestamps come from the container and rise with the frames", "[video][ffmpeg]")
{
	const Fixture file{kClipH264_64x48_12, sizeof(kClipH264_64x48_12)};
	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());

	using Catch::Matchers::WithinAbs;
	CHECK_THAT(sequence->frame(0).timestamp.seconds(), WithinAbs(0.0, 1e-6));
	CHECK_THAT(sequence->frame(6).timestamp.seconds(), WithinAbs(6.0 / 24.0, 1e-3));

	for (std::size_t i = 1; i < sequence->size(); ++i)
		CHECK(sequence->frame(i).timestamp.seconds() > sequence->frame(i - 1).timestamp.seconds());
}

TEST_CASE("explicitly tagged PQ / BT.2020 footage is refused, not relabelled", "[video][ffmpeg]")
{
	// ADR-0018's rule reaching a real file: lain would have to convert to process this, and a
	// conversion nobody asked for is the silent wrong answer the whole colour axis exists to stop.
	const Fixture file{kPqTagged_64x48_1, sizeof(kPqTagged_64x48_1)};
	CHECK_FALSE(lain::io::video::open(file.string()).has_value());
}

TEST_CASE("a truncated container fails to open rather than crashing", "[video][ffmpeg]")
{
	// Half a real file, so the header parses and the packets do not — the shape that finds an
	// unchecked return value, where random bytes would be rejected immediately.
	const Fixture truncated{kClipH264_64x48_12, sizeof(kClipH264_64x48_12) / 2};
	CHECK_FALSE(lain::io::video::open(truncated.string()).has_value());

	static const unsigned char garbage[] = {'n', 'o', 't', ' ', 'a', ' ', 'v', 'i', 'd', 'e', 'o'};
	const Fixture nonsense{garbage, sizeof(garbage)};
	CHECK_FALSE(lain::io::video::open(nonsense.string()).has_value());
}

TEST_CASE("a clip is a sequence like any other: clip re-bases position, keeps identity", "[video][ffmpeg]")
{
	// The medium disappearing is the point of the whole model — media::clip has no idea a decoder
	// is involved, and a clipped video frame reports the ordinal it has in its SOURCE.
	const Fixture file{kClipH264_64x48_12, sizeof(kClipH264_64x48_12)};
	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());

	const lain::media::FrameSequence clipped = lain::media::clip(*sequence, 6, 3);
	REQUIRE(clipped.size() == 3);
	CHECK(clipped.frame(0).ordinal == 6);
	CHECK(clipped.frame(0).source == sequence->frame(6).source);

	const lain::image::Image frame = clipped.image(0);
	REQUIRE(frame.valid());
	CHECK(looksLikeFrame(frame, 6));
}
