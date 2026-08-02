// Unit tests for lain::core::Uuid. Pure std, no driver. Covers v7 generation (shape + uniqueness),
// the deliberately liberal parse, canonical formatting, ordering and hashing.

#include "lain/core/uuid.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <unordered_set>

using lain::core::Uuid;

TEST_CASE("a default-constructed uuid is nil", "[uuid]")
{
	const Uuid nil;
	REQUIRE(nil.isNil());
	REQUIRE_FALSE(static_cast<bool>(nil));
	REQUIRE(nil.toString() == "00000000-0000-0000-0000-000000000000");
	REQUIRE(nil == Uuid{});
}

TEST_CASE("generate mints distinct, non-nil version-7 ids", "[uuid]")
{
	const Uuid a = Uuid::generate();
	REQUIRE_FALSE(a.isNil());
	REQUIRE(static_cast<bool>(a));

	// Version 7 in the high nibble of byte 6, variant 0b10 in the top bits of byte 8.
	REQUIRE((a.bytes()[6] >> 4) == 0x7);
	REQUIRE((a.bytes()[8] & 0xc0) == 0x80);

	// Uniqueness across a burst: many land in the same millisecond, so this exercises the random
	// tail rather than the timestamp.
	std::set<Uuid> minted;
	for (int i = 0; i < 1000; ++i)
		minted.insert(Uuid::generate());
	REQUIRE(minted.size() == 1000);
}

TEST_CASE("toString is canonical lowercase 8-4-4-4-12", "[uuid]")
{
	const auto id = Uuid::parse("0123456789ABCDEF0123456789ABCDEF");
	REQUIRE(id);
	REQUIRE(id->toString() == "01234567-89ab-cdef-0123-456789abcdef");
	REQUIRE(id->toString().size() == 36);
}

TEST_CASE("parse accepts any well-formed uuid", "[uuid]")
{
	// The canonical form, an uppercase v4 (what `uuidgen` prints), and the unhyphenated form all
	// name the same value — the id is opaque to us, so no version or variant nibble is policed.
	const auto canonical = Uuid::parse("019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e55");
	const auto upper = Uuid::parse("019FBAFB-2C31-7B8E-9A4F-1D6C0F2A8E55");
	const auto bare = Uuid::parse("019fbafb2c317b8e9a4f1d6c0f2a8e55");
	REQUIRE(canonical);
	REQUIRE(upper);
	REQUIRE(bare);
	REQUIRE(*canonical == *upper);
	REQUIRE(*canonical == *bare);

	// A v4 id (version nibble 4) parses as readily as a v7 one.
	const auto v4 = Uuid::parse("f81d4fae-7dec-41d0-a765-00a0c91e6bf6");
	REQUIRE(v4);
	REQUIRE((v4->bytes()[6] >> 4) == 0x4);
}

TEST_CASE("the nil uuid parses, to the nil value", "[uuid]")
{
	const auto nil = Uuid::parse("00000000-0000-0000-0000-000000000000");
	REQUIRE(nil);
	REQUIRE(nil->isNil()); // a caller treating nil as "missing" checks the value, not the parse
}

TEST_CASE("parse rejects malformed text", "[uuid]")
{
	REQUIRE_FALSE(Uuid::parse(""));
	REQUIRE_FALSE(Uuid::parse("019fbafb"));										 // too short
	REQUIRE_FALSE(Uuid::parse("019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e55f"));		 // too long
	REQUIRE_FALSE(Uuid::parse("019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e5g"));			 // 'g' is not hex
	REQUIRE_FALSE(Uuid::parse("{019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e55}"));		 // brace form unsupported
	REQUIRE_FALSE(Uuid::parse("urn:uuid:019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e55")); // urn form unsupported
	REQUIRE_FALSE(Uuid::parse(" 019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e55"));		 // no whitespace trimming
}

TEST_CASE("parse and toString round-trip", "[uuid]")
{
	const Uuid minted = Uuid::generate();
	const auto reparsed = Uuid::parse(minted.toString());
	REQUIRE(reparsed);
	REQUIRE(*reparsed == minted);
	REQUIRE(reparsed->toString() == minted.toString());
}

TEST_CASE("shortString truncates for display, keeping the distinguishing end", "[uuid]")
{
	const auto id = Uuid::parse("019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e55");
	REQUIRE(id);
	REQUIRE(id->shortString() == "...0f2a8e55");

	// Why the tail: a v7 id leads with a millisecond timestamp, so ids minted together share their
	// first characters. A prefix would render a whole graph's nodes identically.
	const Uuid a = Uuid::generate();
	const Uuid b = Uuid::generate();
	REQUIRE(a.shortString() != b.shortString());
}

TEST_CASE("ordering is a big-endian byte compare", "[uuid]")
{
	const auto low = Uuid::parse("00000000-0000-0000-0000-000000000001");
	const auto high = Uuid::parse("10000000-0000-0000-0000-000000000000");
	REQUIRE(low);
	REQUIRE(high);
	REQUIRE(*low < *high); // the LEADING bytes dominate
	REQUIRE_FALSE(*high < *low);
	REQUIRE(Uuid{} < *low);		// nil sorts first
	REQUIRE_FALSE(*low < *low); // irreflexive — a strict weak ordering, as std::map needs
}

TEST_CASE("uuids hash for unordered containers", "[uuid]")
{
	std::unordered_set<Uuid> seen;
	for (int i = 0; i < 500; ++i)
		seen.insert(Uuid::generate());
	REQUIRE(seen.size() == 500);

	// Equal values hash equally (the contract std::unordered_* relies on).
	const auto a = Uuid::parse("019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e55");
	const auto b = Uuid::parse("019FBAFB2C317B8E9A4F1D6C0F2A8E55");
	REQUIRE(a);
	REQUIRE(b);
	REQUIRE(std::hash<Uuid>{}(*a) == std::hash<Uuid>{}(*b));
}
