// Unit tests for lain::string's <key> pattern system — the replacement for io::NumberField,
// which could name exactly one thing and spelled it "####".
//
// The property the whole thing exists to hold is the LAST case here: what format() writes,
// match() reads back. io::NumberField held that structurally, by being one function both
// directions called; here the grammar is shared and the KEY is the caller's, so the inverse is a
// property to assert rather than a consequence of the layout.

#include "lain/string/pattern.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using lain::string::Captures;
using lain::string::Dictionary;
using lain::string::Pattern;

TEST_CASE("a key formats through its fmt spec", "[string][pattern]")
{
	Dictionary values;
	values.set("frame", 7);

	CHECK(*Pattern{"shot.<frame:04>.png"}.format(values) == "shot.0007.png");
	CHECK(*Pattern{"shot.<frame>.png"}.format(values) == "shot.7.png");
	// The whole point of rewriting to fmt rather than parsing a spec of our own: the spec is
	// fmt's, so hex, alignment and everything else come along at no cost.
	CHECK(*Pattern{"<frame:#x>"}.format(values) == "0x7");
	CHECK(*Pattern{"<frame:+}"}.format(values) == "<frame:+}"); // a brace cannot appear in a spec
}

TEST_CASE("the whole fmt spec language is reachable, alignment included", "[string][pattern]")
{
	// The delimiters are two of fmt's three alignment characters, so a token that simply ended at
	// the first '>' would have left "{:>8}" and "{:*<8}" with no spelling at all. findSpecEnd
	// knows where fmt puts an alignment — spec index 0, or 1 after a fill — which is the only
	// place this grammar knows anything about what a spec MEANS.
	Dictionary values;
	values.set("frame", 7);
	values.set("name", std::string{"ab"});

	CHECK(*Pattern{"[<frame:>8>]"}.format(values) == "[       7]");	 // bare align, right
	CHECK(*Pattern{"[<name:<8>]"}.format(values) == "[ab      ]");	 // bare align, left
	CHECK(*Pattern{"[<frame:*>8>]"}.format(values) == "[*******7]"); // fill, then align
	CHECK(*Pattern{"[<frame:^7>]"}.format(values) == "[   7   ]");	 // centre, which never collided
	CHECK(*Pattern{"[<name:8>]"}.format(values) == "[ab      ]");	 // width alone: fmt's own default

	// An alignment is only ASSUMED when that reading still leaves a closing '>' behind, which is
	// what keeps the rule additive: "<name:8>" is a width, not a fill of '8' aligned right, and
	// "<frame:>" is still the empty spec it always was rather than becoming literal text.
	CHECK(*Pattern{"out.<frame:>.png"}.format(values) == "out.7.png");
	CHECK(Pattern{"out.<frame:>.png"}.keys() == std::vector<std::string>{"frame"});

	// And a spec with no alignment is untouched — "zz" is still handed to fmt, and still refused.
	CHECK_FALSE(Pattern{"out.<frame:zz>.png"}.format(values).has_value());
}

TEST_CASE("a width is a padding convention, not a limit", "[string][pattern]")
{
	// A frame too wide for its field widens the field rather than losing a digit, and match()
	// reads the widened name back — which is what keeps a long render's output openable.
	Dictionary values;
	values.set("frame", 12345);
	CHECK(*Pattern{"out.<frame:02>.png"}.format(values) == "out.12345.png");

	const Pattern pattern{"out.<frame:02>.png"};
	const std::optional<Captures> captures = pattern.match("out.12345.png");
	REQUIRE(captures.has_value());
	CHECK(captures->at("frame") == "12345");
}

TEST_CASE("an unmapped key survives as its own token", "[string][pattern]")
{
	// Partial resolution: a pattern that has been half-filled is still a pattern, and the key
	// nobody supplied is still there to be supplied later. This is what the rewrite-only-mapped-
	// keys rule buys, and it is why an unknown key is not an error.
	Dictionary values;
	values.set("take", 3);

	const Pattern pattern{"take<take:02>/shot.<frame:04>.png"};
	const std::optional<std::string> resolved = pattern.format(values);
	REQUIRE(resolved.has_value());
	CHECK(*resolved == "take03/shot.<frame:04>.png");

	// And the result is a pattern again, with the one key that is left.
	const Pattern remaining{*resolved};
	CHECK(remaining.keys() == std::vector<std::string>{"frame"});
}

TEST_CASE("a literal brace is literal", "[string][pattern]")
{
	// Literals are escaped before they reach fmt, so a path holding a brace formats rather than
	// being read as a format hole nobody wrote.
	Dictionary values;
	values.set("frame", 1);
	CHECK(*Pattern{"{cache}/f<frame>.png"}.format(values) == "{cache}/f1.png");
	CHECK(*Pattern{"{}"}.format(values) == "{}");

	const std::optional<Captures> captures = Pattern{"{cache}/f<frame>.png"}.match("{cache}/f1.png");
	REQUIRE(captures.has_value());
	CHECK(captures->at("frame") == "1");
}

