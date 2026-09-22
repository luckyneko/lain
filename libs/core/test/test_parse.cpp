// core::parse / core::parseAt — text to a number, the two ways the tree asks for it.
//
// The cases came from the four sites this replaced (Version's fields, Range's scan, a numbered
// still sequence, a v1 editor key), which is why they read as field and scanner cases rather than
// as a survey of std::from_chars.

#include "lain/core/parse.h"
#include "lain/core/range.h"
#include "lain/core/version.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>

using lain::core::parse;
using lain::core::parseAt;
using lain::core::parseInto;
using lain::core::Range;
using lain::core::Version;

TEST_CASE("parse takes the whole text or nothing", "[core][parse]")
{
	REQUIRE(parse<std::uint32_t>("0") == 0u);
	REQUIRE(parse<std::uint32_t>("42") == 42u);
	REQUIRE(parse<std::uint32_t>("4294967295") == 4294967295u);

	// Each of these is a way a caller could be handed a plausible-looking wrong answer. "1x" is the
	// one that needs the full-consumption test: from_chars stops at the first non-digit and reports
	// SUCCESS for the prefix it read, so without it a typo silently reads as 1.
	REQUIRE_FALSE(parse<std::uint32_t>("1x").has_value());
	REQUIRE_FALSE(parse<std::uint32_t>("1 ").has_value());
	REQUIRE_FALSE(parse<std::uint32_t>("1.0").has_value());
	REQUIRE_FALSE(parse<std::uint32_t>("0x10").has_value()); // read as 0, then junk
	REQUIRE_FALSE(parse<std::uint32_t>("").has_value());
}

TEST_CASE("parse skips no whitespace and takes no sign for an unsigned type", "[core][parse]")
{
	// from_chars answers all three with no check of our own, which is most of why it is the
	// mechanism: a hand-rolled digit scan has to remember each one separately.
	REQUIRE_FALSE(parse<std::uint32_t>(" 1").has_value());
	REQUIRE_FALSE(parse<std::uint32_t>("+1").has_value());
	REQUIRE_FALSE(parse<std::uint32_t>("-1").has_value());

	// A signed type takes the sign, and only the sign.
	REQUIRE(parse<int>("-5") == -5);
	REQUIRE_FALSE(parse<int>("+5").has_value());
}

TEST_CASE("a value too large to hold is refused, not wrapped", "[core][parse]")
{
	// The property the whole thing rests on: std::stoul-style wrapping would hand back a number
	// that is wrong and says nothing about being wrong. Version::parse("4294967296.0.0") is the
	// production case.
	REQUIRE(parse<std::uint32_t>("4294967295").has_value());
	REQUIRE_FALSE(parse<std::uint32_t>("4294967296").has_value());
	REQUIRE_FALSE(parse<std::uint8_t>("256").has_value());
	REQUIRE(parse<std::uint8_t>("255") == 255);
}

TEST_CASE("parseAt reads a leading run and advances past it", "[core][parse]")
{
	// Range::parse's scan, in miniature: a number, a separator this reads itself, a number.
	const std::string text = "12-34x5";
	std::size_t pos = 0;

	REQUIRE(parseAt<std::size_t>(text, pos) == 12u);
	REQUIRE(pos == 2);
	REQUIRE(text[pos] == '-');

	++pos;
	REQUIRE(parseAt<std::size_t>(text, pos) == 34u);
	REQUIRE(pos == 5);

	++pos;
	REQUIRE(parseAt<std::size_t>(text, pos) == 5u);
	REQUIRE(pos == text.size());
}

TEST_CASE("parseAt does not move pos on a refusal", "[core][parse]")
{
	// A scanner decides what to do next from where it is, so a position moved by a failed read
	// would have it resume somewhere it never successfully read.
	const std::string text = "12-x";
	std::size_t pos = 3;
	REQUIRE_FALSE(parseAt<std::size_t>(text, pos).has_value());
	REQUIRE(pos == 3);

	// At the very end there is nothing to read, and that is a refusal rather than a zero.
	pos = text.size();
	REQUIRE_FALSE(parseAt<std::size_t>(text, pos).has_value());
	REQUIRE(pos == text.size());

	// The arm that makes this a real guard rather than a restatement of std::from_chars: on
	// result_out_of_range from_chars DOES advance its ptr past the digits it refused, so returning
	// before pos is written is what keeps a refusal from moving the scanner.
	const std::string tooBig = "99999999999999999999-5";
	pos = 0;
	REQUIRE_FALSE(parseAt<std::uint32_t>(tooBig, pos).has_value());
	REQUIRE(pos == 0);
}

TEST_CASE("parse is parseAt plus nothing left over", "[core][parse]")
{
	// The coupling is the point: the strict policy is stated once, so the two cannot drift into
	// disagreeing about what "1x" means.
	for (const std::string text : {"0", "42", "1x", "1 ", "", " 1", "4294967296"})
	{
		std::size_t pos = 0;
		const auto scanned = parseAt<std::uint32_t>(text, pos);
		const bool wholeText = scanned.has_value() && pos == text.size();

		REQUIRE(parse<std::uint32_t>(text).has_value() == wholeText);
		if (wholeText)
			REQUIRE(*parse<std::uint32_t>(text) == *scanned);
	}
}

