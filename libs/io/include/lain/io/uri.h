#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace lain::io
{
	// The canonical form of `uri` — ONE resource has ONE name, however it was spelled.
	//
	// This exists because identity is built on it. A media::FrameRef names its source by uri and
	// nothing else (ADR-0018: a name you can look up, never a back-pointer), so "./take1.mp4" and
	// "/footage/../footage/take1.mp4" must produce the same string or two references to one frame
	// stop comparing equal — and a saved manifest stops meaning anything. It is the same rule
	// graphio::templateKey already applies to template paths, hoisted here so the image and video
	// openers cannot each grow their own version: a key computed two ways eventually disagrees
	// with itself, and the failure is silent.
	//
	// Local paths resolve through std::filesystem::weakly_canonical, so a path that does not
	// exist yet still has a stable answer (an output pattern, a template about to be written).
	// Symlinks along an existing prefix are resolved. A non-local scheme is returned unchanged —
	// there is nothing to canonicalise until one is actually served.
	//
	// Returns `uri` unchanged if the filesystem refuses to answer, which is the honest fallback:
	// a worse key beats no key.
	[[nodiscard]] std::string canonicalUri(std::string_view uri);

	// The local filesystem path `uri` names, or nullopt when it names something that is not on
	// the local filesystem (a remote scheme).
	//
	// THE conversion from a uri to a path, in one place. Written by hand it is a one-liner —
	// strip the scheme, keep the rest — which is exactly why it kept being written by hand, and
	// why the copies disagreed: one of them skipped the strip entirely and built a path straight
	// out of the whole uri, which is correct for every uri that has no scheme and quietly wrong
	// for every uri that has one. Returning an optional is the point: a caller that cannot serve
	// a remote resource has to SAY so, rather than construct a relative path called "s3:" and
	// carry on.
	//
	// It is the naming rule's other half, and it lives beside it for the same reason canonicalUri
	// and numberField do: a spelling decided in two places eventually disagrees with itself.
	[[nodiscard]] std::optional<std::filesystem::path> localPath(std::string_view uri);

	// Where the number sits in a ####-numbered sequence pattern ("shot.####.png"). `width` is 0
	// when there is no run of '#' at all.
	struct NumberField
	{
		std::size_t offset = 0;
		std::size_t width = 0;
		bool found() const { return width > 0; }
	};

	// The FIRST run of '#' in `pattern`, however long it is.
	//
	// This lives here rather than in either caller because reading a numbered sequence and writing
	// one must agree on where the number goes: io::image::openSequence matches files against this,
	// and a render sweep substitutes into it. Two implementations of "find the # run" means what a
	// sweep writes cannot be read back, and the disagreement is silent — the same shape
	// canonicalUri exists to prevent.
	[[nodiscard]] NumberField numberField(std::string_view pattern);

	// `pattern` with its '#' run replaced by `number`, zero-padded to the run's width.
	//
	// The width is a padding CONVENTION, not a limit: a number too wide for it widens the field
	// rather than being truncated, which keeps the result readable by numberField's matcher (it
	// accepts any run of digits). A pattern with no '#' run comes back unchanged — the caller
	// decides whether that is an error, because for a single write it is fine and for a sweep it
	// would silently overwrite one file N times.
	[[nodiscard]] std::string substituteNumber(std::string_view pattern, unsigned long long number);
} // namespace lain::io