TEST_CASE("a spec fmt refuses is a refusal, not an exception", "[string][pattern]")
{
	// A pattern is user input — "--result out.<frame:zz>.png" — and lain::string cannot log, so
	// the refusal is the return type. Throwing out of a string helper would take down the caller
	// on a typo; rendering the token literally would write every frame to one filename.
	Dictionary values;
	values.set("frame", 7);
	CHECK_FALSE(Pattern{"out.<frame:zz>.png"}.format(values).has_value());

	// An unmapped key with the same bad spec is NOT a refusal: nothing asked fmt about it.
	CHECK(*Pattern{"out.<other:zz>.png"}.format(Dictionary{}) == "out.<other:zz>.png");
}

TEST_CASE("only a well-formed token is a key; anything else is literal", "[string][pattern]")
{
	CHECK(Pattern{"<frame>"}.keys() == std::vector<std::string>{"frame"});
	CHECK(Pattern{"<frame:04>"}.keys() == std::vector<std::string>{"frame"});

	// The escape, and it needs no escape character: a name cannot hold a space, a digit cannot
	// start one, and an unterminated '<' closes nothing.
	CHECK(Pattern{"<3 files>"}.keys().empty());
	CHECK(Pattern{"a < b > c"}.keys().empty());
	CHECK(Pattern{"<9lives>"}.keys().empty());
	CHECK(Pattern{"unterminated <frame"}.keys().empty());
	CHECK(*Pattern{"<3 files>"}.format(Dictionary{}) == "<3 files>");

	CHECK(Pattern{"shot.<frame:04>.png"}.has("frame"));
	CHECK_FALSE(Pattern{"shot.<frame:04>.png"}.has("take"));
	CHECK_FALSE(Pattern{"shot.0007.png"}.has("frame"));
}

TEST_CASE("match is anchored at both ends and every key is non-empty", "[string][pattern]")
{
	const Pattern pattern{"shot.<frame:04>.png"};

	CHECK(pattern.match("shot.0007.png")->at("frame") == "0007");
	CHECK(pattern.match("shot.7.png")->at("frame") == "7");

	// The spec is NOT a filter — what a capture means belongs to the caller, which has to parse
	// it anyway. io::image is what rejects a non-numeric one.
	CHECK(pattern.match("shot.x.png")->at("frame") == "x");

	CHECK_FALSE(pattern.match("other.0007.png").has_value());	 // wrong prefix
	CHECK_FALSE(pattern.match("shot.0007.exr").has_value());	 // wrong suffix
	CHECK_FALSE(pattern.match("shot..png").has_value());		 // a key captures at least one
	CHECK_FALSE(pattern.match("shot.0007.png.bak").has_value()); // nothing may be left over

	// A pattern with no keys names exactly itself.
	const Pattern fixed{"out.png"};
	CHECK(fixed.match("out.png").has_value());
	CHECK(fixed.match("out.png")->empty());
	CHECK_FALSE(fixed.match("out.0001.png").has_value());
}

TEST_CASE("several keys, and a repeated one must capture the same text", "[string][pattern]")
{
	const Pattern pattern{"<take>/shot.<frame:04>.png"};
	const std::optional<Captures> captures = pattern.match("take03/shot.0007.png");
	REQUIRE(captures.has_value());
	CHECK(captures->at("take") == "take03");
	CHECK(captures->at("frame") == "0007");

	// One name, one capture: naming a key twice asserts the two runs are the same text, so a
	// candidate where they differ does not match rather than quietly keeping one of them.
	const Pattern doubled{"<name>_<name>.png"};
	REQUIRE(doubled.match("ab_ab.png").has_value());
	CHECK(doubled.match("ab_ab.png")->at("name") == "ab");
	CHECK_FALSE(doubled.match("ab_cd.png").has_value());
}

TEST_CASE("what format writes, match reads back", "[string][pattern]")
{
	// The inverse property, which is the reason both directions live in one type. io::NumberField
	// held it by being one function; a grammar shared between a writer and a reader holds it only
	// if something asserts it.
	const Pattern pattern{"out.<frame:04>.png"};
	for (const int frame : {0, 7, 42, 999, 1000, 12345})
	{
		Dictionary values;
		values.set("frame", frame);

		const std::optional<std::string> name = pattern.format(values);
		REQUIRE(name.has_value());

		const std::optional<Captures> captures = pattern.match(*name);
		REQUIRE(captures.has_value());
		CHECK(std::stoi(captures->at("frame")) == frame);
	}
}
