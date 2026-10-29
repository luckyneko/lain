#include "resampling.h"

#include "unionfind.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace lain::camera::detail
{
	std::vector<bool> holdOut(std::size_t count, double fraction, std::size_t minimum,
							  const std::function<bool(std::size_t)>& mayHold)
	{
		std::vector<bool> held(count, false);
		if (!(fraction > 0) || count < minimum)
			return held;
		const std::size_t stride = std::max<std::size_t>(2, std::size_t(std::lround(1.0 / fraction)));
		for (std::size_t u = stride / 2; u < count; u += stride)
			held[u] = mayHold(u);
		return held;
	}

	std::vector<bool> holdOut(std::size_t cameras, const std::vector<std::vector<std::uint32_t>>& unitCameras,
							  double fraction, std::size_t minimum)
	{
		if (!(fraction > 0) || unitCameras.size() < minimum)
			return std::vector<bool>(unitCameras.size(), false);

		std::vector<std::uint32_t> shared(cameras * cameras, 0);
		const auto pairsOf = [&](std::size_t u)
		{
			std::vector<std::size_t> pairs;
			const std::vector<std::uint32_t>& seen = unitCameras[u];
			for (std::size_t i = 0; i < seen.size(); ++i)
			{
				for (std::size_t j = i + 1; j < seen.size(); ++j)
					pairs.push_back(std::min(seen[i], seen[j]) * cameras + std::max(seen[i], seen[j]));
			}
			return pairs;
		};
		for (std::size_t u = 0; u < unitCameras.size(); ++u)
		{
			for (const std::size_t pair : pairsOf(u))
				++shared[pair];
		}
		const auto componentCount = [&]()
		{
			std::vector<std::uint32_t> parent(cameras);
			std::iota(parent.begin(), parent.end(), 0u);
			std::size_t components = cameras;
			for (std::size_t pair = 0; pair < shared.size(); ++pair)
			{
				if (shared[pair] == 0)
					continue;
				const std::uint32_t a = root(parent, std::uint32_t(pair / cameras));
				const std::uint32_t b = root(parent, std::uint32_t(pair % cameras));
				if (a != b)
				{
					parent[a] = b;
					--components;
				}
			}
			return components;
		};
		const std::size_t components = componentCount();
		return holdOut(unitCameras.size(), fraction, minimum,
					   [&](std::size_t u)
					   {
						   const std::vector<std::size_t> pairs = pairsOf(u);
						   bool cuts = false;
						   for (const std::size_t pair : pairs)
						   {
							   if (--shared[pair] == 0)
								   cuts = true;
						   }
						   if (cuts && componentCount() > components)
						   {
							   for (const std::size_t pair : pairs)
								   ++shared[pair];
							   return false;
						   }
						   return true;
					   });
	}

	std::vector<std::vector<std::uint32_t>> bootstrapDraws(const std::vector<std::uint32_t>& units,
														   std::uint32_t resamples, std::uint64_t seed)
	{
		std::mt19937_64 engine(seed);
		std::vector<std::vector<std::uint32_t>> draws(resamples);
		for (std::vector<std::uint32_t>& draw : draws)
		{
			for (std::size_t i = 0; i < units.size(); ++i)
				draw.push_back(units[std::size_t(engine() % units.size())]);
		}
		return draws;
	}
} // namespace lain::camera::detail
