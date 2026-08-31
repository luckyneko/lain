#pragma once

#include "lain/media/frameref.h"
#include "lain/media/framespec.h"

#include <lain/image/image.h>

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace lain::media
{
	// One bounded thing frames come from: a video file, an image-sequence pattern. Identified by
	// its canonical uri, and the only class in this library that decodes.
	//
	// EXACTLY ONE CLASS DECODES PER MEDIUM (ADR-0018). The rejected alternative — an abstract
	// source with ClippedSource / ConcatSource / ReversedSource wrappers — makes each verb a new
	// class and makes clip-of-concat-of-clip a chain of virtuals whose flattening nobody owns.
	// Here every sequence operation is a LIST operation over FrameSequence, and a source only
	// ever answers "how many frames" and "decode this one".
	//
	// This base owns the two things every medium needs identically — memoisation and the
	// synchronisation around it — so a subclass implements decodeFrame() and nothing else. A
	// source is logically immutable and physically memoising, which satisfies flow's actual
	// requirement (a recompute never disturbs a payload another slot is still reading) through
	// internal synchronisation rather than inertness. Concurrent reads are SAFE, not parallel:
	// one decode at a time behind the mutex is the first implementation, and a decoder pool is a
	// later swap behind this same interface, which is why CONTEXT.md worded the promise as
	// "concurrent decode requests are safe even when an implementation serializes access
	// internally".
	//
	// Held by shared_ptr — a FrameSequence's entries share sources, and copying a sequence must
	// not copy a decoder or its cache.
	class FrameSource
	{
	public:
		virtual ~FrameSource() = default;

		FrameSource(const FrameSource&) = delete;
		FrameSource& operator=(const FrameSource&) = delete;

		// The canonical uri identifying this source, as it appears in every FrameRef it issues.
		const std::string& uri() const { return m_uri; }

		// The declared shape of every frame here. Established at open, never re-derived.
		const FrameSpec& spec() const { return m_spec; }

		// Exact, because the source is indexed at open rather than estimated from a duration.
		std::size_t frameCount() const { return m_frameCount; }

		// The identity of frame `ordinal`, WITHOUT decoding it. Out of range yields an invalid
		// FrameRef.
		FrameRef frame(std::size_t ordinal) const;

		// Frame `ordinal`, decoded. BLOCKS, and yields an invalid Image on any failure — out of
		// range, a decode error, or a frame that does not match the declared spec. That is the
		// convention LoadImageNode already uses, and it is deliberate: compute() is synchronous
		// everywhere in this engine, and an async payload would be the first thing to break it.
		//
		// The returned Image owns its pixels, so it stays valid after the cache evicts it.
		lain::image::Image image(std::size_t ordinal) const;

		// How many decoded frames a source retains by default.
		//
		// Small on purpose. The cache is not there to hold a working set — the host renders one
		// frame at a time (ADR-0018: a render is a fold, not a map) — but to stop a graph that
		// reads one position twice, or a viewer scrubbing back a frame, from decoding twice. At
		// 4K a frame is ~33 MB, so a large ring costs hundreds of megabytes to serve a pattern
		// nothing exhibits. A medium that decodes in groups can ask for more.
		static constexpr std::size_t defaultCacheFrames = 4;

	protected:
		FrameSource(std::string uri, FrameSpec spec, std::size_t frameCount,
					std::size_t cacheFrames = defaultCacheFrames);

		// Decode frame `ordinal`, ignoring any cache. Called with the source's lock held and
		// `ordinal` already range-checked; return an invalid Image on failure. The one thing a
		// medium must implement.
		virtual lain::image::Image decodeFrame(std::size_t ordinal) const = 0;

		// Presentation time of frame `ordinal`, from the start of this source. The default
		// derives it from the nominal rate, which is right for constant-rate material and for
		// stills; a medium with a real frame table overrides it and reports what the container
		// says, which is what makes variable frame rate free.
		//
		// core::Time stores exact nanoseconds, so a rate that does not divide a second evenly
		// lands within a nanosecond rather than exactly. That is why the RATE is rational and
		// this is not: the exact value lives in the rate, and this is a derived convenience.
		virtual lain::core::Time timestampOf(std::size_t ordinal) const;

	private:
		// A ring rather than an LRU: sequential access is the workload the whole design is built
		// around (ADR-0018 keeps one Evaluation across a render so the decoder stays warm), and
		// for a sequential reader every eviction policy agrees. An LRU would cost a data
		// structure to serve a difference nothing here can observe.
		struct Slot
		{
			std::size_t ordinal = 0;
			bool filled = false;
			lain::image::Image image;
		};

		std::string m_uri;
		FrameSpec m_spec;
		std::size_t m_frameCount = 0;

		mutable std::mutex m_mutex;
		mutable std::vector<Slot> m_ring;
		mutable std::size_t m_next = 0;
	};
} // namespace lain::media
