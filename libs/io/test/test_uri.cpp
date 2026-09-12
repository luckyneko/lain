// Unit tests for io::canonicalise — the one naming rule that needs the filesystem.
//
// It is worth its own tests because identity is built on it: a media::FrameRef names its source by
// uri and nothing else, and flowview's templateKey caches template definitions by it. Two spellings
// of one path must collapse to one string, or two references to one frame stop comparing equal and
// a cache invalidation quietly misses.

#include "lain/io/uri.h"

#include <lain/core/uri.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>

namespace fs = std::filesystem;

using lain::core::Uri;
using lain::io::canonicalise;

// What a Uri is CALLED — scheme(), path(), extension() — is core's, and is tested in
// libs/core/test/test_uri.cpp. What it RESOLVES to needs a filesystem, so it is tested here, next
// to the library that can ask one. That split is ADR-0023's, made visible in the test layout.

namespace
{
	// A uniquely-named temp directory holding one file, removed on destruction.
	class TempTree
	{
	public:
		TempTree()
		{
			m_dir = lain::testing::scratchPath("uri");
			fs::remove_all(m_dir);
			fs::create_directories(m_dir / "sub");
			std::ofstream out(m_dir / "file.txt", std::ios::trunc);
			out << "x";
		}

		~TempTree()
		{
			std::error_code error;
			fs::remove_all(m_dir, error);
		}

		TempTree(const TempTree&) = delete;
		TempTree& operator=(const TempTree&) = delete;

		const fs::path& dir() const { return m_dir; }

	private:
		fs::path m_dir;
	};
} // namespace

TEST_CASE("one file has one key however it is spelled", "[io][uri]")
{
	TempTree tree;
	const Uri direct = canonicalise(Uri::fromPath(tree.dir() / "file.txt"));

	CHECK(canonicalise(Uri::fromPath(tree.dir() / "." / "file.txt")) == direct);
	CHECK(canonicalise(Uri::fromPath(tree.dir() / "sub" / ".." / "file.txt")) == direct);

	// A file:// spelling names the same resource as the bare path.
	CHECK(canonicalise(Uri{"file://" + (tree.dir() / "file.txt").string()}) == direct);
}

TEST_CASE("a path that does not exist still has a stable key", "[io][uri]")
{
	TempTree tree;

	// weakly_canonical, not canonical: an output pattern is canonicalised before anything is
	// written to it, and a link to a template that has not been created yet must still key
	// consistently so it can heal when the file appears.
	const Uri missing = canonicalise(Uri::fromPath(tree.dir() / "not-yet.json"));
	CHECK_FALSE(missing.empty());
	CHECK(canonicalise(Uri::fromPath(tree.dir() / "sub" / ".." / "not-yet.json")) == missing);

	// The existing prefix is still resolved, so the answer is an absolute path.
	REQUIRE(missing.path().has_value());
	CHECK(missing.path()->is_absolute());
}

TEST_CASE("a non-local scheme is returned unchanged", "[io][uri]")
{
	// There is nothing to canonicalise until such a scheme is actually served; inventing a
	// normalisation now would be a rule with no implementation behind it.
	CHECK(canonicalise(Uri{"s3://bucket/key/../key/frame.png"}) == Uri{"s3://bucket/key/../key/frame.png"});
	CHECK(canonicalise(Uri{"https://example.com/a/./b"}) == Uri{"https://example.com/a/./b"});
}

TEST_CASE("canonicalise and Uri::path compose", "[io][uri]")
{
	// How every caller uses them: canonicalise for the NAME, then ask the Uri for the path to
	// open. A canonical local uri is still local, and a canonical remote one is still remote — so
	// the pair round-trips without either step having to know what the other did.
	const TempTree tree;
	const Uri canonical = canonicalise(Uri::fromPath(tree.dir() / "sub" / ".." / "file.txt"));
	const auto path = canonical.path();
	REQUIRE(path.has_value());
	CHECK(fs::exists(*path));

	CHECK_FALSE(canonicalise(Uri{"s3://bucket/key"}).path().has_value());
}

TEST_CASE("canonicalise answers a Uri, so a key cannot be spelled back into a raw string", "[io][uri]")
{
	// The typing is the point of the change: canonicalUri used to take and return std::string, so
	// its result was indistinguishable from any other string and could be concatenated, re-parsed
	// or handed to fs::path by a caller who had not canonicalised at all. A Uri still has to be
	// canonicalised deliberately — the type does not promise that — but it cannot be mistaken for
	// a path, which is the failure ADR-0023 records as having actually happened.
	TempTree tree;
	const Uri canonical = canonicalise(Uri::fromPath(tree.dir() / "file.txt"));
	STATIC_REQUIRE(std::is_same_v<decltype(canonicalise(Uri{})), lain::core::Uri>);
	CHECK(canonical.isLocal());
	CHECK(canonical.extension() == "txt");
}
