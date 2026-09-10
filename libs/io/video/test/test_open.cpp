// Unit tests for the lain::io::video seam — the registry, the capability report, and the
// VideoSource that turns a VideoReader into a media::FrameSource.
//
// Same discipline as libs/io/image's tests: a FAKE reader registered into the production registry
// and REAL temp files, so the production dispatch path runs with no codec and no driver. The fake
// decodes a frame whose first pixel carries the ordinal, so a test can say which frame it got.

#include "lain/io/video/open.h"
#include "lain/io/video/reader.h"

#include <lain/io/uri.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

using lain::io::video::readerRegistry;
using lain::io::video::VideoReader;

namespace
{
	constexpr std::size_t fakeFrameCount = 6;

	// A reader that decodes nothing: it reports a fixed spec and paints the ordinal into the first
	// pixel. It still takes the Stream, because "the seam opens the transport and hands it over"
	// is one of the things under test — a reader that never received one would not notice if the
	// seam stopped opening it.
	class FakeReader : public VideoReader
	{
	public:
		bool open(std::unique_ptr<lain::io::ReadStream> stream, lain::media::FrameRate fallbackRate) override
		{
			if (!stream)
				return false;
			m_stream = std::move(stream);

			// One byte decides whether this reader accepts the container, so a test can present a
			// file it must refuse without needing a second registered reader.
			std::byte tag{};
			const auto read = m_stream->read(&tag, 1);
			if (!read || *read != 1 || std::to_integer<std::uint8_t>(tag) == refuseTag)
				return false;

			m_spec.extent = {4, 2};
			m_spec.pixelFormat = lain::image::PixelFormat::RGB8;
			m_spec.colorSpace = lain::image::ColorSpace::BT709;
			m_spec.alphaMode = lain::image::AlphaMode::Unspecified;
			m_spec.rate = fallbackRate.specified() ? fallbackRate : lain::media::FrameRate{24, 1};
			return true;
		}

		const lain::media::FrameSpec& spec() const override { return m_spec; }
		std::size_t frameCount() const override { return fakeFrameCount; }

		const std::string& container() const override { return m_container; }
		const std::string& codec() const override { return m_codec; }

		lain::core::Time timestamp(std::size_t ordinal) const override
		{
			// Deliberately NOT rate x ordinal: the base class's default is that, so a test can only
			// tell the override is reached if the container reports something else. This is the
			// variable-frame-rate shape in miniature.
			return lain::core::Time::from<lain::core::Time::Milliseconds>(static_cast<double>(ordinal) * 100.0 + 7.0);
		}

		lain::image::Image decode(std::size_t ordinal) override
		{
			++decodes;
			lain::image::Image frame{4, 2, lain::image::PixelFormat::RGB8, lain::image::ColorSpace::BT709};
			frame.data()[0] = static_cast<std::uint8_t>(ordinal);
			return frame;
		}

		static constexpr std::uint8_t refuseTag = 0xFF;
		static inline int decodes = 0;

	private:
		std::unique_ptr<lain::io::ReadStream> m_stream;
		lain::media::FrameSpec m_spec;
		// Two facts about one file, which is the whole reason they are reported rather than
		// dispatched on: neither is derivable from the other, and neither is in the file's name.
		std::string m_container{"fakecontainer"};
		std::string m_codec{"fakecodec"};
	};

	// A uniquely-named temp file holding one byte, removed on destruction.
	class TempFile
	{
	public:
		explicit TempFile(std::uint8_t tag, const std::string& extension = "mp4")
		{
			m_path = lain::testing::scratchPath("video", "." + extension);
			std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
			const char byte = static_cast<char>(tag);
			out.write(&byte, 1);
		}

		~TempFile()
		{
			std::error_code error;
			fs::remove(m_path, error);
		}

		TempFile(const TempFile&) = delete;
		TempFile& operator=(const TempFile&) = delete;

		std::string string() const { return m_path.string(); }

	private:
		fs::path m_path;
	};

	// Registered once for the whole executable, the way the io::image tests do it. The EMPTY
	// registry is the one state that cannot be restored — a core::Factory only grows — so the case
	// that needs it lives in its own executable (test_nocodec.cpp) rather than depending on the
	// order Catch2 happens to run these in.
	struct Registration
	{
		Registration() { readerRegistry().registerType<FakeReader>("fake"); }
	};
	const Registration registration;
} // namespace

TEST_CASE("a registered reader opens a uri as a sequence", "[io::video]")
{
	TempFile file{1};

	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());
	CHECK(sequence->size() == fakeFrameCount);
	CHECK(sequence->spec().extent.x == 4);
	CHECK(sequence->spec().colorSpace == lain::image::ColorSpace::BT709);

	// The frame names its source by CANONICAL uri, which is what makes two references to one frame
	// compare equal however the path was spelled (media::FrameRef).
	CHECK(sequence->frame(0).source == lain::io::canonicalUri(file.string()));
	CHECK(sequence->frame(3).ordinal == 3);
}

TEST_CASE("a reader names the container and the codec it found", "[io::video]")
{
	// Reported facts, not structure — the seam's answer to a video file having two aspects. They
	// are exercised here (and consumed by open()'s log line) rather than left as an accessor
	// nothing calls, which is how an unused member stays quietly broken.
	FakeReader reader;
	CHECK(reader.container() == "fakecontainer");
	CHECK(reader.codec() == "fakecodec");
}

TEST_CASE("a reader that refuses the container is reported, not delivered empty", "[io::video]")
{
	TempFile file{FakeReader::refuseTag};
	CHECK_FALSE(lain::io::video::open(file.string()).has_value());
}

TEST_CASE("a missing file never reaches a reader", "[io::video]")
{
	CHECK_FALSE(lain::io::video::open("/nonexistent/lain/clip.mp4").has_value());
}

TEST_CASE("the source memoises decodes and refuses out-of-range frames", "[io::video]")
{
	TempFile file{1};

	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());

	FakeReader::decodes = 0;
	const lain::image::Image first = sequence->image(2);
	const lain::image::Image again = sequence->image(2);
	REQUIRE(first.valid());
	CHECK(first.data()[0] == 2);
	CHECK(again.data()[0] == 2);
	// The ring cache lives in media::FrameSource, so a codec plugin never implements one — that
	// division is the reason VideoSource is in the seam and not in the plugin.
	CHECK(FakeReader::decodes == 1);

	CHECK_FALSE(sequence->image(fakeFrameCount).valid());
}

TEST_CASE("the container's own timestamps are reported, not derived from the rate", "[io::video]")
{
	TempFile file{1};

	const auto sequence = lain::io::video::open(file.string());
	REQUIRE(sequence.has_value());

	// 100ms x ordinal + 7ms, which no rate would produce — proving VideoSource::timestampOf
	// overrides the base's nominal-rate derivation. That override is what makes VFR free.
	using Catch::Matchers::WithinAbs;
	CHECK_THAT(sequence->frame(0).timestamp.as<lain::core::Time::Milliseconds>(), WithinAbs(7.0, 1e-6));
	CHECK_THAT(sequence->frame(2).timestamp.as<lain::core::Time::Milliseconds>(), WithinAbs(207.0, 1e-6));
}

TEST_CASE("the extension claim is the seam's, so it survives having no codec", "[io::video]")
{
	const auto& extensions = lain::io::video::videoExtensions();
	CHECK(std::find(extensions.begin(), extensions.end(), "mp4") != extensions.end());
	CHECK(std::find(extensions.begin(), extensions.end(), "mov") != extensions.end());
	CHECK(std::find(extensions.begin(), extensions.end(), "png") == extensions.end());
}
