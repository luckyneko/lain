#include "lain/io/video/save.h"

#include "lain/io/video/open.h" // isVideoUri

#include <lain/image/pixelformat.h>
#include <lain/io/stream.h>
#include <lain/io/uri.h>
#include <lain/log/log.h>
#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <memory>
#include <utility>

namespace lain::io::video
{
	lain::core::Factory<VideoWriter>& writerRegistry()
	{
		static lain::core::Factory<VideoWriter> registry;
		return registry;
	}

	bool canEncode(const lain::media::FrameSpec& spec)
	{
		return refusedSpecReason(spec).empty();
	}

	std::string refusedSpecReason(const lain::media::FrameSpec& spec)
	{
		if (!spec.valid())
			return "an empty frame size";

		// ALPHA. No video codec in the LGPL set carries an alpha channel, and every one of them
		// reaches a subsampled YUV plane that has nowhere to put it. Dropping it here would be the
		// silent degradation ImageWriter refuses (jpeg rejects alpha rather than flattening it) —
		// composite it yourself, and the result is the one you chose.
		if (lain::image::descriptor(spec.pixelFormat).hasAlpha())
			return lain::string::format("the pixel format {} has an alpha channel, which no video codec holds",
										lain::meta::enums::name(spec.pixelFormat));

		// BIT DEPTH. The codec boundary is 8-bit, as the reader's is: PixelFormat has no planar or
		// subsampled model, so the plugin converts between lain's packed pixels and the encoder's
		// planes, and 16-bit or float would need a wider path than any delivery codec offers. Gray
		// and RGB are both accepted because widening Gray to a colour stream loses nothing — the
		// rule is "no LOSS", not "no conversion".
		if (lain::image::descriptor(spec.pixelFormat).channelType != lain::image::ChannelType::U8)
			return lain::string::format("the pixel format {} is not 8-bit, which no video codec here writes",
										lain::meta::enums::name(spec.pixelFormat));

		// COLOUR. Linear is the arm that is easy to get wrong: AVCOL_TRC_LINEAR exists and the file
		// would encode fine — but the READ policy does not refuse it, so the result comes back
		// tagged BT709 with every value off by the ~2.2 gamma, invisibly. Convert to BT709 first;
		// ADR-0018 says in as many words that BT709 is the transfer video is encoded against.
		//
		// Unspecified is refused for the opposite reason to the read side's tolerance. Untagged
		// INPUT is treated as BT709 with a log line, because rejecting real footage for a tag it
		// never carried would be loud and wrong. Here lain is the AUTHOR, and writing a guess into
		// a file that will outlive the guess is not the same thing at all.
		if (spec.colorSpace != lain::image::ColorSpace::BT709 && spec.colorSpace != lain::image::ColorSpace::sRGB)
			return lain::string::format("the colour space {} is not one a container can state — convert to BT709 first",
										lain::meta::enums::name(spec.colorSpace));

		// RATE. A container must state a timebase, and there is no honest one to invent for a
		// folder of stills. io::sequence::open already takes a fallback rate, and a render takes
		// --rate; this is where the absence of both is reported.
		if (!spec.rate.specified())
			return "no frame rate — a container must state one, so supply it when the sequence is opened";

		return {};
	}

	std::unique_ptr<VideoWriter> openWriter(std::string_view uri, const lain::media::FrameSpec& spec,
											const VideoWriterOptions& options)
	{
		const std::string canonical = lain::io::canonicalUri(uri);

		if (!isVideoUri(canonical))
		{
			lain::log::error("io::video: cannot write {} — its name does not claim a video container", canonical);
			return nullptr;
		}

		if (const std::string reason = refusedSpecReason(spec); !reason.empty())
		{
			lain::log::error("io::video: refusing to write {} — {}", canonical, reason);
			return nullptr;
		}

		const std::vector<std::string> backends = writerRegistry().keys();
		if (backends.empty())
		{
			// The same story open() tells, from the other direction: a document that names a video
			// output keeps its meaning, and running it reports a missing CAPABILITY rather than a
			// missing vocabulary (ADR-0019, amended).
			lain::log::error("io::video: cannot write {} — this build has no video codec plugin "
							 "(configure with -DLAIN_IO_VIDEO_FFMPEG=ON)",
							 canonical);
			return nullptr;
		}

		for (const std::string& backend : backends)
		{
			// A fresh stream per attempt, for the reason open() gives — open() takes the transport,
			// so a backend that refuses has already consumed it. One thing differs on this side and
			// is worth saying rather than discovering: createStream TRUNCATES, so a failed attempt
			// has already emptied the file before the next backend sees it. That is harmless, since
			// nothing valid existed either way and nothing valid exists until finish().
			std::unique_ptr<WriteStream> stream = createStream(canonical);
			if (!stream)
				return nullptr; // createStream logged the reason; a second backend cannot help

			std::unique_ptr<VideoWriter> writer = writerRegistry().create(backend);
			if (writer && writer->open(std::move(stream), lain::io::extensionKey(canonical), spec, options))
			{
				// What this file turned INTO, said once, where a person can see it — and in
				// particular which encoder "by availability" resolved to on this machine, which is
				// the one fact no caller could have known in advance.
				lain::log::info("io::video: writing {} — {}/{}, {}", canonical, writer->container(), writer->codec(),
								spec.toString());
				return writer;
			}
		}

		// Every registered writer logged the family it could not provide and what it does offer;
		// this says what that adds up to, which is the thing a caller acts on.
		lain::log::error("io::video: no registered video writer could write {}", canonical);
		return nullptr;
	}

	bool save(std::string_view uri, const lain::media::FrameSequence& sequence, const VideoWriterOptions& options)
	{
		if (sequence.empty())
		{
			// Not a zero-frame container. The caller asked to transcode nothing, and a file that
			// hides that is worse than an error.
			lain::log::error("io::video: cannot write {} — the sequence is empty", lain::io::canonicalUri(uri));
			return false;
		}

		// The sequence's OWN spec, which is a fact rather than a guess: a sequence is homogeneous,
		// so its declared spec is every frame's. That is what lets the encoder open before a single
		// frame has been decoded.
		std::unique_ptr<VideoWriter> writer = openWriter(uri, sequence.spec(), options);
		if (!writer)
			return false;

		bool ok = true;
		for (std::size_t position = 0; position < sequence.size() && ok; ++position)
		{
			const lain::image::Image image = sequence.image(position);
			if (!image.valid())
			{
				// A hole. Unlike a render, a transcode has no host policy to consult — the caller
				// asked for THESE frames, and a video cannot represent a missing one.
				lain::log::error("io::video: {} produced no image while writing {}",
								 sequence.frame(position).toString(), lain::io::canonicalUri(uri));
				ok = false;
				break;
			}

			ok = writer->write(image);
		}

		// EITHER WAY. A failure leaves a closed, visibly short file rather than a headless one —
		// the same reason the render loop finishes its writers through a single exit.
		const bool finished = writer->finish();
		return ok && finished;
	}
} // namespace lain::io::video
