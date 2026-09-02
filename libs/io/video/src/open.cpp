#include "lain/io/video/open.h"

#include "videosource.h"

#include <lain/io/stream.h>
#include <lain/io/uri.h>
#include <lain/log/log.h>

#include <memory>
#include <utility>

namespace lain::io::video
{
	lain::core::Factory<VideoReader>& readerRegistry()
	{
		static lain::core::Factory<VideoReader> registry;
		return registry;
	}

	const std::vector<std::string>& videoExtensions()
	{
		// CONTAINERS, not codecs — what a FILE is called, which is the only question this list
		// answers. Which codec is inside is the demuxer's business and is established from the
		// bytes; the two are separate aspects of a video file and only the first is in its name.
		static const std::vector<std::string> extensions{
			"mp4",
			"m4v",
			"mov",
			"mkv",
			"webm",
			"avi",
			"mpg",
			"mpeg",
			"ts",
			"mxf",
			"y4m",
		};
		return extensions;
	}

	std::optional<lain::media::FrameSequence> open(std::string_view uri, lain::media::FrameRate rate)
	{
		const std::string canonical = lain::io::canonicalUri(uri);

		const std::vector<std::string> backends = readerRegistry().keys();
		if (backends.empty())
		{
			// The whole reason this seam is built even when no backend plugin is: a document that
			// names a video keeps its node and its edges, and running it reports a missing
			// CAPABILITY rather than a missing vocabulary (ADR-0019, amended).
			lain::log::error("io::video: cannot open {} — this build has no video codec plugin "
							 "(configure with -DLAIN_IO_VIDEO_FFMPEG=ON)",
							 canonical);
			return std::nullopt;
		}

		for (const std::string& backend : backends)
		{
			// A fresh stream per attempt: open() takes the transport, so a backend that refuses
			// the container has already consumed it. Cheap — nothing has been read yet.
			std::unique_ptr<ReadStream> stream = openStream(canonical);
			if (!stream)
				return std::nullopt; // openStream logged the reason; a second backend cannot help

			std::unique_ptr<VideoReader> reader = readerRegistry().create(backend);
			if (reader && reader->open(std::move(stream), rate))
			{
				return lain::media::FrameSequence::over(
					std::make_shared<VideoSource>(canonical, std::move(reader)));
			}
		}

		// Every registered reader logged its own reason for refusing; this says what that adds up
		// to, which is the thing a caller acts on.
		lain::log::error("io::video: no registered video reader could open {}", canonical);
		return std::nullopt;
	}
} // namespace lain::io::video
