#pragma once

#include <lain/media/framesequence.h>
#include <lain/media/framespec.h>

#include <optional>
#include <string_view>

namespace lain::io::image
{
	// Open a folder or a numbered pattern of stills as a media::FrameSequence — the image
	// medium's contribution to the frame-sequence model (ADR-0018).
	//
	// Each medium's opener lives in that medium's own io seam, which is why this is here and not
	// in lain::media: a medium-neutral library that knew how to open things would end up
	// depending on every medium.
	//
	// `uri` is either:
	//   - a DIRECTORY — every file in it whose extension has a registered image reader, sorted
	//     by filename. Lexicographic, matching flow's ListDir so the two never disagree about
	//     order; use a pattern when the numbering is unpadded, since "frame10" sorts before
	//     "frame2".
	//   - a PATTERN containing a run of '#' — "shot.####.png". The run marks WHERE the number is;
	//     its length is the padding convention for writing and is not a filter for reading, so
	//     "shot.7.png" and "shot.0007.png" both match and both sort as 7.
	//
	// `rate` is the nominal rate to declare. Stills genuinely have none, so the default leaves it
	// unspecified — which is what lets such a sequence be concatenated with a video file that
	// does declare one (see media::unify).
	//
	// Returns std::nullopt when the sequence cannot be established: the containing directory does
	// not exist, or the first frame cannot be decoded (its shape is what the sequence declares,
	// so without it there is no spec). A directory that exists but holds no readable images
	// yields an EMPTY sequence — zero frames is a value, the same distinction flow's ListDir
	// draws between a missing folder and an empty one. The reason is logged either way.
	//
	// Frames are decoded lazily, one at a time, and validated against the declared spec as they
	// arrive: a stray odd-sized still yields an invalid Image for that frame rather than
	// corrupting the sequence's promise.
	[[nodiscard]] std::optional<lain::media::FrameSequence> openSequence(std::string_view uri,
																		 lain::media::FrameRate rate = {});
} // namespace lain::io::image
