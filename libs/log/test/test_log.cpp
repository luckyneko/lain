// Unit tests for lain::log. The wrapper's own logic is the Level<->backend mapping;
// the front-ends are exercised as a smoke test (format + emit without throwing),
// including that the emit seam treats braces in formatted text as data. Output goes
// to the console (ctest captures it); no spdlog is named here.

#include <lain/log/log.h>

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
