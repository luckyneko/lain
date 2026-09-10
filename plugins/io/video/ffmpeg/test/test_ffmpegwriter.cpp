// The FFmpeg-backed writer, driven end to end.
//
// Unlike the reader's tests these need NO checked-in fixture bytes: a write test can make its own
// material, and the round trip IS the assertion — encode frames, reopen the result through
// lain::io::video::open, compare. Everything goes through the seam (openWriter / open) and nothing
// constructs a reader or a writer directly, because reaching past the seam would not prove the
// plugin is REGISTERED, which is half of what a codec plugin has to get right.

#include "lain/io/video/ffmpeg/register.h"

#include <lain/io/video/open.h>
#include <lain/io/video/save.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
}

namespace fs = std::filesystem;

using lain::io::video::VideoCodec;
using lain::io::video::VideoWriterOptions;

namespace
{
	struct Registration
	{
		Registration() { lain::io::video::ffmpeg::registerCodec(); }
	};
	const Registration registration;

	constexpr int frameWidth = 64;
	constexpr int frameHeight = 48;
	constexpr std::size_t frameCount = 6;

	lain::media::FrameSpec spec(lain::image::ColorSpace space = lain::image::ColorSpace::BT709)
	{
		lain::media::FrameSpec s;
		s.extent = {frameWidth, frameHeight};
		s.pixelFormat = lain::image::PixelFormat::RGB8;
		s.colorSpace = space;
		s.alphaMode = lain::image::AlphaMode::Unspecified;
		s.rate = {24, 1};
		return s;
	}

	// A frame whose every pixel carries `ordinal` in a way that survives a lossy codec (a flat
	// colour over a large area) AND identifies the frame exactly for the lossless case.
	lain::image::Image frameOf(std::size_t ordinal, lain::image::ColorSpace space = lain::image::ColorSpace::BT709)
	{
		lain::image::Image image{frameWidth, frameHeight, lain::image::PixelFormat::RGB8, space};
		const auto n = static_cast<std::uint8_t>(ordinal);
		std::uint8_t* pixels = image.data();
		for (int y = 0; y < frameHeight; ++y)
		{
			for (int x = 0; x < frameWidth; ++x)
			{
				const std::size_t at = (static_cast<std::size_t>(y) * frameWidth + x) * 3;
				pixels[at + 0] = static_cast<std::uint8_t>(20 * n + 10);
				pixels[at + 1] = 128;
				pixels[at + 2] = static_cast<std::uint8_t>(250 - 20 * n);
			}
		}
		return image;
	}

	class TempOut
	{
	public:
		explicit TempOut(const std::string& extension)
		{
			m_path = lain::testing::scratchPath("vidwrite", "." + extension);
			std::error_code error;
			fs::remove(m_path, error);
		}

		~TempOut()
		{
			std::error_code error;
			fs::remove(m_path, error);
		}

		TempOut(const TempOut&) = delete;
		TempOut& operator=(const TempOut&) = delete;

		std::string string() const { return m_path.string(); }
		bool exists() const { return fs::exists(m_path); }

	private:
		fs::path m_path;
	};

	bool haveEncoderFor(VideoCodec codec, const std::string& extension)
	{
		TempOut probe{extension};
		VideoWriterOptions options;
		options.codec = codec;
		auto writer = lain::io::video::openWriter(probe.string(), spec(), options);
		if (!writer)
			return false;
		(void)writer->finish();
		return true;
	}

	// Are two images byte-for-byte the same picture?
	bool identical(const lain::image::Image& a, const lain::image::Image& b)
	{
		if (!a.valid() || !b.valid() || a.extent() != b.extent() || a.pixelFormat() != b.pixelFormat())
			return false;
		return std::memcmp(a.data(), b.data(), a.byteSize()) == 0;
	}

	// The lossy comparison the reader's tests already established: a flat colour survives encoding,
	// a marker pixel does not.
	bool looksLike(const lain::image::Image& image, std::size_t ordinal)
	{
		if (!image.valid())
			return false;
		const lain::image::Image expected = frameOf(ordinal);
		const std::size_t at = (static_cast<std::size_t>(frameHeight - 2) * frameWidth + 2) * 3;
		for (std::size_t channel = 0; channel < 3; ++channel)
		{
			const int got = image.data()[at + channel];
			const int want = expected.data()[at + channel];
			if (got < want - 14 || got > want + 14)
				return false;
		}
		return true;
	}
} // namespace

