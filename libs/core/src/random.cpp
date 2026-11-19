#include "lain/core/random.h"

namespace lain::core
{
	double standardNormal(std::uint64_t seed, std::uint64_t sample)
	{
		// Twelve 32-bit uniforms, each (u + 0.5) / 2^32 so its mean is exactly 1/2, summed exactly: the
		// integer sum is below 12 * 2^32, a double holds it, and the scaling is by a power of two.
		// Every operation below is exact, so this file needs no -ffp-contract=off: a fused multiply-add
		// of the last two steps rounds to the same value as the two steps one at a time.
		std::uint64_t sum = 0;
		for (std::uint64_t j = 1; j <= 6; ++j)
		{
			const std::uint64_t bits = splitMix64(seed, 6 * sample + j);
			sum += bits >> 32;
			sum += bits & 0xFFFFFFFFull;
		}
		return (double(sum) + 6.0) * 0x1.0p-32 - 6.0;
	}
} // namespace lain::core
