#include "lain/media/framesequence.h"

#include <lain/log/log.h>
#include <lain/string/format.h>

#include <utility>

namespace lain::media
{
	FrameSequence::FrameSequence(std::vector<Entry> entries, FrameSpec spec)
		: m_entries{std::move(entries)}
		, m_spec{spec}
	{
	}

	FrameSequence FrameSequence::over(std::shared_ptr<const FrameSource> source)
	{
		if (!source)
			return {};

		std::vector<Entry> entries;
		entries.reserve(source->frameCount());
		for (std::size_t ordinal = 0; ordinal < source->frameCount(); ++ordinal)
			entries.push_back(Entry{source, ordinal});

		// One source cannot disagree with itself, so this cannot fail — but it goes through the
		// same constructor rather than a second path that sets m_spec its own way.
		return FrameSequence{std::move(entries), source->spec()};
	}

	std::optional<FrameSequence> FrameSequence::of(std::vector<Entry> entries)
	{
		FrameSpec spec;
		for (const Entry& entry : entries)
		{
			// Logged refusals rather than log::ensure throughout: ensure aborts in a debug
			// build, and these are reachable from data — a selection computed from a metric, two
			// real files whose specs disagree — not from a broken invariant.
			if (entry.source == nullptr)
			{
				lain::log::error("media: a frame entry names no source");
				return std::nullopt;
			}

			if (entry.ordinal >= entry.source->frameCount())
			{
				lain::log::error("media: frame {} is out of range for {} ({} frames)", entry.ordinal,
								 entry.source->uri(), entry.source->frameCount());
				return std::nullopt;
			}

			// Unified pairwise as entries are admitted, so the refusal names the source that
			// disagrees rather than reporting that "the sequence" is inconsistent.
			std::optional<FrameSpec> unified = unify(spec, entry.source->spec());
			if (!unified.has_value())
			{
				lain::log::error("media: {} is {}, which cannot join a sequence of {}", entry.source->uri(),
								 entry.source->spec().toString(), spec.toString());
				return std::nullopt;
			}
			spec = *unified;
		}
		return FrameSequence{std::move(entries), spec};
	}

	FrameRef FrameSequence::frame(std::size_t position) const
	{
		if (position >= m_entries.size())
			return {};
		const Entry& entry = m_entries[position];
		return entry.source->frame(entry.ordinal);
	}

	lain::image::Image FrameSequence::image(std::size_t position) const
	{
		if (position >= m_entries.size())
		{
			lain::log::error("media: position {} is out of range for a {}-frame sequence", position,
							 m_entries.size());
			return {};
		}
		const Entry& entry = m_entries[position];
		return entry.source->image(entry.ordinal);
	}

	std::string FrameSequence::toString() const
	{
		if (m_entries.empty())
			return "0 frames";
		return lain::string::format("{} frames · {}", m_entries.size(), m_spec.toString());
	}
} // namespace lain::media