TEST_CASE("a lossless write round-trips byte for byte", "[video][ffmpeg]")
{
	// FFV1 in Matroska over an RGB pixel format is the ONE configuration in which exactness is
	// possible, which is why the family exists in the enum at all: an 8-bit RGB->YUV matrix is not
	// invertible, so FFV1 over yuv420p would round-trip the FILE and not the FRAMES.
	if (!haveEncoderFor(VideoCodec::FFV1, "mkv"))
		SKIP("this build has no ffv1 encoder");

	TempOut out{"mkv"};
	VideoWriterOptions options;
	options.codec = VideoCodec::FFV1;

	std::vector<lain::image::Image> written;
	{
		auto writer = lain::io::video::openWriter(out.string(), spec(), options);
		REQUIRE(writer != nullptr);
		CHECK(writer->codec() == "ffv1");

		for (std::size_t i = 0; i < frameCount; ++i)
		{
			written.push_back(frameOf(i));
			REQUIRE(writer->write(written.back()));
		}
		REQUIRE(writer->finish());
	}

	const auto sequence = lain::io::video::open(out.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->size() == frameCount);
	CHECK(sequence->spec().extent == spec().extent);
	CHECK(sequence->spec().rate == lain::media::FrameRate{24, 1});
	CHECK(sequence->spec().colorSpace == lain::image::ColorSpace::BT709);

	// No tolerance at all. This is what proves the pixel path — every conversion, the range
	// handling and the frame ordering — rather than merely that a file appeared.
	for (std::size_t i = 0; i < frameCount; ++i)
		CHECK(identical(sequence->image(i), written[i]));
}

TEST_CASE("a delivery write reopens with its frames in order", "[video][ffmpeg]")
{
	// Delivery encoding is the one platform-conditional part of this milestone (ADR-0019), so a
	// missing encoder SKIPs rather than fails — the same discipline the [gpu] tests use, and what
	// keeps the conditionality visible in the suite instead of hidden behind an #ifdef.
	if (!haveEncoderFor(VideoCodec::H264, "mp4"))
		SKIP("this build has no h264 delivery encoder");

	TempOut out{"mp4"};
	VideoWriterOptions options;
	options.codec = VideoCodec::H264;

	{
		auto writer = lain::io::video::openWriter(out.string(), spec(), options);
		REQUIRE(writer != nullptr);
		CHECK(writer->container() == "mp4");

		for (std::size_t i = 0; i < frameCount; ++i)
			REQUIRE(writer->write(frameOf(i)));
		REQUIRE(writer->finish());
	}

	// That this reopens AT ALL is the assertion, and it covers two things that are not obvious:
	// the muxer SEEKING BACK to patch the mdat size and append the moov (which is what slice 3's
	// positional, non-truncating WriteStream was specified for), and AV_CODEC_FLAG_GLOBAL_HEADER
	// putting the parameter sets in the container's extradata — without which the stream decodes
	// to nothing.
	const auto sequence = lain::io::video::open(out.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->size() == frameCount);
	for (std::size_t i = 0; i < frameCount; ++i)
		CHECK(looksLike(sequence->image(i), i));
}

TEST_CASE("the colour tag lain writes is the one it reads back", "[video][ffmpeg]")
{
	if (!haveEncoderFor(VideoCodec::FFV1, "mkv"))
		SKIP("this build has no ffv1 encoder");

	// sRGB rather than BT709, because BT709 is what an untagged file would come back as anyway —
	// only the other space can tell a written tag from a default.
	TempOut out{"mkv"};
	VideoWriterOptions options;
	options.codec = VideoCodec::FFV1;

	{
		auto writer = lain::io::video::openWriter(out.string(), spec(lain::image::ColorSpace::sRGB), options);
		REQUIRE(writer != nullptr);
		REQUIRE(writer->write(frameOf(0, lain::image::ColorSpace::sRGB)));
		REQUIRE(writer->finish());
	}

	const auto sequence = lain::io::video::open(out.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->spec().colorSpace == lain::image::ColorSpace::sRGB);
}

TEST_CASE("a container with no encoder in this build is refused, naming what is missing", "[video][ffmpeg]")
{
	// webm carries VP8/VP9/AV1, and an LGPL FFmpeg built --disable-autodetect has no encoder for
	// any of them. The extension stays CLAIMED for the video medium — that is a fact about the
	// name — and what is absent is the capability, which is the distinction ADR-0019 turns on.
	CHECK(lain::io::video::isVideoUri("out.webm"));

	TempOut out{"webm"};
	CHECK(lain::io::video::openWriter(out.string(), spec()) == nullptr);
}

