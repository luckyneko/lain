// What storing a texture costs in memory (renderer.h, storedTexture), measured. This executable replaces
// the global aligned operator new and delete, which every image's lain::memory::Buffer goes through, and
// counts the bytes they hold at once. It is an executable of its own because a replacement is the whole
// program's.
//
// The point (WORK.md M15, *the 20 m room's footprint*): widened to RGBA32F before its curve was undone, an
// 8192 x 8192 grey bake held 36 bytes a texel at once, and a room's bakes, run together on the pool,
// peaked near 7 GB that the allocator kept after they were freed.

#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/render/renderer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <new>
#include <unordered_map>

// The aligned bytes held now, and the most held at once since the count was last reset.
struct AlignedCount
{
	std::mutex mutex;
	std::unordered_map<void*, std::size_t> sizes; // its own nodes come from the plain new, uncounted
	std::size_t live = 0;
	std::size_t peak = 0;
};

static AlignedCount& alignedCount()
{
	static AlignedCount count;
	return count;
}

static void* alignedAllocate(std::size_t size, std::align_val_t align)
{
	const std::size_t alignment = std::max(std::size_t(align), sizeof(void*));
#ifdef _WIN32
	void* p = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
	void* p = nullptr;
	if (posix_memalign(&p, alignment, size == 0 ? 1 : size) != 0)
		p = nullptr;
#endif
	if (!p)
		throw std::bad_alloc();
	AlignedCount& count = alignedCount();
	const std::lock_guard<std::mutex> lock(count.mutex);
	count.sizes[p] = size;
	count.live += size;
	count.peak = std::max(count.peak, count.live);
	return p;
}

static void alignedFree(void* p) noexcept
{
	if (!p)
		return;
	{
		AlignedCount& count = alignedCount();
		const std::lock_guard<std::mutex> lock(count.mutex);
		const auto found = count.sizes.find(p);
		if (found != count.sizes.end())
		{
			count.live -= found->second;
			count.sizes.erase(found);
		}
	}
#ifdef _WIN32
	_aligned_free(p);
#else
	std::free(p);
#endif
}

void* operator new(std::size_t size, std::align_val_t align)
{
	return alignedAllocate(size, align);
}

void operator delete(void* p, std::align_val_t) noexcept
{
	alignedFree(p);
}

void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
	alignedFree(p);
}

// The most aligned bytes held at once while `work` ran, beyond what was held when it began.
template <typename F>
static std::size_t peakDuring(F&& work)
{
	AlignedCount& count = alignedCount();
	std::size_t base = 0;
	{
		const std::lock_guard<std::mutex> lock(count.mutex);
		base = count.live;
		count.peak = count.live;
	}
	work();
	const std::lock_guard<std::mutex> lock(count.mutex);
	return count.peak - base;
}

TEST_CASE("storing a texture holds one float copy of its own channels and the result, beside it", "[render][memory]")
{
	using lain::image::Image;
	using lain::image::PixelFormat;
	const int side = 512;
	const std::size_t texels = std::size_t(side) * side;

	// A grey bake, as a procedural surface makes one: its sRGB copy in float (4 bytes a texel) and the
	// RGBA8 result (4), where widening to RGBA32F first held two of 16 and the result.
	const Image bake(side, side, PixelFormat::Gray32F, lain::image::ColorSpace::Linear);
	Image stored;
	const std::size_t grey = peakDuring([&]
										{ stored = lain::render::storedTexture(bake); });
	CHECK(stored.pixelFormat() == PixelFormat::RGBA8);
	CHECK(grey >= 4 * texels); // the result at least: the count sees the conversion
	CHECK(grey <= 8 * texels);

	// An 8-bit grey picture: widened to Gray32F (4) and its sRGB copy (4), then the copy and the result.
	const Image picture(side, side, PixelFormat::Gray8, lain::image::ColorSpace::Linear);
	CHECK(peakDuring([&]
					 { stored = lain::render::storedTexture(picture); }) <= 8 * texels);

	// An RGB picture: its own three channels in float, twice (12 each), never four.
	const Image colour(side, side, PixelFormat::RGB16, lain::image::ColorSpace::BT709);
	CHECK(peakDuring([&]
					 { stored = lain::render::storedTexture(colour); }) <= 24 * texels);
}
