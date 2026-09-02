// Test for the generated video codec aggregator. Unlike the image one, this must be true in BOTH
// configurations — the aggregator is built whether or not any video plugin is, and "registers
// nothing, correctly" is the case the video opt-in rests on (ADR-0019, amended).

#include <lain/io/video/codecs.h>
#include <lain/io/video/open.h> // readerRegistry

#include <catch2/catch_test_macros.hpp>

TEST_CASE("registerVideoCodecs registers exactly the codecs this build enabled", "[io-video-codecs]")
{
	lain::io::video::registerVideoCodecs();
	CHECK(lain::io::video::readerRegistry().size() == LAIN_VIDEO_CODEC_COUNT);

	// The FFmpeg plugin is the only codec there is today, so the count above and this name are the
	// same fact stated twice — deliberately, because a generated file that silently produced an
	// empty function would still pass a count of 0 against a build that enabled it.
#if LAIN_VIDEO_CODEC_COUNT > 0
	CHECK(lain::io::video::readerRegistry().contains("ffmpeg"));
#endif
}
