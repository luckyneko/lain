#pragma once

namespace lain::io::image::png
{
	// Register this plugin's PNG reader into the io::image reader registry (under "png").
	// Called once by the generated registerImageCodecs() aggregator — or directly by a
	// consumer that wants only PNG. The codec attaches through this seam; nothing in the
	// io::image core names libpng.
	void registerCodec();
} // namespace lain::io::image::png
