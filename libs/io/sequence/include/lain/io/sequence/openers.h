#pragma once

namespace lain::io::sequence
{
	// Wire the built-in media into the opener registry: the video seam under each of its container
	// extensions, the image seam as the default. The app's single sequence-wiring point, beside
	// registerImageCodecs() and registerVideoCodecs() — and distinct from them, because those wire
	// CODECS into a medium while this wires MEDIA into the dispatcher.
	//
	// It has its own header because it has its own translation unit, and it has its own translation
	// unit because it is the only part of this library that names a medium: open.cpp is the registry
	// and depends on nothing but the registry's own vocabulary, which is what lets a medium outside
	// this tree register itself without an edit here. Declaring it in open.h would put the seam's
	// one dependency-bearing function in the header that exists to have none.
	//
	// Call it before io::sequence::open(); with nothing registered, open() has nothing to dispatch
	// to and says so.
	void registerSequenceOpeners();
} // namespace lain::io::sequence
