#include "lain/io/sequence/open.h"

#include <lain/io/image/sequence.h>
#include <lain/io/video/open.h>

#include <string>

// The only translation unit in this library that names a medium. open.cpp holds the registry and
// depends on nothing but the registry's own vocabulary, which is what lets a medium outside this
// tree register itself without an edit here.

namespace lain::io::sequence
{
	void registerSequenceOpeners()
	{
		// Video claims its container extensions by name. The list is the SEAM's (io::video), not a
		// codec plugin's, so .mp4 routes to video and reports a missing capability even in a build
		// with no video codec at all (ADR-0019, amended).
		for (const std::string& extension : lain::io::video::videoExtensions())
			registerOpener(extension, &lain::io::video::open);

		// Stills are the default rather than a set of claimed extensions, because the image medium
		// is addressed structurally: a folder has no extension to key on, and a "shot.####.png"
		// pattern names the still format rather than the sequence's. openSequence already answers
		// "that is neither a directory nor a ####-numbered pattern" for anything it cannot take,
		// which is the honest report for an unclaimed name.
		registerDefaultOpener(&lain::io::image::openSequence);
	}
} // namespace lain::io::sequence
