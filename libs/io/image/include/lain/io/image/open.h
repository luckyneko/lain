#pragma once

#include <lain/core/uri.h>
#include <lain/media/framesequence.h>
#include <lain/media/framespec.h>

#include <optional>
#include <string_view>

namespace lain::io::image
{
	// The pattern key naming a still's ordinal: "shot.<frame:04>.png" (lain::string::Pattern).
	//
	// ONE declaration, because reading a numbered sequence and writing one must agree on it. The
	// matcher below is the reader; a render sweep is the writer, and it names this constant rather
	// than its own spelling — two spellings of the key means what a sweep writes cannot be read
	// back, and the disagreement is silent. (Until M13 slice 2 that agreement was structural:
	// both directions called io::numberField, which knew only "where the '#' run is". The key is
	// what carries it now.)
	//
	// ":04" is the padding CONVENTION, not part of the key: a width is a writing decision and no
	// filter at all on the way in, so "shot.7.png" and "shot.0007.png" both match.
	inline constexpr std::string_view frameKey = "frame";

	// Open a folder or a numbered pattern of stills as a media::FrameSequence — the image
	// medium's contribution to the frame-sequence model (ADR-0018), and the peer of
	// io::video::open.
	//
	// THE SEQUENCE IS lain::media's, NOT THIS MEDIUM'S. A frame sequence is medium-neutral and
	// lives in lain::media, which depends on no io at all; what lives here is the image medium's
	// way of OPENING one, the way io::video owns the video medium's. A medium-neutral library
	// that knew how to open things would end up depending on every medium — which is also why
	// io::sequence, one level up, is what dispatches between the two.
	//
	// It is `open` and not `openSequence` because open is the verb: uri in, a LAZY HANDLE out.
	// load() in this same seam is the other one — uri in, a whole decoded Image out — and the
	// difference between them is exactly the difference the two names carry.
	//
	// `uri` is either:
	//   - a DIRECTORY — every file in it whose extension has a registered image reader, sorted
	//     by filename. Lexicographic, matching flow's ListDir so the two never disagree about
	//     order; use a pattern when the numbering is unpadded, since "frame10" sorts before
	//     "frame2".
	//   - a PATTERN naming the frame key — "shot.<frame:04>.png". The key marks WHERE the number
	//     is; the spec is the padding convention for writing and is not a filter for reading, so
	//     "shot.7.png" and "shot.0007.png" both match and both sort as 7. The key must be in the
	//     FILENAME: the scan reads one directory, so a key in a directory component is refused
	//     rather than silently matching nothing.
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
	[[nodiscard]] std::optional<lain::media::FrameSequence> open(const lain::core::Uri& uri,
																 lain::media::FrameRate rate = {});
} // namespace lain::io::image