TEST_CASE("an unavailable codec family is refused rather than substituted", "[video][ffmpeg]")
{
	// QuickTime will not carry FFV1 (avformat_query_codec says so; Matroska is FFV1's container),
	// while it carries h264 and ProRes perfectly well. So this asks for a family THIS container
	// cannot hold, in a container that has plenty of other options — which is exactly the shape
	// that would succeed if the writer ever substituted. A render that quietly became a different
	// codec is a wrong answer that looks like success.
	//
	// Note the assumption that did NOT hold, since it is the kind of thing to get wrong twice:
	// mp4 *does* carry FFV1 in a current FFmpeg (ISO/IEC 23001-17), so it is not a mismatch.
	TempOut out{"mov"};
	VideoWriterOptions options;
	options.codec = VideoCodec::FFV1;
	CHECK(lain::io::video::openWriter(out.string(), spec(), options) == nullptr);
}

TEST_CASE("a mismatched frame is refused, and what came before it survives", "[video][ffmpeg]")
{
	if (!haveEncoderFor(VideoCodec::FFV1, "mkv"))
		SKIP("this build has no ffv1 encoder");

	TempOut out{"mkv"};
	VideoWriterOptions options;
	options.codec = VideoCodec::FFV1;

	{
		auto writer = lain::io::video::openWriter(out.string(), spec(), options);
		REQUIRE(writer != nullptr);
		REQUIRE(writer->write(frameOf(0)));
		REQUIRE(writer->write(frameOf(1)));

		// Refused, never rescaled (ADR-0018) — a conversion chosen here, on a frame the caller did
		// not know would differ, is the silent lossy conversion the homogeneity rule prevents.
		lain::image::Image wrongSize{frameWidth * 2, frameHeight, lain::image::PixelFormat::RGB8,
									 lain::image::ColorSpace::BT709};
		CHECK_FALSE(writer->write(wrongSize));

		// finish() is CLOSE, not commit: it still writes the trailer after a failed frame, which is
		// what makes a truncated render a playable short file instead of a headless one.
		REQUIRE(writer->finish());
	}

	const auto sequence = lain::io::video::open(out.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->size() == 2);
	CHECK(identical(sequence->image(0), frameOf(0)));
	CHECK(identical(sequence->image(1), frameOf(1)));
}

TEST_CASE("finish is idempotent and a second call cannot corrupt the file", "[video][ffmpeg]")
{
	if (!haveEncoderFor(VideoCodec::FFV1, "mkv"))
		SKIP("this build has no ffv1 encoder");

	TempOut out{"mkv"};
	VideoWriterOptions options;
	options.codec = VideoCodec::FFV1;

	{
		auto writer = lain::io::video::openWriter(out.string(), spec(), options);
		REQUIRE(writer != nullptr);
		REQUIRE(writer->write(frameOf(0)));
		CHECK(writer->finish());
		CHECK(writer->finish()); // repeats the first answer; a second trailer would corrupt it
	}

	const auto sequence = lain::io::video::open(out.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->size() == 1);
}

TEST_CASE("save transcodes a whole sequence through the one-shot facade", "[video][ffmpeg]")
{
	if (!haveEncoderFor(VideoCodec::FFV1, "mkv"))
		SKIP("this build has no ffv1 encoder");

	TempOut source{"mkv"};
	VideoWriterOptions options;
	options.codec = VideoCodec::FFV1;
	{
		auto writer = lain::io::video::openWriter(source.string(), spec(), options);
		REQUIRE(writer != nullptr);
		for (std::size_t i = 0; i < frameCount; ++i)
			REQUIRE(writer->write(frameOf(i)));
		REQUIRE(writer->finish());
	}

	const auto opened = lain::io::video::open(source.string());
	REQUIRE(opened.has_value());

	// The transcode case the facade exists for: a sequence in, a container out, with the spec
	// taken from the sequence rather than from a frame.
	TempOut copy{"mkv"};
	REQUIRE(lain::io::video::save(copy.string(), *opened, options));

	const auto reopened = lain::io::video::open(copy.string());
	REQUIRE(reopened.has_value());
	CHECK(reopened->size() == frameCount);
	for (std::size_t i = 0; i < frameCount; ++i)
		CHECK(identical(reopened->image(i), frameOf(i)));
}
