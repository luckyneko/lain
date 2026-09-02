#pragma once

// This translation unit's one export. The PNG reader itself stays private to pngreader.cpp — it is
// named nowhere else, and a class in a header that nothing constructs is a wider surface than the
// plugin has any use for. What crosses the boundary is the registration, so that is what is
// declared here.

namespace lain::io::image::png
{
	// Register the PNG reader into the io::image reader registry (under "png"). Called by
	// registerCodec (register.cpp), which registers this plugin's reader and writer together.
	void registerPngReader();
} // namespace lain::io::image::png
