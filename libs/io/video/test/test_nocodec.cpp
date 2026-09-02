// The default build's video story, in its own executable.
//
// It needs an EMPTY reader registry, and a core::Factory only ever grows — so this cannot share a
// process with test_open.cpp's fake codec without depending on the order Catch2 runs cases in. One
// case, one binary, no ordering rule to remember.

#include "lain/io/video/open.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

TEST_CASE("with no codec registered the seam reports a missing capability", "[io::video]")
{
	namespace fs = std::filesystem;
	const fs::path path = fs::temp_directory_path() / "lain_video_nocodec.mp4";
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
