#include "videosource.h"

#include <utility>

namespace lain::io::video
{
	VideoSource::VideoSource(std::string uri, std::unique_ptr<VideoReader> reader)
		: FrameSource{std::move(uri), reader->spec(), reader->frameCount()}
		, m_reader{std::move(reader)}
	{
	}

	lain::image::Image VideoSource::decodeFrame(std::size_t ordinal) const
	{
		// The reader logs its own reason and returns an invalid Image, which is precisely what the
		// base class expects back — so there is nothing to translate here.
		return m_reader->decode(ordinal);
	}

	lain::core::Time VideoSource::timestampOf(std::size_t ordinal) const
	{
		// Overridden rather than inherited: the base derives a timestamp from the nominal rate,
		// which is right for stills and for constant-rate material and wrong for everything else.
		// A container reports what it actually stored, which is what makes VFR free (ADR-0018).
		return m_reader->timestamp(ordinal);
	}
} // namespace lain::io::video
