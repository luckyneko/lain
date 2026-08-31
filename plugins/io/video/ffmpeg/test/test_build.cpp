// The runtime half of the licence gate (ADR-0019).
//
// cmake/addFFmpeg.cmake refuses a GPL-configured archive at configure time by reading its
// MANIFEST.txt — no execution, so it works when cross-compiling. This asks the LINKED library
// the same questions through its own API. Two independent things must be wrong at once for a
// GPL-configured FFmpeg to reach a build, and for an FFmpeg supplied through LAIN_FFMPEG_ROOT
// (which has no manifest) this is the only gate there is.
//
// It is deliberately not a [gpu]-style skip-aware test: there is no driver to be absent. If
// this binary runs at all, the library loaded, and a wrong answer is a real failure.

#include <lain/io/video/ffmpeg/build.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace lain::io::video::ffmpeg;

TEST_CASE("the linked FFmpeg reports a configuration", "[video][license]")
{
	// An empty string here means the library answered nothing, which would make every
	// assertion below vacuously true — the failure mode a licence check must not have.
	CHECK_FALSE(configuration().empty());
	CHECK_FALSE(license().empty());
}

TEST_CASE("the linked FFmpeg is not GPL- or nonfree-configured", "[video][license]")
{
	const std::string config = configuration();

	// The configure string is the licence. A build with --enable-gpl relicenses the whole
	// distribution under GPL whether or not a GPL codec is ever called, which is exactly the
	// silent introduction ADR-0015's policy exists to prevent.
	CHECK(config.find("--enable-gpl") == std::string::npos);
	CHECK(config.find("--enable-nonfree") == std::string::npos);

	// avutil_license() is derived independently of the string above, so agreeing costs a
	// second thing to be wrong. It returns exactly one of five fixed strings: "LGPL version
	// 2.1 or later", "LGPL version 3 or later", "GPL version 2 or later", "GPL version 3 or
	// later", or "nonfree and unredistributable".
	//
	// So the test is that it STARTS with LGPL, not that it contains it. Searching for a
	// substring is the trap here, in both directions: every acceptable answer contains "GPL",
	// and — as the first draft of this test discovered by failing — "LGPL version 2.1 or
	// later" contains "GPL version" too, one character in. A prefix is the only form that
	// separates the five.
	CHECK(license().rfind("LGPL", 0) == 0);
}

TEST_CASE("the linked FFmpeg carries no GPL encoder", "[video][license]")
{
	const std::string config = configuration();

	// The second opinion on the encoder table, matching what the publishing repository
	// asserts of its own artifacts: x264 and x265 are GPL SOURCE linked in, a different thing
	// from the platform wrappers, and the tier forbids them by name.
	CHECK(config.find("libx264") == std::string::npos);
	CHECK(config.find("libx265") == std::string::npos);
	CHECK(config.find("libfdk_aac") == std::string::npos);
}
