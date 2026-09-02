#pragma once

// This translation unit's one export; the PNG writer stays private to pngwriter.cpp, for the same
// reason its reader does.

namespace lain::io::image::png
{
	// Register the PNG writer into the io::image writer registry (under "png"). Called by
	// registerCodec (register.cpp), which registers this plugin's reader and writer together.
	void registerPngWriter();
} // namespace lain::io::image::png
