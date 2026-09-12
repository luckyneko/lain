#pragma once

#include <cstddef>
#include <filesystem>
#include <functional> // std::hash specialisation, in details/uri.inl
#include <optional>
#include <string>
#include <string_view>

namespace lain::core
{
	// The name of one resource — what it is CALLED, and where it is local.
	//
	// It exists to stop a uri being pasted into a std::filesystem::path, which is not a
	// hypothetical: before io::localPath existed, an opener built its path from the WHOLE uri, so
	// "s3://bucket/frames" became a relative directory called "s3:" that it then asked the working
	// directory about. See ADR-0023.
	//
	// IT IS NOT AN RFC 3986 PARSER, and that is a decision rather than an omission. Two of lain's
	// own strings collide with the standard head-on: a sequence pattern ("shot.<frame:04>.png")
	// uses two characters that appear in no production of the grammar, so a conforming parser
	// rejects it outright; and "C:\footage\clip.mp4" parses as SCHEME "C" on a platform lain
	// ships. The opaque "scheme://rest" split below is immune to both by construction. (The
	// pattern collision predates the named form — the retired "####" spelling collided with the
	// fragment delimiter instead. It changed shape; it did not go away.)
	//
	// IT CARRIES NO PATH ALGEBRA. There is no parent(), no filename(), no relative() — none of them
	// means anything for a scheme with no implementation, and every such call in the tree operates
	// on a genuinely local path, which path() hands back. extension() is the exception because it
	// survives that test: "which codec, or which medium" is a real question for every scheme,
	// including one served remotely.
	//
	// Canonicalisation is NOT here: it touches the filesystem, which lain::core does not. It lives
	// beside the transport as io::canonicalise(Uri).
	class Uri
	{
	public:
		// The empty uri — names nothing. Distinct from a uri naming the empty path.
		Uri() = default;

		// Adopt `text` as a uri. Implicit, deliberately: every existing caller holds a string or a
		// string literal, and a conversion that had to be spelled out at ~60 call sites would buy
		// nothing — the type's value is in what it REFUSES to offer afterwards, not in policing
		// where a name came from.
		Uri(std::string text);
		Uri(std::string_view text);
		Uri(const char* text);

		// A local filesystem path, as a uri. The conversion that has to be explicit, because it is
		// the one that can silently mean something else: a Windows path is not a uri and must not
		// be parsed as one (see the C: note above).
		static Uri fromPath(const std::filesystem::path& path);

		// The whole uri, as written.
		const std::string& toString() const { return m_text; }

		// The scheme, without "://". A bare path with no "://" reports "local" — so a caller never
		// has to special-case the absence of one.
		std::string_view scheme() const;

		// Everything after "://", or the whole string when there is none.
		std::string_view rest() const;

		// Whether this names something on the local filesystem (a bare path, file:// or local://).
		bool isLocal() const;

		// The local filesystem path this names, or nullopt when it names something that is not on
		// the local filesystem.
		//
		// Returning an optional is the point: a caller that cannot serve a remote resource has to
		// SAY so, rather than construct a relative path called "s3:" and carry on.
		std::optional<std::filesystem::path> path() const;

		// The lowercase extension, without the leading dot, or empty when there is none — the key
		// every format-keyed registry in the tree looks a codec up by.
		//
		// A member rather than a free function because THREE seams ask it (io::image keys readers
		// and writers by it, io::video claims a set of container extensions, io::sequence
		// dispatches a uri to a medium by it), and a format decided in three places eventually
		// disagrees with itself over a spelling — "MP4", "file://clip.MP4". The failure is the
		// quiet kind: the wrong opener, or none.
		//
		// Read from the LAST path component, so a scheme prefix is harmless. A uri with no
		// extension (a directory of stills) yields an empty string, which is a legitimate answer
		// rather than a failure — it is what "not addressed by format" looks like.
		std::string extension() const;

		bool empty() const { return m_text.empty(); }
		explicit operator bool() const { return !m_text.empty(); }

		// Comparison is on the TEXT, exactly. Two spellings of one resource are two different Uris
		// until io::canonicalise has been applied — which is why identity-bearing code (a
		// media::FrameRef, a template cache key) canonicalises first and says so.
		friend bool operator==(const Uri& a, const Uri& b) { return a.m_text == b.m_text; }
		friend bool operator!=(const Uri& a, const Uri& b) { return !(a == b); }
		friend bool operator<(const Uri& a, const Uri& b) { return a.m_text < b.m_text; }

	private:
		std::string m_text;
	};
} // namespace lain::core

// Uri is hashable: a std::hash specialisation lets one key an unordered_map/set. Its body is
// implementation, so it lives beside this header rather than in it.
#include "lain/core/details/uri.inl"
