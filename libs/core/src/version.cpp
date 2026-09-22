#include "lain/core/version.h"

#include "lain/core/parse.h"

#include <cstdint>
#include <utility>

namespace lain::core
{
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

		// What's left must be exactly "major.minor.patch". Each field is parsed WHOLE, so "1x"
		// and an empty field are refused; the patch field carries no separator, so a trailing
		// ".4" lands in it and fails that same test.
		uint32_t parts[3] = {0, 0, 0};
		for (int i = 0; i < 3; ++i)
		{
			std::string_view field = text;
			if (i < 2)
			{
				const auto dot = text.find('.');
				if (dot == std::string_view::npos)
					return std::nullopt;
				field = text.substr(0, dot);
				text = text.substr(dot + 1);
			}

			const std::optional<uint32_t> value = core::parse<uint32_t>(field);
			if (!value.has_value())
				return std::nullopt;
			parts[i] = *value;
		}

		return Version{parts[0], parts[1], parts[2], std::move(prerelease), std::move(build)};
	}
} // namespace lain::core
