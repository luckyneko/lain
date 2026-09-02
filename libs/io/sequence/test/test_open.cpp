// Unit tests for the medium-neutral opener registry (M10 slice 5).
//
// The registry is the thing under test, so the media registered here are FAKES: two openers that
// record what they were asked, which is enough to pin the dispatch rule and keeps the test from
// needing a codec, a driver or a real container. registerSequenceOpeners() — the production
// wiring — is proved separately, by the io::image and io::video seams' own tests.

#include "lain/io/sequence/open.h"
#include "lain/io/sequence/openers.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
	std::string claimed; // which fake was asked, and for what
	std::string fallbackUri;

	std::optional<lain::media::FrameSequence> claimingOpener(std::string_view uri, lain::media::FrameRate)
	{
		claimed = std::string(uri);
		return lain::media::FrameSequence{};
	}

	std::optional<lain::media::FrameSequence> defaultOpener(std::string_view uri, lain::media::FrameRate)
	{
		fallbackUri = std::string(uri);
		return lain::media::FrameSequence{};
	}

	struct Registration
	{
		Registration()
		{
			lain::io::sequence::registerOpener("fakevid", &claimingOpener);
			lain::io::sequence::registerDefaultOpener(&defaultOpener);
		}
	};
	const Registration registration;
} // namespace

TEST_CASE("a claimed extension routes to its medium", "[io::sequence]")
{
	claimed.clear();
	REQUIRE(lain::io::sequence::open("/footage/take1.fakevid").has_value());
	CHECK(claimed == "/footage/take1.fakevid");
}

TEST_CASE("the claim is case-insensitive, because io::extensionKey lowercases", "[io::sequence]")
{
	claimed.clear();
	// The same rule io::image keys a codec by. A uri that named one medium in lowercase and
	// another in caps would be the quiet kind of wrong.
	REQUIRE(lain::io::sequence::open("/footage/TAKE1.FAKEVID").has_value());
	CHECK(claimed == "/footage/TAKE1.FAKEVID");
}

TEST_CASE("an unclaimed name goes to the default opener", "[io::sequence]")
{
	// A directory has no extension at all, and a "####" pattern's extension names the STILL
	// format rather than the sequence's — which is why the image medium is the default rather than
	// a set of claims.
	fallbackUri.clear();
	REQUIRE(lain::io::sequence::open("/footage/take1").has_value());
	CHECK(fallbackUri == "/footage/take1");

	fallbackUri.clear();
	REQUIRE(lain::io::sequence::open("/footage/shot.####.png").has_value());
	CHECK(fallbackUri == "/footage/shot.####.png");
}

TEST_CASE("the production wiring claims video containers and defaults to stills", "[io::sequence]")
{
	// Registering the real media over the fakes: what matters is that .mp4 stops reaching the
	// default. Nothing here opens a file — the seams' own tests do that.
	lain::io::sequence::registerSequenceOpeners();

	fallbackUri.clear();
	CHECK_FALSE(lain::io::sequence::open("/nonexistent/lain/clip.mp4").has_value());
	CHECK(fallbackUri.empty()); // the video seam took it, and reported for itself

	// ... while a folder still falls to the still opener, which reports its own reason.
	CHECK_FALSE(lain::io::sequence::open("/nonexistent/lain/stills").has_value());
}
