#pragma once

#include <lain/image/image.h>
#include <lain/memory/buffer.h>

#include <cstdint>
#include <optional>

namespace lain::io::image
{
	// How hard a writer should squeeze — THE JOB, never a codec's own number.
	//
	// The three encoders here spell one question three incompatible ways: png has a zlib effort
	// level 0-9, tiff has a choice of ALGORITHM, and jpeg has neither. A seam carrying any of those
	// would be carrying one codec's vocabulary, and a document or a command line naming it would
	// stop meaning anything the moment a format joined — the same reason io::video::VideoCodec names
	// a family rather than "h264_videotoolbox". So a caller says what it wants and each codec maps
	// that onto whatever it actually has.
	//
	// Default is "WHAT THIS CODEC ALREADY DID", not "what the library defaults to", and the
	// distinction is load-bearing rather than pedantic: read the other way, a tiff falls back to
	// uncompressed and every tiff lain writes silently grows. It is stated here because it is the
	// one arm a tidy pass would "correct".
	enum class Compression
	{
		Default, // what this codec already did before it had a knob
		None,	 // store it: the fastest write and the largest file
		Fast,	 // cheap compression, a larger file
		Small,	 // work harder, a smaller file
	};

	// What a writer may be told beyond the image.
	//
	// Deliberately two fields, and the omissions are the design — png's filter strategy, tiff's
	// deflate level and a jpeg chroma-subsampling choice are all KNOBS, and the rule above is that
	// the seam names none.
	//
	// THE TWO FIELDS ANSWER DIFFERENT QUESTIONS AND MUST NOT BE MERGED. Compression trades time for
	// size and changes no pixel; quality trades fidelity for size and cannot be undone. libtiff even
	// spells its deflate LEVEL "ZIPQUALITY", which is exactly the confusion worth refusing: one
	// field meaning fidelity in one codec and speed in another is a value that disagrees with
	// itself.
	struct ImageWriterOptions
	{
		Compression compression = Compression::Default;

		// Fidelity for a LOSSY format, 1..100. Unset means the codec's own default, which is why
		// this is an optional rather than a 0 sentinel: "no quality asked for" and "quality 0" are
		// different requests (the rule RunOptions::frameRange already states), and it keeps
		// "default options change nothing" true by construction rather than by the seam happening
		// to spell the same number the codec does.
		//
		// A LOSSLESS codec ignores it — ask isLossy() before offering it to anyone.
		std::optional<std::uint8_t> quality;
	};

	// An ImageWriter encodes a CPU lain::image::Image into a memory::Buffer of one format's
	// bytes — the encode counterpart of ImageReader. One implementation per format
	// (PngWriter, TiffWriter, …); registered into the writer registry by a lowercase format
	// key. Stateless; the registry makes one per encode.
	//
	// Like ImageReader, this interface names no third-party codec — including in its options, which
	// is what ImageWriterOptions above is for.
	class ImageWriter
	{
	public:
		virtual ~ImageWriter() = default;

		// Whether this writer can encode `image` WITHOUT LOSS — the format natively represents
		// its pixel format, channel count, and bit depth. A codec must NOT silently degrade an
		// input it can't hold (JPEG has no alpha; PNG has no float): it reports false here, and
		// the seam rejects loudly, leaving any lossy conversion (e.g. dropping alpha) to the
		// caller's explicit convert(). The precondition for encode().
		//
		// It takes no options, and that is a decision rather than an omission: what a format can
		// HOLD is a fact about the image's pixel format, channel count and colour tag
		// (ADR-0003 / ADR-0020), and no knob moves it.
		virtual bool canEncode(const lain::image::Image& image) const = 0;

		// Whether encoding through this format discards fidelity the pixels cannot get back.
		//
		// A DIFFERENT QUESTION FROM canEncode, and the pair is why both exist: canEncode asks
		// whether the format can hold this image, which JPEG can answer yes to while still losing
		// detail in every block. This is what a host asks before offering a quality control — a
		// slider on PNG would be a lie — and what lets it warn when a quality was asked of a codec
		// that ignores it.
		//
		// It is a fact about the format as THIS writer configures it. The day a tiff writer offers
		// JPEG-in-TIFF, this answer becomes options-dependent and the signature has to follow.
		virtual bool isLossy() const = 0;

		// Encode `image` to format bytes, or std::nullopt on an encode error. Precondition:
		// canEncode(image) is true (the seam checks it first). The save() facade logs a nullopt.
		//
		// `options` has no default argument HERE on purpose: a default on a virtual is bound to the
		// static type, so two callers holding the same writer through different types would get
		// different defaults. The defaults live on the free functions in save.h, exactly as
		// io::video::openWriter's do.
		virtual std::optional<memory::Buffer> encode(const lain::image::Image& image,
													 const ImageWriterOptions& options) const = 0;
	};
} // namespace lain::io::image
