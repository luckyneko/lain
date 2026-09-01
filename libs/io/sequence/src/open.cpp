#include "lain/io/sequence/open.h"

#include <lain/io/image/sequence.h>

namespace lain::io::sequence
{
	std::optional<lain::media::FrameSequence> open(std::string_view uri, lain::media::FrameRate rate)
	{
		// One medium. Slice 5 replaces this body with the extension-keyed registry (and the
		// "no opener for .mp4" diagnostic that goes with it) when io::video arrives; no caller
		// changes, which is the whole reason this indirection exists before it has a choice to make.
		return lain::io::image::openSequence(uri, rate);
	}
} // namespace lain::io::sequence
