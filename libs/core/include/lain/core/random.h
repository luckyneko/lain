#pragma once

#include <cstdint>
#include <string_view>

namespace lain::core
{
	// SplitMix64's output `n` (1-based) from `seed`, computed directly rather than stepped: its state
	// after n steps is seed + n * 0x9E3779B97F4A7C15, mixed. So a draw depends on nothing but its seed
	// and counter, and two threads can draw any outputs of one stream without sharing state. With
	// n = 1 it is SplitMix64's one-step mix of `seed`, which is how a seed is scrambled into another.
	// Exact integer arithmetic, so the same bits on every platform; and constexpr, as the body is
	// pure arithmetic.
	constexpr std::uint64_t splitMix64(std::uint64_t seed, std::uint64_t n)
	{
		std::uint64_t z = seed + n * 0x9E3779B97F4A7C15ull;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	// The standard normal draw for sample `sample` of the stream `seed`: the sum of twelve uniform draws,
	// less six (Irwin-Hall), each a half of one of splitMix64's outputs 6 * sample + 1 to 6 * sample + 6
	// from `seed`. Counter-based, so a draw depends on nothing but its seed and sample, and the sum is
	// taken in integers, so the draw is exact: mean exactly 0, variance 1, within six of 0, and the same
	// bits on every platform.
	//
	// Use this wherever noise must be the same on every machine, which is every test that draws it:
	// std::normal_distribution is implementation-defined, and Box-Muller goes through libm's log and
	// cos, which are not correctly rounded and so differ by platform.
	//
	// A seed is used as given. Two seeds that differ by k times SplitMix64's increment, for a small k,
	// share uniforms k outputs apart, so their draws are related. No difference of a few small integers
	// is such a multiple, but a key with many or large fields is safest hashed into a seed (seedOf)
	// rather than added up.
	double standardNormal(std::uint64_t seed, std::uint64_t sample);

	// A seed for a structured key: the first eight bytes of SHA-256 over `text`, read big-endian. The
	// caller spells the key: one field to a line, under a versioned name ("lain-rigplan-noise/1"), so a
	// change to how a key is spelt is a change of name rather than a silent re-roll of everything drawn
	// from it. The same bits on every platform.
	std::uint64_t seedOf(std::string_view text);
} // namespace lain::core
