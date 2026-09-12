// Unit tests for lain::log. The wrapper's own logic is the Level<->backend mapping;
// the front-ends are exercised as a smoke test (format + emit without throwing),
// including that the emit seam treats braces in formatted text as data, and that log.h
// carries the toString() formatter so a lain type renders by name. Output goes to the
// console (ctest captures it); no spdlog is named here.

#include "lain/log/log.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using lain::log::Level;

TEST_CASE("level set/get round-trips through the backend", "[log]")
{
	for (const Level l : {Level::Trace, Level::Debug, Level::Info, Level::Warn,
						  Level::Error, Level::Critical, Level::Off})
	{
		lain::log::setLevel(l);
		REQUIRE(lain::log::level() == l);
	}
	lain::log::setLevel(Level::Info);
}

TEST_CASE("typed front-ends format and emit without throwing", "[log]")
{
	lain::log::setLevel(Level::Trace); // nothing filtered, so every path runs

	REQUIRE_NOTHROW(lain::log::info("plain message, no placeholders"));
	REQUIRE_NOTHROW(lain::log::info("formatted {} and {}", 42, "args"));
	REQUIRE_NOTHROW(lain::log::trace("trace {}", 1));
	REQUIRE_NOTHROW(lain::log::debug("debug {}", 2));
	REQUIRE_NOTHROW(lain::log::warn("warn {:.2f}", 3.14159));
	REQUIRE_NOTHROW(lain::log::error("error {}", 'x'));
	REQUIRE_NOTHROW(lain::log::critical("critical {}", true));

	lain::log::setLevel(Level::Info);
}

// A lain type is one that says what it is: it exposes toString(). Nothing here includes
// <lain/string/format.h>, and test-log links lain::log and nothing else - so if the
// formatter renders this, log.h is what brought it into scope, which is the whole claim.
struct Widget
{
	int count = 0;
	std::string toString() const { return "widget x" + std::to_string(count); }
};

TEST_CASE("a type with toString renders by name at a log site", "[log]")
{
	// The assertion is on fmt::format rather than on log output because the sink is not
	// capturable here - and it is the same formatter the front-ends below reach. What this
	// pins is that INCLUDING log.h is sufficient: drop the include from log.h and this stops
	// compiling rather than quietly printing an address or a type name.
	REQUIRE(fmt::format("{}", Widget{3}) == "widget x3");

	// Format specs still apply, because the formatter inherits fmt's std::string one.
	REQUIRE(fmt::format("[{:>12}]", Widget{3}) == "[   widget x3]");

	lain::log::setLevel(Level::Info);
	REQUIRE_NOTHROW(lain::log::info("{}", Widget{7}));
	REQUIRE_NOTHROW(lain::log::warn("two of them: {} and {}", Widget{1}, Widget{2}));
}

TEST_CASE("the emit seam treats braces in the message as data", "[log]")
{
	lain::log::setLevel(Level::Info);

	// The formatted text itself contains braces; log() must emit them literally, not
	// reinterpret them as placeholders (which would throw on a missing argument).
	const std::string payload = "{not a placeholder} and {}";
	REQUIRE_NOTHROW(lain::log::info("payload: {}", payload));
	REQUIRE_NOTHROW(lain::log::log(Level::Info, payload));
	REQUIRE_NOTHROW(lain::log::log(Level::Warn, "preformatted via the seam"));
}

TEST_CASE("ensure passes through a true condition", "[log]")
{
	// A satisfied precondition returns true and logs nothing (in any build).
	REQUIRE(lain::log::ensure(true, "should not be logged {}", 1));
	REQUIRE(lain::log::ensure(2 + 2 == 4, "arithmetic still works"));
}

#ifdef NDEBUG
TEST_CASE("ensure logs and returns false on a violation (release)", "[log]")
{
	// In a release build the assert is a no-op, so ensure logs the error and returns false
	// for the caller to bail on. (In a debug build this aborts, so the case is NDEBUG-only.)
	REQUIRE_FALSE(lain::log::ensure(false, "expected failure {}", 42));
}
#endif
