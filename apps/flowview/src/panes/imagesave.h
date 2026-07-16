#pragma once

#include "../pinkey.h"

namespace lain::image
{
	class Image;
}

namespace flowview
{
	struct AppContext;

	// The format dropdown + Save… widget for one image, shared by the Inspector's output ports and
	// the Interface panel's outputs. `key` scopes the per-pin remembered format choice (in
	// AppContext::saveFormat). Draws inline (no window of its own).
	void renderImageSave(AppContext& ctx, const PinKey& key, const lain::image::Image& img);
} // namespace flowview
