#pragma once

#include "lain/media/frameref.h"
#include "lain/media/framesource.h"
#include "lain/media/framespec.h"

#include <lain/image/image.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lain::media
{
	// A finite, ordered LIST of frame references over one or more sources. Not a handle to a
	// file and not a chain of wrapping decoders (ADR-0018).
	//
	// Everything follows from the list form: clip slices, concatenate appends, reverse reverses,
	// a selection is an arbitrary subset — and a multi-file timeline is free, which is
	// CONTEXT.md's *camera-sequence definition* over *media segments* realised rather than a new
	// concept. See operations.h for the verbs.
	//
	// Copyable and cheap to copy: entries share their sources by shared_ptr, so a copy duplicates
	// a small vector and no decoder, handle or cache. That is what lets a sequence ride a flow
	// port like any other payload.
	//
	// An EMPTY sequence is a value, not a failure — zero frames of nothing, which is what an
	// empty folder legitimately produces. Failure is std::nullopt, as it is for io::image::load.
	class FrameSequence
	{
	public:
		// One frame: which source, and which frame of it. The ordinal is IDENTITY and never
		// changes; the entry's position in the list is where it currently sits.
		struct Entry
		{
			std::shared_ptr<const FrameSource> source;
			std::size_t ordinal = 0;
		};

		// The empty sequence.
		FrameSequence() = default;

		// Every frame of `source`, in order. Null yields the empty sequence.
		static FrameSequence over(std::shared_ptr<const FrameSource> source);

		// A sequence of arbitrary entries, or nullopt when their sources' specs cannot be
		// unified (see unify()) — the point of composition where homogeneity is enforced. An
		// entry whose ordinal is out of its source's range is refused too, since it could never
		// decode. The primitive every operation in operations.h is built from.
		[[nodiscard]] static std::optional<FrameSequence> of(std::vector<Entry> entries);

		bool empty() const { return m_entries.empty(); }
		std::size_t size() const { return m_entries.size(); }

		// The single declared shape of every frame here — unified across all sources at
		// construction, so it is a fact rather than a promise. An empty sequence's spec is
		// invalid, which is the honest answer to "what shape are no frames".
		const FrameSpec& spec() const { return m_spec; }

		const std::vector<Entry>& entries() const { return m_entries; }

		// The identity of the frame at `position`, WITHOUT decoding it. Out of range yields an
		// invalid FrameRef.
		FrameRef frame(std::size_t position) const;

		// The frame at `position`, decoded. Blocks; an invalid Image on any failure, including
		// out of range. See FrameSource::image.
		lain::image::Image image(std::size_t position) const;

		// "500 frames · 3840x2160 RGB8 sRGB · 29.97 fps"
		std::string toString() const;

	private:
		explicit FrameSequence(std::vector<Entry> entries, FrameSpec spec);

		std::vector<Entry> m_entries;
		FrameSpec m_spec;
	};
} // namespace lain::media
