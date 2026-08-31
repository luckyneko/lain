#pragma once

// lain::media — finite, ordered, lazily decoded footage, medium-neutral.
//
// The facade: include this for the whole vocabulary, or the individual headers when only part
// of it is wanted. The library owns the sequence model and DEPENDS ON NO io AT ALL — each
// medium's opener lives in that medium's own io seam (io::image::openSequence,
// io::video::open), so the medium-neutral library never depends on every medium (ADR-0018).

#include "lain/media/frameref.h"	  // which frame of which source — identity
#include "lain/media/framesequence.h" // the list of frame references
#include "lain/media/framesource.h"	  // one bounded thing frames come from; the only decoder
#include "lain/media/framespec.h"	  // the single declared shape of every frame
#include "lain/media/operations.h"	  // clip / concat / reverse / stride / select
