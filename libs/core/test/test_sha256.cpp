// Unit tests for lain::core::Sha256, against the FIPS 180-4 example vectors and the padding edges.

#include "lain/core/sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <string>

using namespace lain::core;

TEST_CASE("sha256 matches the FIPS 180-4 example vectors", "[sha256]")
{
	REQUIRE(sha256("").toString() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	REQUIRE(sha256("abc").toString() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	// 56 bytes: the padding cannot fit the length in this block, so it spills into a second one.
	REQUIRE(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").toString() ==
			"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	REQUIRE(sha256("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")
				.toString() == "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("sha256 is right on both sides of the padding boundary", "[sha256]")
{
	// 55 bytes is the longest message whose padding fits one block; 64 is exactly one block.
	REQUIRE(sha256(std::string(55, 'a')).toString() == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
	REQUIRE(sha256(std::string(64, 'a')).toString() == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST_CASE("a hash fed in pieces equals the hash of the whole", "[sha256]")
{
	// One million 'a's, the FIPS long-message vector, fed in pieces whose sizes straddle every block
	// edge. A chunking bug shows as a digest that depends on how the bytes arrived.
	const std::string million(1000000, 'a');
	const std::string expected = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
	REQUIRE(sha256(million).toString() == expected);

	for (const std::size_t piece : {std::size_t{1}, std::size_t{55}, std::size_t{56}, std::size_t{63}, std::size_t{64},
									std::size_t{65}, std::size_t{1000}})
	{
		Sha256 hash;
		for (std::size_t at = 0; at < million.size(); at += piece)
			hash.update(million.data() + at, std::min(piece, million.size() - at));
		INFO("piece size " << piece);
		REQUIRE(hash.finish().toString() == expected);
	}
}

TEST_CASE("finish resets the hash, so one object hashes several things", "[sha256]")
{
	Sha256 hash;
	hash.update("abc");
	REQUIRE(hash.finish() == sha256("abc"));
	REQUIRE(hash.finish() == sha256(""));
	hash.update("a");
	hash.update("bc");
	REQUIRE(hash.finish() == sha256("abc"));
	REQUIRE(sha256("abc") != sha256("abd"));
}
