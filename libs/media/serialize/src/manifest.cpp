#include "lain/media/serialize/manifest.h"

#include <lain/media/frameref.h>

namespace lain::media
{
	SequenceManifest manifestOf(const FrameSequence& sequence)
	{
		SequenceManifest manifest;
		manifest.spec = sequence.spec();
		manifest.frames.reserve(sequence.size());

		for (std::size_t position = 0; position < sequence.size(); ++position)
		{
			// frame(), not image(): a manifest describes which frames were chosen and must not
			// decode a single one to say so.
			const FrameRef ref = sequence.frame(position);
			manifest.frames.push_back(
				ManifestFrame{ref.source, static_cast<std::uint64_t>(ref.ordinal), ref.timestamp.seconds()});
		}
		return manifest;
	}
} // namespace lain::media
