// Unit tests for the lain::io::video WRITE seam — the registry, the spec refusals, openWriter's
// dispatch, and the one-shot save(uri, sequence) transcode facade.
//
// Same discipline as test_open.cpp: a FAKE writer registered into the production registry and REAL
// temp files, so the production path runs with no codec and no FFmpeg. The fake writes a readable
// line per frame through the WriteStream it was handed, so a test can say which frames arrived, in
// what order, and whether the file was finished.

#include "lain/io/video/open.h"
#include "lain/io/video/save.h"

#include <lain/media/framesource.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

using lain::io::video::VideoWriter;
using lain::io::video::VideoWriterOptions;
using lain::io::video::writerRegistry;

namespace
{
	lain::media::FrameSpec writableSpec()
	{
		lain::media::FrameSpec spec;
		spec.extent = {4, 2};
		spec.pixelFormat = lain::image::PixelFormat::RGB8;
		spec.colorSpace = lain::image::ColorSpace::BT709;
		spec.alphaMode = lain::image::AlphaMode::Unspecified;
		spec.rate = {24, 1};
		return spec;
	}

	// An image matching writableSpec(), with `tag` in its first byte so a written frame is
	// identifiable in the fake's output.
	lain::image::Image taggedFrame(std::uint8_t tag)
	{
		lain::image::Image frame{4, 2, lain::image::PixelFormat::RGB8, lain::image::ColorSpace::BT709};
		frame.data()[0] = tag;
		return frame;
	}

	// A writer that encodes nothing: it writes one text line per frame through the transport, so
	// the bytes on disk are checkable. It still TAKES the WriteStream, because "the seam creates
	// the transport and hands it over" is one of the things under test — a writer that never
	// received one would not notice if the seam stopped creating it.
	class FakeWriter : public VideoWriter
	{
	public:
		bool open(std::unique_ptr<lain::io::WriteStream> stream, std::string_view format,
				  const lain::media::FrameSpec& spec, const VideoWriterOptions& options) override
		{
			if (!stream)
				return false;

			// One option value decides whether this writer accepts the request, so a test can make
			// openWriter refuse without needing a second registered backend — the fake's peer of
			// FakeReader's refuse tag.
			if (options.codec == refusedCodec)
				return false;

			m_stream = std::move(stream);
			m_spec = spec;
			m_container = std::string{format};
			m_codec = "fakecodec";

			lastSpec = spec;
			lastOptions = options;
			lastContainer = m_container;

			return line("open " + m_container);
		}

		const std::string& container() const override { return m_container; }
		const std::string& codec() const override { return m_codec; }

		bool write(const lain::image::Image& image) override
		{
			// The spec check every writer owes its caller (ADR-0018: refused, never rescaled or
			// relabelled). media::matches is the one place the homogeneity question is asked, so
			// the fake asks it too rather than approximating it.
			if (!lain::media::matches(m_spec, image))
				return false;

			return line("frame " + std::to_string(static_cast<int>(image.data()[0])));
		}

		bool finish() override
		{
			if (m_finished)
				return m_status;

			m_finished = true;
			++finishes;
			m_status = line("end") && m_stream->finish();
			return m_status;
		}

		static constexpr auto refusedCodec = lain::io::video::VideoCodec::MJPEG;

		// What the last open() was told, so a test can assert on the spec the SEAM passed through
		// rather than on the one it constructed.
		static inline lain::media::FrameSpec lastSpec{};
		static inline VideoWriterOptions lastOptions{};
		static inline std::string lastContainer{};
		static inline int finishes = 0;

	private:
		bool line(const std::string& text)
		{
			const std::string out = text + "\n";
			return m_stream->write(reinterpret_cast<const std::byte*>(out.data()), out.size());
		}

		std::unique_ptr<lain::io::WriteStream> m_stream;
		lain::media::FrameSpec m_spec;
		std::string m_container;
		std::string m_codec;
		bool m_finished = false;
		bool m_status = false;
	};

	// A source of `count` frames whose first byte is the ordinal; ordinal `hole` decodes invalid,
	// which is how a transcode meets a frame it cannot write.
	class FakeSource : public lain::media::FrameSource
	{
	public:
		FakeSource(std::string uri, std::size_t count, std::size_t hole = npos)
			: FrameSource(std::move(uri), writableSpec(), count)
			, m_hole(hole)
		{
		}

		static constexpr std::size_t npos = static_cast<std::size_t>(-1);

	protected:
		lain::image::Image decodeFrame(std::size_t ordinal) const override
		{
			if (ordinal == m_hole)
				return {};
			return taggedFrame(static_cast<std::uint8_t>(ordinal));
		}

