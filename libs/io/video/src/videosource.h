#pragma once

// The video medium's media::FrameSource — private to lain::io::video.
//
// It lives in the SEAM rather than in a backend plugin so that the ring cache, the lock, the range
// check and the spec enforcement are implemented once for the medium instead of once per backend:
// a plugin implements VideoReader and nothing else, exactly as ImageSequenceSource leaves libpng
// with only ImageReader to fill in.

#include "lain/io/video/reader.h"

#include <lain/media/framesource.h>

#include <cstddef>
#include <memory>
#include <string>

namespace lain::io::video
{
	class VideoSource : public lain::media::FrameSource
	{
	public:
		VideoSource(std::string uri, std::unique_ptr<VideoReader> reader);

	protected:
		lain::image::Image decodeFrame(std::size_t ordinal) const override;
		lain::core::Time timestampOf(std::size_t ordinal) const override;

	private:
		// Mutable because decoding is a physical act on a logically immutable source: the base
		// class calls decodeFrame() const with its lock held, and a decoder has a position that
		// the decode moves. It is the same memoisation the ring cache itself is (framesource.h);
		// the lock is what makes it safe, and the const is what promises the SEQUENCE does not
		// change.
		mutable std::unique_ptr<VideoReader> m_reader;
	};
} // namespace lain::io::video
