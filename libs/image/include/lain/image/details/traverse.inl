#pragma once

// Definitions for image::visit (see traverse.h). The format -> Color table it dispatches
// over is ColorTypeList, declared with the Color family it enumerates, in color.h.

#include <cstddef>

namespace lain::image
{
	template <typename F>
	void visit(Image& img, F&& fn)
	{
		ColorTypeList::visitAt(static_cast<std::size_t>(img.pixelFormat()),
							   [&](auto tag)
							   { fn(img.as<typename decltype(tag)::type>()); });
	}

	template <typename F>
	void visit(const Image& img, F&& fn)
	{
		ColorTypeList::visitAt(static_cast<std::size_t>(img.pixelFormat()),
							   [&](auto tag)
							   { fn(img.as<typename decltype(tag)::type>()); });
	}
} // namespace lain::image
