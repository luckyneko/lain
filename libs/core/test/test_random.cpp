// Unit tests for lain::core::random.h: splitMix64 against SplitMix64's published sequence,
// standardNormal's draws pinned outside lain, their exactness, and that they are a standard normal,
// independent of one another, and seedOf against SHA-256's published digests.

#include "lain/core/random.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using lain::core::seedOf;
using lain::core::splitMix64;
using lain::core::standardNormal;

TEST_CASE("splitMix64 is SplitMix64's published sequence", "[random]")
{
	// The outputs of Vigna's stepped generator (splitmix64.c) from seeds 0 and 1234567, which other
	// ports quote as their test vectors; recomputed by Python's integer arithmetic. The counter form
	// gives output n without stepping, so these also check that its state after n steps is
	// seed + n * gamma. A constexpr variable, so the function is checked usable at compile time.
	constexpr std::uint64_t first = splitMix64(0, 1);
	REQUIRE(first == 0xe220a8397b1dcdafull);
	REQUIRE(splitMix64(0, 2) == 0x6e789e6aa1b965f4ull);
	REQUIRE(splitMix64(0, 3) == 0x06c45d188009454full);

	REQUIRE(splitMix64(1234567, 1) == 6457827717110365317ull);
	REQUIRE(splitMix64(1234567, 2) == 3203168211198807973ull);
	REQUIRE(splitMix64(1234567, 3) == 9817491932198370423ull);
	REQUIRE(splitMix64(1234567, 4) == 4593380528125082431ull);
	REQUIRE(splitMix64(1234567, 5) == 16408922859458223821ull);
}

// The seed feature/M15's rigplan derives for NoiseKey{7, kCamera, 3}; its test_sensor.cpp pins these
// same four draws, so when rigplan's own copy gives way to this one, its pins hold unchanged.
static constexpr std::uint64_t kSeed = 0x6fb29c25566225f6ull;

TEST_CASE("standardNormal is pinned outside lain", "[random]")
{
	// Computed by Python's integer arithmetic from the SplitMix64 / Irwin-Hall rule random.h states, so
	// the draws are the same on every platform and any change to them is a change to that rule.
	REQUIRE(standardNormal(kSeed, 0) == -0x1.0fe770d3p+0);
	REQUIRE(standardNormal(kSeed, 1) == -0x1.69098e44p-1);
	REQUIRE(standardNormal(kSeed, 2) == 0x1.2ca9bffcp-2);
	REQUIRE(standardNormal(kSeed, 1000000) == 0x1.d57746ecp-1);
}

TEST_CASE("standardNormal draws are exact", "[random]")
{
	// Each is a whole number of 2^-32 within six of 0: the integer sum, scaled by a power of two.
	for (std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{1}, kSeed})
	{
		for (std::uint64_t sample = 0; sample < 10000; ++sample)
		{
			const double z = standardNormal(seed, sample);
			const double units = z * 0x1.0p32;
			REQUIRE(units == std::floor(units));
			REQUIRE(std::abs(z) <= 6.0);
		}
	}
}

static double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
	double ma = 0, mb = 0;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		ma += a[i];
		mb += b[i];
	}
	ma /= double(a.size());
	mb /= double(b.size());
	double sab = 0, saa = 0, sbb = 0;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		sab += (a[i] - ma) * (b[i] - mb);
		saa += (a[i] - ma) * (a[i] - ma);
		sbb += (b[i] - mb) * (b[i] - mb);
	}
	return sab / std::sqrt(saa * sbb);
}

TEST_CASE("standardNormal draws are a standard normal, independent of one another", "[random]")
{
	const std::size_t n = 200000;
	std::vector<double> z(n + 1);
	double mean = 0, peak = 0;
	for (std::size_t i = 0; i <= n; ++i)
	{
		z[i] = standardNormal(kSeed, i);
		mean += z[i];
		peak = std::max(peak, std::abs(z[i]));
	}
	mean /= double(n + 1);
	double variance = 0;
	for (double v : z)
		variance += (v - mean) * (v - mean);
	variance /= double(n);

	// A mean of 200001 draws has a standard deviation of 0.0022, and their variance one of 0.0032.
	REQUIRE(std::abs(mean) < 0.011);
	REQUIRE(std::abs(variance - 1.0) < 0.016);
	REQUIRE(peak <= 6.0); // Irwin-Hall's bound

	// Neighbouring samples, and the same sample of another seed, are unrelated: a correlation of
	// independent draws has a standard deviation of 1 / sqrt(n), 0.0022.
	const std::vector<double> here(z.begin(), z.end() - 1);
	const std::vector<double> next(z.begin() + 1, z.end());
	REQUIRE(std::abs(correlation(here, next)) < 0.011);

	std::vector<double> otherSeed(n);
	for (std::size_t i = 0; i < n; ++i)
		otherSeed[i] = standardNormal(kSeed + 1, i);
	REQUIRE(std::abs(correlation(here, otherSeed)) < 0.011);
}

TEST_CASE("seedOf is the first eight bytes of a text's SHA-256, big-endian", "[random]")
{
	// FIPS 180-4's own examples, "abc" and the empty message, whose digests begin ba7816bf 8f01cfea and
	// e3b0c442 98fc1c14; recomputed by Python's hashlib.
	REQUIRE(seedOf("abc") == 0xba7816bf8f01cfeaull);
	REQUIRE(seedOf("") == 0xe3b0c44298fc1c14ull);
	// Every byte of the text counts, its line ends included.
	REQUIRE(seedOf("lain-rigplan-noise/1\n7\n") != seedOf("lain-rigplan-noise/1\n7"));
}
