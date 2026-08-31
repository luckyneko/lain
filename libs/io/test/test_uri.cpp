// Unit tests for io::canonicalUri — the one function that decides a resource's name.
//
// It is worth its own tests because identity is built on it: a media::FrameRef names its source by
// uri and nothing else, and flowview's templateKey caches template definitions by it. Two spellings
// of one path must collapse to one string, or two references to one frame stop comparing equal and
// a cache invalidation quietly misses.

#include "lain/io/uri.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using lain::io::canonicalUri;

namespace
{
	// A uniquely-named temp directory holding one file, removed on destruction.
	class TempTree
	{
	public:
		TempTree()
		{
			static int counter = 0;
			m_dir = fs::temp_directory_path() / ("lain_uri_" + std::to_string(counter++));
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
	const std::string direct = canonicalUri((tree.dir() / "file.txt").string());

	CHECK(canonicalUri((tree.dir() / "." / "file.txt").string()) == direct);
	CHECK(canonicalUri((tree.dir() / "sub" / ".." / "file.txt").string()) == direct);

	// A file:// spelling names the same resource as the bare path.
	CHECK(canonicalUri("file://" + (tree.dir() / "file.txt").string()) == direct);
}

TEST_CASE("a path that does not exist still has a stable key", "[io][uri]")
{
	TempTree tree;

	// weakly_canonical, not canonical: an output pattern is canonicalised before anything is
	// written to it, and a link to a template that has not been created yet must still key
	// consistently so it can heal when the file appears.
	const std::string missing = canonicalUri((tree.dir() / "not-yet.json").string());
	CHECK_FALSE(missing.empty());
	CHECK(canonicalUri((tree.dir() / "sub" / ".." / "not-yet.json").string()) == missing);

	// The existing prefix is still resolved, so the answer is an absolute path.
	CHECK(fs::path(missing).is_absolute());
}

TEST_CASE("a non-local scheme is returned unchanged", "[io][uri]")
{
	// There is nothing to canonicalise until such a scheme is actually served; inventing a
	// normalisation now would be a rule with no implementation behind it.
	CHECK(canonicalUri("s3://bucket/key/../key/frame.png") == "s3://bucket/key/../key/frame.png");
	CHECK(canonicalUri("https://example.com/a/./b") == "https://example.com/a/./b");
}
