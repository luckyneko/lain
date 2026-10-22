#include "lain/image/procedural.h"

#include "lain/image/color.h"
#include "lain/image/imageview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Moved verbatim from libs/camera/test/texturedscene.h, where M9's synthetic scene still samples it per
// ray: every expression is the one it was, so that scene draws the same footage byte for byte.
namespace lain::image
{
	static std::uint64_t mix(std::uint64_t x)
	{
		x += 0x9e3779b97f4a7c15ull;
		x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
		x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
		return x ^ (x >> 31);
	}

	// A value in [0, 1) for lattice point (i, j).
	static double lattice(std::uint64_t seed, std::int64_t i, std::int64_t j)
	{
		const std::uint64_t key = mix(seed ^ mix(std::uint64_t(i) * 0x632be59bd9b4e019ull ^ std::uint64_t(j) * 0x8cb92ba72f3d8dd7ull));
		return double(key >> 11) * 0x1.0p-53;
	}

	static double valueNoise(std::uint64_t seed, double x, double y)
	{
		const double fi = std::floor(x), fj = std::floor(y);
		const auto i = std::int64_t(fi), j = std::int64_t(fj);
		const double fx = x - fi, fy = y - fj;
		const double sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
		const double top = lattice(seed, i, j) * (1 - sx) + lattice(seed, i + 1, j) * sx;
		const double bottom = lattice(seed, i, j + 1) * (1 - sx) + lattice(seed, i + 1, j + 1) * sx;
		return top * (1 - sy) + bottom * sy;
	}

	double proceduralGrey(const ProceduralTexture& t, double u, double v)
	{
		// Four octaves, the coarser ones stronger.
		double value = 0;
		double weight = 0;
		for (int o = 0; o < 4; ++o)
		{
			const double size = t.cell * double(1 << o);
			const double w = double(1 << o);
			value += w * valueNoise(t.seed + std::uint64_t(o), u / size, v / size);
			weight += w;
		}
		value /= weight;
		// A soft disc in each blob cell, bright or dark, inside its own cell.
		const double blob = 5 * t.cell;
		const double ci = std::floor(u / blob), cj = std::floor(v / blob);
		const auto i = std::int64_t(ci), j = std::int64_t(cj);
		const std::uint64_t s = t.seed ^ 0xb10b5ull;
		const double cx = (ci + 0.3 + 0.4 * lattice(s, i, j)) * blob;
		const double cy = (cj + 0.3 + 0.4 * lattice(s + 1, i, j)) * blob;
		const double radius = (0.08 + 0.12 * lattice(s + 2, i, j)) * blob;
		const double tone = lattice(s + 3, i, j) < 0.5 ? 0.08 : 0.92;
		const double d = std::hypot(u - cx, v - cy);
		const double edge = std::clamp((radius - d) / (0.25 * radius) + 0.5, 0.0, 1.0);
		value = value * (1 - edge) + tone * edge;
		return 255.0 * std::clamp(value, 0.0, 1.0);
	}

	Image bakeProceduralTexture(const ProceduralTexture& texture, int width, int height, double texelsPerMetre)
	{
		Image baked(width, height, PixelFormat::Gray32F, ColorSpace::Linear);
		auto texels = baked.as<ColorGrayf>();
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const double grey = proceduralGrey(texture, (x + 0.5) / texelsPerMetre, (y + 0.5) / texelsPerMetre);
				texels(x, y) = ColorGrayf(float(grey / 255.0));
			}
		}
		return baked;
	}
} // namespace lain::image
