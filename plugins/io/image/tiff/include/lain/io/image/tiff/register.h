#pragma once

namespace lain::io::image::tiff
{
	// Register this plugin's TIFF reader into the io::image reader registry (under "tiff"
	// and "tif"). Called once by the generated registerImageCodecs() aggregator — or
	// directly by a consumer that wants only TIFF. The codec attaches through this seam;
	// nothing in the io::image core names libtiff.
	void registerCodec();
} // namespace lain::io::image::tiff
