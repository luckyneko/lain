// The default build's video story, in its own executable.
//
// It needs EMPTY reader AND writer registries, and a core::Factory only ever grows — so this
// cannot share a process with test_open.cpp's and test_save.cpp's fakes without depending on the
// order Catch2 runs cases in. One binary, no ordering rule to remember.

#include "lain/io/video/open.h"
#include "lain/io/video/save.h"

#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

TEST_CASE("with no codec registered the seam reports a missing capability", "[io::video]")
{
	namespace fs = std::filesystem;
	const fs::path path = lain::testing::scratchPath("nocodec", ".mp4");
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << "not really an mp4";
	}

	REQUIRE(lain::io::video::readerRegistry().size() == 0);

	// The file EXISTS and the seam still refuses it — the report is about the build, not the file.
	// That is the whole reason lain::io::video is compiled even when no codec plugin is: an .mp4
	// keeps its meaning, and a document naming one keeps its node and its edges (ADR-0019).
	CHECK_FALSE(lain::io::video::open(path.string()).has_value());

	std::error_code error;
	fs::remove(path, error);
}

TEST_CASE("with no codec registered the seam refuses to write, for the same reason", "[io::video]")
{
	namespace fs = std::filesystem;
	const fs::path path = lain::testing::scratchPath("nocodec-out", ".mp4");
	std::error_code error;
	fs::remove(path, error);

	REQUIRE(lain::io::video::writerRegistry().size() == 0);

	lain::media::FrameSpec spec;
	spec.extent = {4, 2};
	spec.pixelFormat = lain::image::PixelFormat::RGB8;
	spec.colorSpace = lain::image::ColorSpace::BT709;
	spec.rate = {24, 1};

	// The spec is one lain can write and the path names a container it claims, so the ONLY thing
	// missing is the capability — which is exactly what the refusal has to be about. A build with
	// no codec must still recognise .mp4 as video, or a document naming one loses its meaning
	// rather than losing an encoder (ADR-0019, amended).
	CHECK(lain::io::video::canEncode(spec));
	CHECK(lain::io::video::isVideoUri(path.string()));
	CHECK(lain::io::video::openWriter(path.string(), spec) == nullptr);

	// Refused before the transport, so no half-made file is left behind.
	CHECK_FALSE(fs::exists(path));

	fs::remove(path, error);
}
