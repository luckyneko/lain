#include "lain/core/version.h"

#include <cstdint>
#include <utility>

namespace lain::core
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Parse an all-digits field into a uint32, rejecting empties and overflow.
	static bool parseNumber(std::string_view s, uint32_t& out)
	{
		if (s.empty())
			return false;
		uint64_t v = 0;
		for (char c : s)
		{
			if (c < '0' || c > '9')
				return false;
			v = v * 10 + static_cast<uint64_t>(c - '0');
			if (v > UINT32_MAX)
				return false;
		}
		out = static_cast<uint32_t>(v);
		return true;
	}

	// --- Version ----------------------------------------------------------------

	Version::Version(uint32_t major, uint32_t minor, uint32_t patch, std::string prerelease, std::string build)
		: m_major(major)
		, m_minor(minor)
		, m_patch(patch)
		, m_prerelease(std::move(prerelease))
		, m_build(std::move(build))
	{
	}

	std::string Version::toString() const
	{
		std::string s = std::to_string(m_major) + '.' + std::to_string(m_minor) + '.' + std::to_string(m_patch);
		if (!m_prerelease.empty())
			s += '-' + m_prerelease;
		if (!m_build.empty())
			s += '+' + m_build;
		return s;
	}

	std::optional<Version> Version::parse(std::string_view text)
	{
		// Build metadata first (everything past the first '+'), then pre-release
		// (everything past the first '-' in what remains) — so a '-' inside the build
		// tag stays with the build, and a '-' inside the pre-release tag is preserved.
		std::string build;
		if (const auto plus = text.find('+'); plus != std::string_view::npos)
		{
			const std::string_view b = text.substr(plus + 1);
			if (b.empty())
				return std::nullopt;
			build.assign(b);
			text = text.substr(0, plus);
		}

		std::string prerelease;
		if (const auto dash = text.find('-'); dash != std::string_view::npos)
		{
			const std::string_view p = text.substr(dash + 1);
			if (p.empty())
				return std::nullopt;
			prerelease.assign(p);
			text = text.substr(0, dash);
		}

		// What's left must be exactly "major.minor.patch". The patch field carries no
		// separator, so a trailing ".4" lands in it and fails the all-digits check.
		uint32_t parts[3] = {0, 0, 0};
		for (int i = 0; i < 3; ++i)
		{
			if (i < 2)
			{
				const auto dot = text.find('.');
				if (dot == std::string_view::npos)
					return std::nullopt;
				if (!parseNumber(text.substr(0, dot), parts[i]))
					return std::nullopt;
				text = text.substr(dot + 1);
			}
			else if (!parseNumber(text, parts[2]))
				return std::nullopt;
		}

		return Version{parts[0], parts[1], parts[2], std::move(prerelease), std::move(build)};
	}

	bool Version::operator==(const Version& rhs) const
	{
		return m_major == rhs.m_major && m_minor == rhs.m_minor && m_patch == rhs.m_patch;
	}

	bool Version::operator<(const Version& rhs) const
	{
		if (m_major != rhs.m_major)
			return m_major < rhs.m_major;
		if (m_minor != rhs.m_minor)
			return m_minor < rhs.m_minor;
		return m_patch < rhs.m_patch;
	}
} // namespace lain::core
