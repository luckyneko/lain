#pragma once

#include <lain/media/frameref.h>
#include <lain/media/framesequence.h>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>

// Finding a capture-group member's frame in its camera's footage: private to lain::camera, so the
// method modules that decode members' frames (board registration, feature extraction) share one
// answer.
namespace lain::camera::detail
{
	// Where each frame of one camera's footage sits in it, found by the frame's identity. Built once
	// per footage, so a dataset of thousands of groups does not search the footage per member.
	class FootageIndex
	{
	public:
		explicit FootageIndex(const media::FrameSequence& footage)
			: m_footage(&footage)
		{
			for (std::size_t i = 0; i < footage.size(); ++i)
			{
				const media::FrameRef frame = footage.frame(i);
				m_positions.emplace(Key{frame.source.toString(), frame.ordinal}, i);
			}
		}

		const media::FrameSequence& footage() const { return *m_footage; }

		// The footage's position holding `frame`, or nullopt when the footage has no such frame.
		std::optional<std::size_t> position(const media::FrameRef& frame) const
		{
			const auto found = m_positions.find(Key{frame.source.toString(), frame.ordinal});
			if (found == m_positions.end())
				return std::nullopt;
			return found->second;
		}

	private:
		using Key = std::pair<std::string, std::size_t>; // source uri, ordinal: a frame's identity

		const media::FrameSequence* m_footage;
		std::map<Key, std::size_t> m_positions;
	};
} // namespace lain::camera::detail
