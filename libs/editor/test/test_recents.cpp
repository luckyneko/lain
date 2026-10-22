// RecentDocuments — the last document and the Open Recent list, and the one rule for how they change.

#include <lain/editor/recents.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using lain::editor::RecentDocuments;
using std::filesystem::path;

TEST_CASE("noting a document makes it the last and the most recent", "[recents]")
{
	RecentDocuments docs;
	lain::editor::noteDocument(docs, "a.json");
	lain::editor::noteDocument(docs, "b.json");
	REQUIRE(docs.last == path("b.json"));
	REQUIRE(docs.recent == std::vector<path>{"b.json", "a.json"});
}

TEST_CASE("noting a document already listed moves it to the front, once", "[recents]")
{
	RecentDocuments docs;
	lain::editor::noteDocument(docs, "a.json");
	lain::editor::noteDocument(docs, "b.json");
	lain::editor::noteDocument(docs, "a.json");
	REQUIRE(docs.recent == std::vector<path>{"a.json", "b.json"});
}

TEST_CASE("the recent list is capped, oldest dropped", "[recents]")
{
	RecentDocuments docs;
	for (int i = 0; i < 15; ++i)
		lain::editor::noteDocument(docs, "doc" + std::to_string(i) + ".json");
	REQUIRE(docs.recent.size() == lain::editor::maxRecentDocuments);
	REQUIRE(docs.recent.front() == path("doc14.json"));
	REQUIRE(docs.recent.back() == path("doc5.json"));

	lain::editor::noteDocument(docs, "x.json", 3); // an app may keep fewer
	REQUIRE(docs.recent == std::vector<path>{"x.json", "doc14.json", "doc13.json"});
}

TEST_CASE("an empty path is not a document", "[recents]")
{
	RecentDocuments docs;
	lain::editor::noteDocument(docs, "a.json");
	lain::editor::noteDocument(docs, path());
	REQUIRE(docs.last == path("a.json"));
	REQUIRE(docs.recent == std::vector<path>{"a.json"});
}

TEST_CASE("forgetting a document drops it from the list, and from last only if it is last", "[recents]")
{
	RecentDocuments docs;
	lain::editor::noteDocument(docs, "a.json");
	lain::editor::noteDocument(docs, "b.json");

	lain::editor::forgetDocument(docs, "a.json");
	REQUIRE(docs.recent == std::vector<path>{"b.json"});
	REQUIRE(docs.last == path("b.json")); // not the one forgotten

	lain::editor::forgetDocument(docs, "b.json");
	REQUIRE(docs.recent.empty());
	REQUIRE(docs.last.empty());
}
