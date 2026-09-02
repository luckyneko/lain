#include "lain/io/image/jpeg/register.h"

#include "jpegreader.h"
#include "jpegwriter.h"

namespace lain::io::image::jpeg
{
	void registerCodec()
	{
		// The plugin's whole attachment surface: its reader and its writer arrive together, so a
		// build that can open a JPEG can also write one. Each half owns the keys it claims, in the
		// translation unit that implements it — nothing here names stb.
		registerJpegReader();
		registerJpegWriter();
	}
} // namespace lain::io::image::jpeg
