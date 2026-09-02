#pragma once

// This translation unit's one export; the JPEG writer stays private to jpegwriter.cpp, for the same
// reason its reader does.

namespace lain::io::image::jpeg
{
	// Register the JPEG writer into the io::image writer registry (under "jpg" and "jpeg"). Called by
	// registerCodec (register.cpp), which registers this plugin's reader and writer together.
	void registerJpegWriter();
} // namespace lain::io::image::jpeg