	private:
		std::size_t m_hole;
	};

	// A uniquely-named temp path, removed on destruction. Unlike the reader tests' TempFile it
	// creates NOTHING: the whole point of an output path is that the seam creates it.
	class TempOut
	{
	public:
		explicit TempOut(const std::string& extension = "mp4")
		{
			m_path = lain::testing::scratchPath("videowrite", "." + extension);
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

		std::string contents() const
		{
			std::ifstream in(m_path, std::ios::binary);
			std::ostringstream text;
			text << in.rdbuf();
			return text.str();
		}

	private:
		fs::path m_path;
	};

	struct Registration
	{
		Registration() { writerRegistry().registerType<FakeWriter>("fake"); }
	};
	const Registration registration;

	lain::media::FrameSequence sequenceOf(std::size_t count, std::size_t hole = FakeSource::npos)
	{
		return lain::media::FrameSequence::over(std::make_shared<FakeSource>("fake://source", count, hole));
	}
} // namespace

TEST_CASE("openWriter creates the transport and hands it to a backend", "[io::video]")
{
	TempOut out;

	auto writer = lain::io::video::openWriter(out.string(), writableSpec());
	REQUIRE(writer != nullptr);

	// The container the backend was told is the EXTENSION KEY, not the path — a claim about the
	// format, never a name it could open (ADR-0004).
	CHECK(FakeWriter::lastContainer == "mp4");
	CHECK(writer->codec() == "fakecodec");

	// The file exists before a single frame: the transport is created at open, which is what makes
	// openWriter the preflight.
	CHECK(out.exists());

	CHECK(writer->write(taggedFrame(7)));
	CHECK(writer->finish());
	CHECK(out.contents() == "open mp4\nframe 7\nend\n");
}

TEST_CASE("the extension key is case-insensitive and decides the medium", "[io::video]")
{
	TempOut upper{"MP4"};
	CHECK(lain::io::video::openWriter(upper.string(), writableSpec()) != nullptr);
	CHECK(FakeWriter::lastContainer == "mp4");

	// A still format is not this seam's business. It is refused on the NAME, before a spec, a
	// registry or a transport is consulted.
	TempOut still{"png"};
	CHECK(lain::io::video::openWriter(still.string(), writableSpec()) == nullptr);
	CHECK_FALSE(still.exists());
}

TEST_CASE("a spec a container cannot state is refused before anything is created", "[io::video]")
{
	// Alpha: no video codec holds it, so the refusal is the medium's and lives in the seam.
	lain::media::FrameSpec alpha = writableSpec();
	alpha.pixelFormat = lain::image::PixelFormat::RGBA8;
	CHECK_FALSE(lain::io::video::canEncode(alpha));
	CHECK(lain::io::video::refusedSpecReason(alpha).find("alpha") != std::string::npos);

	// Linear: it would ENCODE fine, and come back tagged BT709 with every value off by the gamma.
	// That silence is the reason it is refused rather than tolerated.
	lain::media::FrameSpec linear = writableSpec();
	linear.colorSpace = lain::image::ColorSpace::Linear;
	CHECK_FALSE(lain::io::video::canEncode(linear));

	// Unspecified: lain is the AUTHOR here, and writing a guess into a file that outlives the guess
	// is not the same as tolerating an untagged file on the way in.
	lain::media::FrameSpec untagged = writableSpec();
	untagged.colorSpace = lain::image::ColorSpace::Unspecified;
	CHECK_FALSE(lain::io::video::canEncode(untagged));

	// Bit depth: the codec boundary is 8-bit, as the reader's is, and no delivery codec here takes
	// anything wider.
	lain::media::FrameSpec deep = writableSpec();
	deep.pixelFormat = lain::image::PixelFormat::RGB16;
	CHECK_FALSE(lain::io::video::canEncode(deep));
	CHECK(lain::io::video::refusedSpecReason(deep).find("8-bit") != std::string::npos);

	// Gray8 is ACCEPTED, which is the other half of that rule: widening gray to a colour stream
	// loses nothing, and the contract is "no LOSS", not "no conversion".
	lain::media::FrameSpec gray = writableSpec();
	gray.pixelFormat = lain::image::PixelFormat::Gray8;
	CHECK(lain::io::video::canEncode(gray));

	// No rate: a container must state a timebase and there is none to invent.
	lain::media::FrameSpec rateless = writableSpec();
	rateless.rate = {};
	CHECK_FALSE(lain::io::video::canEncode(rateless));
	CHECK(lain::io::video::refusedSpecReason(rateless).find("rate") != std::string::npos);

	// Both writable spaces pass, and the accepted spec has no reason to report.
	CHECK(lain::io::video::canEncode(writableSpec()));
	CHECK(lain::io::video::refusedSpecReason(writableSpec()).empty());
	lain::media::FrameSpec srgb = writableSpec();
	srgb.colorSpace = lain::image::ColorSpace::sRGB;
	CHECK(lain::io::video::canEncode(srgb));

	TempOut out;
	CHECK(lain::io::video::openWriter(out.string(), alpha) == nullptr);
	CHECK_FALSE(out.exists()); // refused before the transport, so nothing was truncated
}

TEST_CASE("a frame that does not match the spec is refused, not rescaled", "[io::video]")
{
	TempOut out;
	auto writer = lain::io::video::openWriter(out.string(), writableSpec());
	REQUIRE(writer != nullptr);

	CHECK(writer->write(taggedFrame(1)));

	lain::image::Image wrongSize{8, 2, lain::image::PixelFormat::RGB8, lain::image::ColorSpace::BT709};
	CHECK_FALSE(writer->write(wrongSize));

	lain::image::Image wrongSpace{4, 2, lain::image::PixelFormat::RGB8, lain::image::ColorSpace::sRGB};
	CHECK_FALSE(writer->write(wrongSpace));

	// A refusal is not corruption: what was written before it is still there after finishing.
	CHECK(writer->finish());
	CHECK(out.contents() == "open mp4\nframe 1\nend\n");
}

TEST_CASE("finish is idempotent and repeats its first answer", "[io::video]")
{
	TempOut out;
	auto writer = lain::io::video::openWriter(out.string(), writableSpec());
	REQUIRE(writer != nullptr);

	const int before = FakeWriter::finishes;
	CHECK(writer->finish());
	CHECK(writer->finish());
	// Twice called, once done — a second trailer would corrupt the file it is meant to close.
	CHECK(FakeWriter::finishes == before + 1);
}

TEST_CASE("openWriter refuses when no backend can serve the request", "[io::video]")
{
	TempOut out;
	VideoWriterOptions options;
	options.codec = FakeWriter::refusedCodec;

	// Never a fallback to a family that happens to be available: a delivery render that quietly
	// became something else is a wrong answer that looks like success.
	CHECK(lain::io::video::openWriter(out.string(), writableSpec(), options) == nullptr);
}

TEST_CASE("save writes every frame of a sequence, in order, and finishes", "[io::video]")
{
	TempOut out;
	REQUIRE(lain::io::video::save(out.string(), sequenceOf(3)));

	// The order is the assertion: a transcode that reordered frames would still produce a file.
	CHECK(out.contents() == "open mp4\nframe 0\nframe 1\nframe 2\nend\n");

	// The spec came from the SEQUENCE, not from a frame — which is what lets the encoder open
	// before anything has been decoded.
	CHECK(FakeWriter::lastSpec.rate == lain::media::FrameRate{24, 1});
	CHECK(FakeWriter::lastSpec.extent.x == 4);
}

TEST_CASE("save refuses an empty sequence rather than writing an empty container", "[io::video]")
{
	TempOut out;
	CHECK_FALSE(lain::io::video::save(out.string(), lain::media::FrameSequence{}));
	// Nothing was opened, so nothing was created: a zero-frame file would hide the refusal.
	CHECK_FALSE(out.exists());
}

TEST_CASE("save closes over what exists when a frame cannot be decoded", "[io::video]")
{
	TempOut out;
	const int before = FakeWriter::finishes;

	// A hole. A video cannot represent one, and a transcode has no host policy to consult.
	CHECK_FALSE(lain::io::video::save(out.string(), sequenceOf(4, 2)));

	// The failure is reported AND the file is finished — a short, closed file rather than a
	// headless one, which is the same discipline the render loop's single exit enforces.
	CHECK(FakeWriter::finishes == before + 1);
	CHECK(out.contents() == "open mp4\nframe 0\nframe 1\nend\n");
}

TEST_CASE("isVideoUri answers about the name, and only about the name", "[io::video]")
{
	CHECK(lain::io::video::isVideoUri("clip.mp4"));
	CHECK(lain::io::video::isVideoUri("/footage/CLIP.MP4"));
	CHECK(lain::io::video::isVideoUri("take1.mkv"));

	// The still medium is addressed structurally, not by name — a folder has no extension, and a
	// numbered pattern's extension names the still format rather than the sequence's.
	CHECK_FALSE(lain::io::video::isVideoUri("shot.####.png"));
	CHECK_FALSE(lain::io::video::isVideoUri("/footage/take1"));
	CHECK_FALSE(lain::io::video::isVideoUri(""));
}
