#pragma once

namespace lain::io::image::jpeg
{
	// Register this plugin's JPEG reader into the io::image reader registry (under "jpg"
	// and "jpeg"). Called once by the generated registerImageCodecs() aggregator — or
	// directly by a consumer that wants only JPEG. The codec attaches through this seam;
	// nothing in the io::image core names stb.
	void registerCodec();
} // namespace lain::io::image::jpeg
