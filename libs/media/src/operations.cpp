#include "lain/media/operations.h"

#include <lain/log/log.h>

#include <algorithm>
#include <utility>

namespace lain::media
{
	// Every verb here builds a new entry list and hands it to FrameSequence::of, so admission —
	// the range check and the spec unification — happens in exactly one place. A verb that
	// constructed a sequence directly would be a second admission path, and the one that got
	// updated last would be the one with the bug.
	//
	// clip / reverse / stride cannot fail: they only ever drop or reorder entries that were
	// already admitted, so the sequence they build is a subset of one that already unified. They
	// still route through of(), and simply return the empty sequence in the impossible case
	// rather than carrying an optional the caller would have to unwrap for no reason.
	static FrameSequence rebuild(std::vector<FrameSequence::Entry> entries)
	{
		std::optional<FrameSequence> sequence = FrameSequence::of(std::move(entries));
		return sequence.has_value() ? std::move(*sequence) : FrameSequence{};
	}

	FrameSequence clip(const FrameSequence& sequence, std::size_t position, std::size_t count)
	{
		const std::vector<FrameSequence::Entry>& entries = sequence.entries();
		if (position >= entries.size())
			return {};

		// Clamped rather than refused: asking for more than exists is an ordinary thing to do at
		// the end of a timeline, and the honest answer is "what there was".
		const std::size_t available = entries.size() - position;
		const std::size_t taken = std::min(count, available);

		return rebuild(std::vector<FrameSequence::Entry>{entries.begin() + static_cast<std::ptrdiff_t>(position),
														 entries.begin() + static_cast<std::ptrdiff_t>(position + taken)});
	}

	std::optional<FrameSequence> concat(const FrameSequence& a, const FrameSequence& b)
	{
		std::vector<FrameSequence::Entry> entries = a.entries();
		entries.insert(entries.end(), b.entries().begin(), b.entries().end());

		// of() unifies, so the empty-sequence case needs no special handling here: an empty
		// sequence contributes no entries and therefore no constraint.
		return FrameSequence::of(std::move(entries));
	}

	FrameSequence reverse(const FrameSequence& sequence)
	{
		std::vector<FrameSequence::Entry> entries = sequence.entries();
		std::reverse(entries.begin(), entries.end());
		return rebuild(std::move(entries));
	}

	FrameSequence stride(const FrameSequence& sequence, std::size_t step)
	{
		if (step == 0)
		{
			lain::log::error("media: stride needs a step of at least 1");
			return {};
		}

		std::vector<FrameSequence::Entry> entries;
		entries.reserve((sequence.size() + step - 1) / step);
		for (std::size_t position = 0; position < sequence.size(); position += step)
			entries.push_back(sequence.entries()[position]);

		return rebuild(std::move(entries));
	}

	std::optional<FrameSequence> select(const FrameSequence& sequence, const std::vector<std::size_t>& positions)
	{
		std::vector<FrameSequence::Entry> entries;
		entries.reserve(positions.size());
		for (std::size_t position : positions)
		{
			if (position >= sequence.size())
			{
				lain::log::error("media: cannot select position {} of a {}-frame sequence", position,
								 sequence.size());
				return std::nullopt;
			}
			entries.push_back(sequence.entries()[position]);
		}
		return FrameSequence::of(std::move(entries));
	}
} // namespace lain::media
