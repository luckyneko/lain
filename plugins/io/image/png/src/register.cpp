#include "lain/io/image/png/register.h"

#include "pngreader.h"
#include "pngwriter.h"

namespace lain::io::image::png
{
	void registerCodec()
	{
		// The plugin's whole attachment surface: its reader and its writer arrive together, so a
		// build that can open a PNG can also write one. Each half owns the keys it claims, in the
		// translation unit that implements it — nothing here names libpng.
		registerPngReader();
		registerPngWriter();
	}
} // namespace lain::io::image::png
