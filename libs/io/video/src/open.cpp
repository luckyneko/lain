#include "lain/io/video/open.h"

#include "videosource.h"

#include <lain/io/stream.h>
#include <lain/io/uri.h>
#include <lain/log/log.h>

#include <algorithm>
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

	bool isVideoUri(std::string_view uri)
	{
		// io::extensionKey, not a local lowercase-the-suffix: three seams ask what a file is called
		// (a codec key, this container claim, io::sequence's medium dispatch) and a format decided
		// in three places disagrees with itself over "clip.MP4" silently.
		const std::string key = lain::io::extensionKey(uri);
		if (key.empty())
			return false;

		const std::vector<std::string>& extensions = videoExtensions();
		return std::find(extensions.begin(), extensions.end(), key) != extensions.end();
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
				// What the file turned out to be, said once, where a person can see it. The
				// container and the codec are separate facts about one file and neither is in its
				// name — a "clip.mp4" is as likely to hold prores as h264 — so a reader that could
				// not report both would leave the distinction invisible in the one place it costs
				// nothing to state.
				lain::log::info("io::video: opened {} — {}/{}, {}, {} frames", canonical, reader->container(),
								reader->codec(), reader->spec().toString(), reader->frameCount());

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
