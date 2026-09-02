#include "lain/io/sequence/openers.h"

#include "lain/io/sequence/open.h" // registerOpener / registerDefaultOpener

#include <lain/io/image/sequence.h>
#include <lain/io/video/open.h>

#include <string>

// The only translation unit in this library that names a medium — which is exactly why it is its
// own file with its own header, rather than a few lines added to the registry's. open.cpp depends
// on nothing but the registry's own vocabulary, and that is what lets a medium outside this tree
// register itself without an edit here.

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
