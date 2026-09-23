#include "lain/core/uri.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace lain::core
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Where "://" sits in `text`, or npos. The ONE place the split is decided.
	static std::size_t separator(std::string_view text)
	{
		return text.find("://");
	}

	// --- Uri ---------------------------------------------------------------------

	Uri::Uri(std::string text)
		: m_text(std::move(text))
	{
	}

	Uri::Uri(std::string_view text)
		: m_text(text)
	{
	}

	Uri::Uri(const char* text)
		: m_text(text != nullptr ? text : "")
	{
	}

	Uri Uri::fromPath(const std::filesystem::path& path)
	{
		// No scheme prefix is added. A bare path IS a local uri here, and prefixing "file://" would
		// change what every existing document and manifest on disk says without buying anything —
		// scheme() already answers "local" for one.
		return Uri{path.string()};
	}

	std::string_view Uri::scheme() const
	{
		const std::size_t sep = separator(m_text);
		if (sep == std::string_view::npos)
			return "local"; // a bare path is the local scheme, so no caller special-cases absence
		return std::string_view{m_text}.substr(0, sep);
	}

	std::string_view Uri::rest() const
	{
		const std::size_t sep = separator(m_text);
		if (sep == std::string_view::npos)
			return m_text;
		return std::string_view{m_text}.substr(sep + 3);
	}

	bool Uri::isLocal() const
	{
		const std::string_view s = scheme();
		return s == "local" || s == "file";
	}

	std::optional<std::filesystem::path> Uri::path() const
	{
		if (!isLocal())
			return std::nullopt;
		return std::filesystem::path{rest()};
	}

	std::string Uri::extension() const
	{
		// SCANNED HERE rather than handed to std::filesystem::path, which is what this used to do.
		// On Windows that type also splits a component on ':' — "C:foo" is a real drive-relative
		// path — so "shot.<frame:04>.png" reported its extension as "<frame" there while every
		// other platform said "png". io::sequence dispatches a uri to a medium BY EXTENSION, so a
		// render pattern reached no medium at all on Windows, silently.
		//
		// The deeper reason is that the delegation contradicted what this type is for: a Uri
		// deliberately carries no platform's path semantics (ADR-0023), and borrowing them for one
		// accessor let one in through the back door. Doing the scan here makes the answer the same
		// everywhere, which is the only thing a format-keyed registry can be built on.
		//
		// Both separators, because a Uri legitimately holds either: "s3://bucket/take1.mov" and
		// "C:\footage\clip.mp4" are both things it must answer for.
		const std::size_t lastSeparator = m_text.find_last_of("/\\");
		const std::string_view name = lastSeparator == std::string::npos
										  ? std::string_view{m_text}
										  : std::string_view{m_text}.substr(lastSeparator + 1);

		// A LEADING dot is a name, not an extension (".hidden", and "." / ".." with it), and a
		// trailing one names no format.
		const std::size_t dot = name.find_last_of('.');
		if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size())
			return {};

		std::string ext{name.substr(dot + 1)};
		std::transform(ext.begin(), ext.end(), ext.begin(),
					   [](unsigned char c)
					   { return static_cast<char>(std::tolower(c)); });
		return ext;
	}
} // namespace lain::core
