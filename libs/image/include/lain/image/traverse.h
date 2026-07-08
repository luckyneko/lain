#pragma once

#include "lain/image/color.h"	  // Color typedefs (the visit dispatch table) + Image::as<C>
#include "lain/image/imageview.h" // ImageView / ConstImageView

#include <algorithm>
#include <cstddef>

// The traversal layer: free-function algorithms over the typed views — the generic surface
// the operation catalog (brightness, convolution, ...) is written against. Kept apart from
// the view *types* (imageview.h) so a consumer that only needs a view doesn't pull the algorithms.
namespace lain::image
{
	// Apply fn(C&) to every pixel of a view, in place (row-major).
	template <typename C, typename F>
	void forEach(ImageView<C> view, F&& fn)
	{
		for (int y = 0; y < view.height(); ++y)
		{
			C* r = view.row(y);
			for (int x = 0; x < view.width(); ++x)
				fn(r[x]);
		}
	}

	// Write dst(x,y) = fn(src(x,y)) over the overlapping extent. src / dst may be any view
	// types (mutable or const, and even different Color types for a converting map), so
	// there is nothing to specify — the pixel types are deduced from the views.
	template <typename SrcView, typename DstView, typename F>
	void transform(const SrcView& src, DstView dst, F&& fn)
	{
		const int w = std::min(src.width(), dst.width());
		const int h = std::min(src.height(), dst.height());
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
				dst(x, y) = fn(src(x, y));
		}
	}

	// Tile a view into blockW x blockH windows (clamped at the right/bottom edges) and call
	// fn(ImageView<C> block, int x0, int y0) for each. The building block for per-block ops.
	template <typename C, typename F>
	void forEachBlock(ImageView<C> view, int blockW, int blockH, F&& fn)
	{
		for (int y = 0; y < view.height(); y += blockH)
		{
			for (int x = 0; x < view.width(); x += blockW)
			{
				const int w = std::min(blockW, view.width() - x);
				const int h = std::min(blockH, view.height() - y);
				fn(view.subview(x, y, w, h), x, y);
			}
		}
	}

	// Dispatch a runtime PixelFormat to the matching Color view and call fn with it — so an
	// op is written once as a generic lambda (fn(auto view)) and runs on every format. fn
	// receives an ImageView<C> (mutable) or ConstImageView<C> (const overload). Defined in
	// traverse.inl over the private format -> Color table.
	template <typename F>
	void visit(Image& img, F&& fn);
	template <typename F>
	void visit(const Image& img, F&& fn);
} // namespace lain::image

#include "lain/image/details/traverse.inl"
