#include "lain/io/image/tiff/register.h"

#include "tiffreader.h"
#include "tiffwriter.h"

namespace lain::io::image::tiff
{
	void registerCodec()
	{
		// The plugin's whole attachment surface: its reader and its writer arrive together, so a
		// build that can open a TIFF can also write one. Each half owns the keys it claims, in the
		// translation unit that implements it — nothing here names libtiff.
		registerTiffReader();
		registerTiffWriter();
	}
} // namespace lain::io::image::tiff
