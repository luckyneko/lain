#pragma once

#include <lain/image/image.h>
#include <lain/io/stream.h>
#include <lain/media/framespec.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace lain::io::video
{
	// Which FAMILY of encoder a writer should use — the job, never an encoder's name.
	//
	// Coarse on purpose. "h264_videotoolbox" is a fact about one platform and one build, so a
	// command line or a document that named it would stop working on the next machine — which is
	// exactly what ADR-0019 forbids ("the writer selects by AVAILABILITY, never by a hardcoded
	// name"). What a caller can honestly ask for is the JOB — deliver this to a player, or archive
	// it — and the backend answers whether this build can do that here, reporting through codec()
	// which encoder it actually found.
	//
	// The delivery / archival split is the one that matters. Decoding is uniform on every target
	// and so is archival ENCODING; only delivery encoding is platform-conditional (ADR-0019's
	// table), so only the first two entries can be refused for reasons that are not about lain.
	//
	// libx264 / libx265 are deliberately absent and can never be added: they are GPL, and linking a
	// build configured with them relicenses the whole distribution regardless of which encoder is
	// actually called. The FFmpeg plugin's tests assert they are not in the linked library at all.
	enum class VideoCodec
	{
		Auto,	// delivery where this build provides one, else an honest refusal — never a fallback
		H264,	// delivery: the codec every player reads
		HEVC,	// delivery: smaller, available on fewer platforms than H264
		ProRes, // archival: intra-only, edit-friendly, everywhere
		FFV1,	// archival and LOSSLESS everywhere — the only family a byte-exact round trip fits through
		MJPEG,	// archival: intra-only, lossy, the widest compatibility floor
	};

	// What a writer may be told beyond the spec.
	//
	// Deliberately two fields, and the omissions are the design. The RATE is not here: it lives in
	// the FrameSpec, which is the one place a sequence's rate lives, and a second copy would need a
	// precedence rule a caller has to remember. GOP length, ProRes profile and FFV1 level are not
	// here either — the same judgement io::image::ImageWriter made about jpeg quality, for the same
	// reason: the shape of encoder config is decided once real encoders have visible needs, not
	// guessed at the seam.
	struct VideoWriterOptions
	{
		VideoCodec codec = VideoCodec::Auto;

		// Target bits per second for the LOSSY families; ignored by FFV1, and by ProRes, which is
		// profile-driven. Zero means "derive from the spec" — a delivery encoder handed no rate
		// target has no defined output, and FFmpeg's own zero default is either a failure to open
		// or a fixed low number nobody chose, so the derivation is documented at the backend
		// rather than hidden.
		std::uint32_t bitsPerSecond = 0;
	};

	// A VideoWriter ENCODES and MUXES one video container — the video medium's peer of ImageWriter,
	// and, with VideoReader, the whole of what a video BACKEND plugin implements.
	//
	// IT BREAKS ImageWriter's SHAPE DELIBERATELY (ADR-0018). An encoder is open-push-finalise and
	// its file is invalid until finalised, so this is a stateful HANDLE rather than
	// encode(asset) -> Buffer: there is no intermediate byte buffer to hand back, and a 500-frame
	// render must never materialise one. That is the second of the two places the image parallel
	// breaks in this milestone; the first was the transport.
	//
	// It is handed an open WriteStream rather than a uri, for the same ADR-0004 reason the reader
	// is handed a ReadStream: the transport is lain's, so a backend never creates a file. The muxer
	// SEEKS BACK over bytes it has already written — an MP4 patches its mdat size and appends its
	// moov once the frames are known — which io::WriteStream serves: its onWrite is positional and
	// a re-acquired handle reopens for update rather than truncating. Slice 3 wrote that down as
	// the reason it works that way; this is the caller it was written for.
	//
	// NOT thread-safe. Its owner serialises every call. One writer, one file, one forward pass.
	class VideoWriter
	{
	public:
		virtual ~VideoWriter() = default;

		// Take `stream` and open the muxer and the encoder: `format` is the container's lowercase
		// extension key ("mp4"), `spec` fixes every frame, `options` says what the caller wants.
		//
		// THE SPEC COMES FIRST AND FIXES EVERYTHING (ADR-0018). Extent, pixel format, colour space,
		// alpha and rate are decided before a frame exists, and every later frame is CHECKED
		// against them rather than rescaled — a conversion chosen here, on a frame the caller did
		// not know would differ, is precisely the silent lossy conversion the homogeneity rule
		// exists to prevent. This is only possible because a sequence is homogeneous, which is what
		// FrameSpec carries and why an encoder can be opened before the first frame arrives.
		//
		// THE CONTAINER IS NAMED, THE CODEC IS NOT. What a file is called says how its streams are
		// muxed and nothing about what encoded them; which encoder can serve options.codec is a
		// fact about this build and this machine that only a backend can establish. Returns false
		// when it cannot — naming the family it could not provide and what this build does offer,
		// never falling back to another family, because a delivery render that quietly became
		// MJPEG is a wrong answer that looks like success.
		//
		// A spec whose rate is UNSPECIFIED is refused by the seam before reaching here: a container
		// must state a timebase, and inventing one would be a lie about the footage.
		[[nodiscard]] virtual bool open(std::unique_ptr<lain::io::WriteStream> stream, std::string_view format,
										const lain::media::FrameSpec& spec, const VideoWriterOptions& options) = 0;

		// The muxer and the encoder actually selected ("mov,mp4,m4a" / "h264_videotoolbox").
		// Reported, not dispatched on — the write-side mirror of VideoReader::container()/codec(),
		// and the only way a caller or a test can see that selection HAPPENED rather than trust
		// that it did. Empty before a successful open().
		virtual const std::string& container() const = 0;
		virtual const std::string& codec() const = 0;

		// Encode and mux one frame. Frames are consumed IN ORDER and their position IS their
		// timestamp: a writer has no seek, so frame n is the n'th write() that returned true. The
		// stream timebase is the reciprocal of the spec's rate, which makes the output
		// constant-rate and exactly as long as the number of frames written.
		//
		// Returns false when `image` does not match the spec (media::matches — refused, never
		// rescaled or relabelled) or when the encoder fails. A false is TERMINAL: the caller stops
		// and calls finish(), which closes honestly over what exists.
		[[nodiscard]] virtual bool write(const lain::image::Image& image) = 0;

		// Flush the encoder's held frames, write the trailer, flush and finish the transport.
		//
		// EXPLICIT and status-returning, because a trailer write can fail and a destructor has
		// nowhere to report it — the same reason io::WriteStream::finish() exists one layer down,
		// and the reason CONTEXT.md's entry says "avoid: a writer whose destructor is the commit".
		//
		// It is CLOSE, NOT COMMIT. After a failed write() this still writes the trailer, so a
		// truncated render leaves a playable file that is visibly short — which is the whole of
		// ADR-0018's default missing-frame policy ("finalise what exists, report the ordinal, exit
		// non-zero"). Idempotent: a second call repeats the first one's answer. An implementation's
		// destructor finishes an unfinished writer so no file is left headless, but only an
		// explicit call can tell you whether that worked.
		[[nodiscard]] virtual bool finish() = 0;
	};
} // namespace lain::io::video