TEST_CASE("a float reads through a different mechanism and answers the same way", "[core][parse]")
{
	// float does NOT go through std::from_chars: libc++ marks the floating-point overloads
	// "introduced in macOS 26", so on Apple they are unavailable to anything with an ordinary
	// deployment target. The arm is std::strtof instead — which is looser, and is made to answer
	// alike. This case is the only thing holding the two arms together, since nothing about the two
	// mechanisms does.
	CHECK(parse<float>("1.5") == 1.5f);
	CHECK(parse<float>("-1.5") == -1.5f);
	CHECK(parse<float>("0") == 0.0f);
	CHECK(parse<float>("1e3") == 1000.0f);
	CHECK(parse<float>(".5") == 0.5f);
	CHECK(parse<float>("5.") == 5.0f);
	CHECK(parse<double>("1.5") == 1.5);
	CHECK(parse<long double>("1.5") == 1.5L);

	// An integer is a number, so it reads as one.
	CHECK(parse<float>("12") == 12.0f);

	REQUIRE(parse<float>("inf").has_value());
	CHECK(std::isinf(*parse<float>("inf")));
	REQUIRE(parse<float>("nan").has_value());
	CHECK(std::isnan(*parse<float>("nan")));
}

TEST_CASE("the integral and floating arms refuse the same forms", "[core][parse]")
{
	// THE AGREEMENT, asserted over one list rather than two copies of one — a list duplicated per
	// type is exactly the drift this exists to catch. Each of these is a form std::strtof accepts
	// and std::from_chars does not, so a float arm left to its own mechanism would take them.
	for (const std::string text : {"+1", " 1", "\t1", "0x10", "-0x10", "1 ", "", "hello", "1x"})
	{
		INFO("text: '" << text << "'");
		CHECK_FALSE(parse<int>(text).has_value());
		CHECK_FALSE(parse<float>(text).has_value());
		CHECK_FALSE(parse<double>(text).has_value());
	}
}

TEST_CASE("a float too large to hold is refused, not saturated", "[core][parse]")
{
	// std::strtof answers HUGE_VALF and sets ERANGE, which read without the errno check would hand
	// back an infinity for a finite number someone wrote — the float twin of the integral wrap.
	CHECK_FALSE(parse<float>("1e999").has_value());
	CHECK_FALSE(parse<double>("1e999").has_value());
	CHECK(parse<double>("1e300").has_value()); // in range for a double, not for a float
	CHECK_FALSE(parse<float>("1e300").has_value());
}

TEST_CASE("parseAt scans a float and advances past it", "[core][parse]")
{
	// The incremental form works for a float too, so the two arms match in shape and not only in
	// what they refuse.
	const std::string text = "1.5,-2e2";
	std::size_t pos = 0;

	CHECK(parseAt<float>(text, pos) == 1.5f);
	REQUIRE(pos == 3);
	CHECK(text[pos] == ',');

	++pos;
	CHECK(parseAt<float>(text, pos) == -200.0f);
	CHECK(pos == text.size());

	// And a refusal leaves the scanner where it was, as the integral arm does.
	pos = 0;
	CHECK_FALSE(parseAt<float>(" 1.5", pos).has_value());
	CHECK(pos == 0);
}

TEST_CASE("parseInto is parse in the spelling an out-parameter hook needs", "[core][parse]")
{
	// ONE body for every type that offers CLI11's lexical_cast. Two types here, with unrelated
	// parse() implementations, because a single one would not distinguish "the adapter works" from
	// "Range works" — and it is the adapter being shared that this pins.
	Range range;
	REQUIRE(parseInto("0-9x3", range));
	REQUIRE(range == Range{0, 9, 3});

	Version version;
	REQUIRE(parseInto("1.4.0-rc.2", version));
	REQUIRE(version == Version{1, 4, 0});

	SECTION("a refusal leaves the output alone")
	{
		// The contract every such hook has, and the reason the adapter is one function: a copy per
		// type is where one of them eventually assigns a half-built value on a refusal, in a branch
		// the caller reads as "nothing happened".
		Range untouchedRange{1, 2, 1};
		REQUIRE_FALSE(parseInto("nonsense", untouchedRange));
		REQUIRE(untouchedRange == Range{1, 2, 1});

		Version untouchedVersion{9, 9, 9};
		REQUIRE_FALSE(parseInto("nonsense", untouchedVersion));
		REQUIRE(untouchedVersion == Version{9, 9, 9});
	}

	SECTION("it takes the whole text, because T::parse does")
	{
		// parseInto adds no reading of its own: the strictness is the type's, so "0-9junk" is
		// refused here for exactly the reason Range::parse refuses it.
		Range range2;
		REQUIRE_FALSE(parseInto("0-9junk", range2));
		REQUIRE_FALSE(parseInto("", range2));
	}
}
