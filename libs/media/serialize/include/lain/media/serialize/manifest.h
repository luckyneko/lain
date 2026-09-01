#pragma once

#include <lain/data/data.h>
#include <lain/media/framesequence.h>
#include <lain/media/framespec.h>

#include <cstdint>
#include <string>
#include <vector>

// lain::media::serialize — the frame-sequence model's documents.
//
// A SEPARATE TARGET over lain::media + lain::data, kept out of media by the same boundary rule
// flow::serialize follows: a concern lives in the core library iff it stays free of an external
// format, and the moment it must name lain::data it lives outside. That keeps `lain::media` linking
// no data — as no foundational library in the set does — while the knowledge of what a sequence's
// honest description IS stays with the sequence model rather than in whichever app happens to write
// one.

// Everything here is in `lain::media` — not a `serialize` sub-namespace — because FrameSpec's
// serialize() must be in FrameSpec's own namespace for ADL to find it, and a namespace cannot share
// a name with a function in the same scope. The TARGET is still lain::media::serialize, the way
// lain::io::image::codecs is a target whose functions live in lain::io::image.
namespace lain::media
{
	// FrameRate and FrameSpec become serializable in THIS TARGET rather than in lain::media's own,
	// for the boundary reason above. Both round-trip properly; a FrameSequence deliberately does
	// not (see manifestOf below).
	LAIN_SERIALIZE(FrameRate, numerator, denominator)

	// Hand-written for exactly one reason: `extent` is a math::Vec2i, which is a pure alias for
	// glm::ivec2 — so ADL for a bridge would associate `glm`, not `lain::math`, and a
	// LAIN_SERIALIZE placed in lain::math would COMPILE AND NEVER BE FOUND. Putting one in namespace
	// glm works but is then global to the program, so the next target wanting it collides. Flattening
	// the vector here costs two lines and owns nothing it should not.
	//
	// glm's .x/.y are real lvalue members, so member() binds them in both directions.
	inline void serialize(lain::data::Archive& archive, FrameSpec& spec)
	{
		archive.member("width", spec.extent.x)
			.member("height", spec.extent.y)
			.member("pixelFormat", spec.pixelFormat) // enums reflect as their NAME
			.member("colorSpace", spec.colorSpace)
			.member("alphaMode", spec.alphaMode)
			.member("rate", spec.rate);
	}
	// One frame's identity as a document records it.
	//
	// Its own type rather than a FrameRef because FrameRef::timestamp is a core::Time whose only
	// accessor returns by value, and Archive::member needs a real lvalue — it writes through the
	// reference on load. Seconds is what a manifest reader wants anyway.
	struct ManifestFrame
	{
		std::string source;		   // the canonical uri of the source this frame belongs to
		std::uint64_t ordinal = 0; // which frame OF THAT SOURCE — identity, not position
		double timestamp = 0.0;	   // presentation time from the start of its source, in seconds
	};

	// What a sequence-valued output writes: "these frames of that source".
	//
	// A selection's honest artifact is not pixels (ADR-0018). A processing graph structurally cannot
	// produce a sequence — a frame reference names a source and a computed frame has none — so a
	// sequence-valued output is always a SELECTION, and what it selected is the thing worth
	// recording.
	//
	// There is no frameCount: it is frames.size(), and a document that records a derivable fact
	// gives it somewhere to disagree with what it describes. Position is likewise implicit in the
	// array order, while identity is explicit per entry — which is the distinction the whole model
	// rests on, since a clip re-bases the first and preserves the second.
	//
	// Each frame carries its own source uri rather than an index into a list of sources: a frame
	// reference is "a name you can look up", and an index into one sequence's private source list is
	// exactly what CONTEXT.md's *FrameRef* entry says to avoid. It costs repetition in the file and
	// buys a document that means something on its own.
	struct SequenceManifest
	{
		int version = 1;
		FrameSpec spec;
		std::vector<ManifestFrame> frames;
	};

	LAIN_SERIALIZE(ManifestFrame, source, ordinal, timestamp)
	LAIN_SERIALIZE(SequenceManifest, version, spec, frames)

	// Describe `sequence` as a manifest.
	//
	// ONE WAY, and structurally so: there is no counterpart turning a manifest back into a
	// FrameSequence, because a sequence's entries hold shared_ptr<const FrameSource> and rebuilding
	// one means re-opening every source it names — with a policy for the ones that have moved. Note
	// what is NOT here as a result: a `serialize(Archive&, FrameSequence&)`. Archive is
	// direction-agnostic, so writing one would make `data::fromValue<FrameSequence>` compile and
	// silently yield an empty sequence. The reader arrives with the opener registry, if a caller ever
	// wants one.
	//
	// The manifest itself round-trips perfectly well — it is plain data — which is what lets it be
	// tested as data rather than only as bytes.
	[[nodiscard]] SequenceManifest manifestOf(const FrameSequence& sequence);
} // namespace lain::media
