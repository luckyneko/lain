// flowview's session file keeps its shape across the hoist of its recents into lain::editor: the
// same four keys, in the same order, so a ~/.flowview/session.json written before still reads.
// Driver-free: it maps a Session to and from a data::Value and never touches the file.

#include "session.h"

#include <lain/data/data.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using flowview::Session;
using std::filesystem::path;

TEST_CASE("a session writes the keys its file has always had", "[session]")
{
	Session session;
	lain::editor::noteDocument(session.graphs, "/graphs/a.json");
	lain::editor::noteDocument(session.graphs, "/graphs/b.json");
	session.lastDialogDir = "/graphs";
	const lain::data::Value value = lain::data::toValue(session);

	REQUIRE(value.isObject());
	std::vector<std::string> keys;
	for (const auto& entry : *value.asObject())
		keys.push_back(entry.first);
	REQUIRE(keys == std::vector<std::string>{"lastGraph", "recentGraphs", "lastDialogDir", "scheduler"});
	const lain::data::Value* last = value.find("lastGraph");
	REQUIRE(last != nullptr);
	REQUIRE(*last == lain::data::toValue(path("/graphs/b.json")));
}

TEST_CASE("a session file written before the hoist still reads", "[session]")
{
	// As flowview wrote it with LAIN_SERIALIZE over flat lastGraph / recentGraphs fields.
	lain::data::Value::Object old;
	old.emplace_back("lastGraph", lain::data::toValue(path("/graphs/b.json")));
	old.emplace_back("recentGraphs", lain::data::toValue(std::vector<path>{"/graphs/b.json", "/graphs/a.json"}));
	old.emplace_back("lastDialogDir", lain::data::toValue(path("/graphs")));
	old.emplace_back("scheduler", lain::data::toValue(flowview::RunStrategy::Serial));

	const std::optional<Session> session = lain::data::fromValue<Session>(lain::data::Value(std::move(old)));
	REQUIRE(session.has_value());
	REQUIRE(session->graphs.last == path("/graphs/b.json"));
	REQUIRE(session->graphs.recent == std::vector<path>{"/graphs/b.json", "/graphs/a.json"});
	REQUIRE(session->lastDialogDir == path("/graphs"));
	REQUIRE(session->scheduler == flowview::RunStrategy::Serial);
}
