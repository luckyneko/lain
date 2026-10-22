// DocumentGuard — the rule between a destructive request and unsaved work. These are the rules
// flowview's unsaved-changes modal carried out with nothing to test them, since the modal needs a gui;
// here the answers are given directly.

#include <lain/editor/guard.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

using lain::editor::DocumentGuard;
using lain::editor::UnsavedAnswer;

// A save that reports `result` and counts how often it was asked.
struct Save
{
	bool result = true;
	int calls = 0;
	bool operator()()
	{
		++calls;
		return result;
	}
};

TEST_CASE("a request with nothing unsaved comes straight back", "[guard]")
{
	DocumentGuard<std::string> guard;
	const std::optional<std::string> now = guard.request("open a.json", false);
	REQUIRE(now == std::optional<std::string>("open a.json"));
	REQUIRE(guard.pending() == nullptr);
	REQUIRE_FALSE(guard.takePrompt()); // nothing to ask about
}

TEST_CASE("a request with unsaved work is held, and the question asked once", "[guard]")
{
	DocumentGuard<std::string> guard;
	REQUIRE_FALSE(guard.request("new", true).has_value());
	REQUIRE(guard.pending() != nullptr);
	REQUIRE(*guard.pending() == "new");
	REQUIRE(guard.takePrompt());
	REQUIRE_FALSE(guard.takePrompt());	 // consumed: the question is open, not opened again each frame
	REQUIRE(guard.pending() != nullptr); // still waiting for the answer
}

TEST_CASE("Discard lets the request out without saving", "[guard]")
{
	DocumentGuard<std::string> guard;
	guard.request("new", true);
	Save save;
	REQUIRE(guard.answer(UnsavedAnswer::Discard, save) == std::optional<std::string>("new"));
	REQUIRE(save.calls == 0);
	REQUIRE(guard.pending() == nullptr);
}

TEST_CASE("Save lets the request out once the document is saved", "[guard]")
{
	DocumentGuard<std::string> guard;
	guard.request("open b.json", true);
	Save save{true};
	REQUIRE(guard.answer(UnsavedAnswer::Save, save) == std::optional<std::string>("open b.json"));
	REQUIRE(save.calls == 1);
	REQUIRE(guard.pending() == nullptr);
}

TEST_CASE("a Save that does not go through cancels the request", "[guard]")
{
	// The save panel cancelled, or the write failed: the user asked to keep this work, so the swap
	// that would have replaced it must not happen.
	DocumentGuard<std::string> guard;
	guard.request("new", true);
	Save save{false};
	REQUIRE_FALSE(guard.answer(UnsavedAnswer::Save, save).has_value());
	REQUIRE(save.calls == 1);
	REQUIRE(guard.pending() == nullptr); // dropped, not left waiting
}

TEST_CASE("Cancel drops the request without saving", "[guard]")
{
	DocumentGuard<std::string> guard;
	guard.request("new", true);
	Save save;
	REQUIRE_FALSE(guard.answer(UnsavedAnswer::Cancel, save).has_value());
	REQUIRE(save.calls == 0);
	REQUIRE(guard.pending() == nullptr);
}

TEST_CASE("a new request replaces one left unanswered", "[guard]")
{
	// A question closed with Escape resolves nothing: its request stays behind until the next ask.
	DocumentGuard<std::string> guard;
	guard.request("open a.json", true);
	guard.request("open b.json", true);
	REQUIRE(*guard.pending() == "open b.json");
	Save save;
	REQUIRE(guard.answer(UnsavedAnswer::Discard, save) == std::optional<std::string>("open b.json"));

	// And a request made once the work is saved supersedes a stale one rather than leaving it behind,
	// along with the question about it that had not been opened yet: a modal asking about nothing.
	guard.request("open c.json", true);
	REQUIRE(guard.request("open d.json", false) == std::optional<std::string>("open d.json"));
	REQUIRE(guard.pending() == nullptr);
	REQUIRE_FALSE(guard.takePrompt());
}

TEST_CASE("with nothing held there is nothing to answer", "[guard]")
{
	DocumentGuard<std::string> guard;
	Save save;
	REQUIRE_FALSE(guard.answer(UnsavedAnswer::Save, save).has_value());
	REQUIRE_FALSE(guard.answer(UnsavedAnswer::Discard, save).has_value());
	REQUIRE(save.calls == 0); // no request, so no reason to save
}
