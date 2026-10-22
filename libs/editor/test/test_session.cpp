// The session file: best-effort persistence of an app's convenience state. Every case writes to a
// scratch path, never to the user's ~/.<app>.

#include <lain/data/macros.h>
#include <lain/editor/session.h>
#include <lain/io/data/codecs.h>
#include <lain/testing/scratch.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sessiontest
{
	struct Session
	{
		std::filesystem::path lastDocument;
		std::vector<std::filesystem::path> recent;
		int count = 7;
	};
	LAIN_SERIALIZE(Session, lastDocument, recent, count)
} // namespace sessiontest

using sessiontest::Session;

TEST_CASE("the session file lives in the app's config folder", "[session]")
{
	const std::filesystem::path file = lain::editor::sessionFile("someapp");
	if (file.empty())
		SKIP("no home directory to resolve");
	REQUIRE(file.filename() == "session.json");
	REQUIRE(file.parent_path().filename() == ".someapp");
}

TEST_CASE("a missing session file is a default session, not an error", "[session]")
{
	const Session session = lain::editor::loadSession<Session>(lain::testing::scratchPath("absent", ".json"));
	REQUIRE(session.lastDocument.empty());
	REQUIRE(session.count == 7);
	REQUIRE(lain::editor::loadSession<Session>({}).count == 7); // nowhere to read from either
}

#if LAIN_EDITOR_TEST_JSON
// Inside the block that uses it: with no json codec there is nothing to write, and an unused static
// function fails the strict build.
static void writeText(const std::filesystem::path& file, const std::string& text)
{
	std::ofstream out(file, std::ios::binary);
	out << text;
}

TEST_CASE("a session round-trips through its file", "[session]")
{
	lain::io::data::registerDataCodecs();
	const std::filesystem::path file = lain::testing::scratchPath("session", ".json");
	Session session;
	session.lastDocument = "/rigs/hall.json";
	session.recent = {"/rigs/hall.json", "/rigs/studio.json"};
	session.count = 3;
	lain::editor::saveSession(file, session);

	const Session back = lain::editor::loadSession<Session>(file);
	REQUIRE(back.lastDocument == session.lastDocument);
	REQUIRE(back.recent == session.recent);
	REQUIRE(back.count == 3);
}

TEST_CASE("an unreadable or misshapen session file is a default session", "[session]")
{
	lain::io::data::registerDataCodecs();

	const std::filesystem::path garbage = lain::testing::scratchPath("garbage", ".json");
	writeText(garbage, "this is not json {");
	REQUIRE(lain::editor::loadSession<Session>(garbage).count == 7);

	const std::filesystem::path array = lain::testing::scratchPath("array", ".json");
	writeText(array, "[1, 2, 3]"); // valid json, but not a session's shape
	REQUIRE(lain::editor::loadSession<Session>(array).count == 7);
}

TEST_CASE("a session file missing a field keeps that field's default", "[session]")
{
	// An older file, written before a field existed, still reads.
	lain::io::data::registerDataCodecs();
	const std::filesystem::path file = lain::testing::scratchPath("older", ".json");
	writeText(file, R"({"lastDocument": "/rigs/hall.json"})");
	const Session session = lain::editor::loadSession<Session>(file);
	REQUIRE(session.lastDocument == std::filesystem::path("/rigs/hall.json"));
	REQUIRE(session.count == 7);
}
#endif
