#pragma once

#include <lain/core/uri.h>

namespace lain::io
{
	// The canonical form of `uri` — ONE resource has ONE name, however it was spelled.
	//
	// This exists because identity is built on it. A media::FrameRef names its source by uri and
	// nothing else (ADR-0018: a name you can look up, never a back-pointer), so "./take1.mp4" and
	// "/footage/../footage/take1.mp4" must produce the same string or two references to one frame
	// stop comparing equal — and a saved manifest stops meaning anything. It is the same rule
	// graphio::templateKey already applies to template paths, hoisted here so the image and video
	// openers cannot each grow their own version: a key computed two ways eventually disagrees
	// with itself, and the failure is silent.
	//
	// It lives HERE rather than on core::Uri because it touches the filesystem, which lain::core
	// does not (ADR-0023). That split is also what keeps the type honest about what it knows: a
	// Uri can say what it is called and whether it is local, but only io can say what it resolves
	// to.
	//
	// Local paths resolve through std::filesystem::weakly_canonical, so a path that does not
	// exist yet still has a stable answer (an output pattern, a template about to be written).
	// Symlinks along an existing prefix are resolved. A non-local scheme is returned unchanged —
	// there is nothing to canonicalise until one is actually served.
	//
	// Returns `uri` unchanged if the filesystem refuses to answer, which is the honest fallback:
	// a worse key beats no key.
	[[nodiscard]] lain::core::Uri canonicalise(const lain::core::Uri& uri);
} // namespace lain::io
