#include "lain/media/framesource.h"

#include <lain/log/log.h>

#include <utility>

namespace lain::media
{
	FrameSource::FrameSource(std::string uri, FrameSpec spec, std::size_t frameCount, std::size_t cacheFrames)
		: m_uri{std::move(uri)}
		, m_spec{spec}
		, m_frameCount{frameCount}
		, m_ring(cacheFrames)
	{
	}

	lain::core::Time FrameSource::timestampOf(std::size_t ordinal) const
	{
		if (!m_spec.rate.specified())
			return {};
		const double seconds = static_cast<double>(ordinal) * m_spec.rate.denominator / m_spec.rate.numerator;
		return lain::core::Time::from<lain::core::Time::Seconds>(seconds);
	}

	FrameRef FrameSource::frame(std::size_t ordinal) const
	{
		if (ordinal >= m_frameCount)
			return {};
		return FrameRef{m_uri, ordinal, timestampOf(ordinal)};
	}

	lain::image::Image FrameSource::image(std::size_t ordinal) const
	{
		// log::error, NOT log::ensure: ensure is a PRECONDITION helper that aborts in a debug
		// build, and every refusal in this file is driven by data rather than by a broken
		// invariant — a graph binding a frame position past the end, a folder holding one stray
		// odd-sized still. ADR-0018 fixes the answer as "an invalid Image", and a debug abort is
		// not that.
		if (ordinal >= m_frameCount)
		{
			lain::log::error("media: frame {} is out of range for {} ({} frames)", ordinal, m_uri, m_frameCount);
			return {};
		}

		// The lock is held across the decode, deliberately. ADR-0018 specifies one decoder behind
		// a mutex: concurrent requests are SAFE, not parallel. Releasing it around the decode
		// would let two callers decode the same frame at once and race to cache it — more work
		// for no benefit, since a decoder pool (the eventual answer to parallelism) is a swap
		// behind this same interface rather than a lock the caller can widen.
		std::lock_guard<std::mutex> lock(m_mutex);

		for (const Slot& slot : m_ring)
		{
			if (slot.filled && slot.ordinal == ordinal)
				return slot.image;
		}

		lain::image::Image decoded = decodeFrame(ordinal);
		if (!decoded.valid())
		{
			// The subclass has already said why. A failure is deliberately NOT cached: a
			// transient decode error would otherwise be remembered for the life of the source,
			// and re-reading is cheap next to being permanently wrong.
			return {};
		}

		// A frame that does not match what the sequence declared is refused rather than
		// delivered. A consumer that received it could only cope by converting, on a frame it
		// did not know would differ — the silent lossy conversion this design exists to prevent.
		if (!matches(m_spec, decoded))
		{
			lain::log::error("media: frame {} of {} is {}, not the declared {} — refusing it", ordinal, m_uri,
							 specOf(decoded).toString(), m_spec.toString());
			return {};
		}

		if (m_ring.empty())
			return decoded; // caching disabled: hand it straight back

		Slot& slot = m_ring[m_next];
		slot.ordinal = ordinal;
		slot.filled = true;
		slot.image = std::move(decoded);
		m_next = (m_next + 1) % m_ring.size();

		// One deep copy out of the cache, which is what makes the returned Image own its pixels
		// and stay valid after eviction.
		return slot.image;
	}
} // namespace lain::media
