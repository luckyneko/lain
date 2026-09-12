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
		// path::extension reads the LAST component's extension, so a scheme prefix is harmless and
		// this deliberately runs on the whole text rather than on rest() — a remote uri still has a
		// format, and refusing to name it would make extension() lie for every scheme but one.
		std::string ext = std::filesystem::path(m_text).extension().string();
		if (!ext.empty() && ext.front() == '.')
			ext.erase(ext.begin());
		std::transform(ext.begin(), ext.end(), ext.begin(),
					   [](unsigned char c)
					   { return static_cast<char>(std::tolower(c)); });
		return ext;
	}
} // namespace lain::core
