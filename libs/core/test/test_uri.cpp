// core::Uri — the name of one resource. The cases that matter are the ones where a uri and a
// filesystem path disagree, because that disagreement is the whole reason the type exists.

#include "lain/core/uri.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_set>

using lain::core::Uri;

TEST_CASE("a bare path is the local scheme, so no caller special-cases absence", "[core][uri]")
{
	const Uri bare{"/footage/take1.mp4"};
	CHECK(std::string{bare.scheme()} == "local");
	CHECK(std::string{bare.rest()} == "/footage/take1.mp4");
	CHECK(bare.isLocal());
	REQUIRE(bare.path().has_value());
	CHECK(*bare.path() == std::filesystem::path{"/footage/take1.mp4"});
}

TEST_CASE("a scheme is split off, and file:// and local:// are both local", "[core][uri]")
{
	CHECK(std::string{Uri{"file:///footage/take1.mp4"}.scheme()} == "file");
	CHECK(std::string{Uri{"local:///footage/take1.mp4"}.scheme()} == "local");
	CHECK(std::string{Uri{"s3://bucket/key"}.scheme()} == "s3");

	// All three spellings of the local one must name the same path.
	const auto expected = std::filesystem::path{"/footage/take1.mp4"};
	CHECK(Uri{"/footage/take1.mp4"}.path() == expected);
	CHECK(Uri{"file:///footage/take1.mp4"}.path() == expected);
	CHECK(Uri{"local:///footage/take1.mp4"}.path() == expected);
}

TEST_CASE("a remote uri has no path, rather than a path made of its whole text", "[core][uri]")
{
	// The bug this type exists to prevent: fs::path{"s3://bucket/frames"} is a RELATIVE directory
	// called "s3:", which the opener then asks the working directory about and reports missing.
	// nullopt forces a caller that cannot serve a remote resource to say so.
	const Uri remote{"s3://bucket/frames"};
	CHECK_FALSE(remote.isLocal());
	CHECK_FALSE(remote.path().has_value());
	CHECK_FALSE(Uri{"https://example.com/clip.mp4"}.path().has_value());
}

TEST_CASE("the ####-pattern and a Windows path survive, which RFC 3986 would not", "[core][uri]")
{
	// The two collisions ADR-0023 turns on, asserted so a later "let's parse it properly" has to
	// fail a test rather than a review.
	//
	// '#' is the fragment delimiter, so a conforming parser reads path "shot." + fragment
	// "###.png". lain scans that run of '#' for both the sequence opener and the render sweep.
	const Uri pattern{"/footage/shot.####.png"};
	CHECK(std::string{pattern.rest()} == "/footage/shot.####.png");
	CHECK(pattern.isLocal());
	CHECK(pattern.extension() == "png");

	// "C:\footage\clip.mp4" parses as SCHEME "C" under RFC 3986. Here it is a local path, because
	// the split is on "://" and a drive letter is followed by one colon, not three characters.
	const Uri windows{"C:\\footage\\clip.mp4"};
	CHECK(std::string{windows.scheme()} == "local");
	CHECK(windows.isLocal());
	CHECK(std::string{windows.rest()} == "C:\\footage\\clip.mp4");
}

TEST_CASE("extension is the registry key: lowercased, dotless, and last-component", "[core][uri]")
{
	CHECK(Uri{"/footage/take1.MP4"}.extension() == "mp4");
	CHECK(Uri{"shot.0001.PnG"}.extension() == "png");

	// A scheme prefix is harmless, and a REMOTE uri still has a format — refusing to name it would
	// make extension() lie for every scheme but one.
	CHECK(Uri{"file:///footage/take1.mov"}.extension() == "mov");
	CHECK(Uri{"s3://bucket/take1.mov"}.extension() == "mov");

	// No extension is a legitimate answer — it is what "not addressed by format" looks like, and
	// it is how io::sequence tells a folder of stills from a video container.
	CHECK(Uri{"/footage/take1"}.extension().empty());
	CHECK(Uri{""}.extension().empty());
	CHECK(Uri{"/footage/.hidden"}.extension().empty());
}

TEST_CASE("a uri compares and hashes on its exact text", "[core][uri]")
{
	// Deliberately NOT canonicalising here: two spellings of one resource stay two Uris until
	// io::canonicalise is applied, and identity-bearing code says that it does so. A Uri that
	// silently canonicalised would touch the filesystem from lain::core, which is std-only.
	CHECK(Uri{"/a/b"} == Uri{"/a/b"});
	CHECK(Uri{"/a/./b"} != Uri{"/a/b"});

	std::unordered_set<Uri> seen;
	seen.insert(Uri{"/a/b"});
	seen.insert(Uri{"/a/b"});
	seen.insert(Uri{"/a/./b"});
	CHECK(seen.size() == 2);
}

TEST_CASE("an empty uri names nothing", "[core][uri]")
{
	const Uri none;
	CHECK(none.empty());
	CHECK_FALSE(static_cast<bool>(none));
	CHECK(none.toString().empty());

	// Distinct from a uri naming the empty path: both are local, but only one is empty.
	CHECK(Uri{"."}.empty() == false);
}

TEST_CASE("fromPath is the explicit conversion, and it adds no scheme", "[core][uri]")
{
	// Explicit because it is the conversion that can silently mean something else — a Windows path
	// is not a uri. No "file://" prefix is added: a bare path already IS the local scheme, and
	// prefixing one would change what every document and manifest on disk says.
	const Uri fromPath = Uri::fromPath(std::filesystem::path{"/footage/take1.mp4"});
	CHECK(fromPath == Uri{"/footage/take1.mp4"});
	CHECK(std::string{fromPath.scheme()} == "local");
}
