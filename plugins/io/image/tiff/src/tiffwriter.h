#pragma once

// This translation unit's one export; the TIFF writer stays private to tiffwriter.cpp, for the same
// reason its reader does.

namespace lain::io::image::tiff
{
	// Register the TIFF writer into the io::image writer registry (under "tiff" and "tif"). Called by
	// registerCodec (register.cpp), which registers this plugin's reader and writer together.
	void registerTiffWriter();
} // namespace lain::io::image::tiff
