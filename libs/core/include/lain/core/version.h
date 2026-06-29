#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lain::core
{
	// A semantic-version value: major.minor.patch with optional pre-release and
	// build-metadata tags (e.g. "1.4.0-rc.2+9e1f3c"). Used for app / library identity
	// — surfaced by --version and logged at startup, and clamped down to an
	// acm::Version at the Vulkan-instance boundary (that conversion lives in
	// lain::app; core names nothing outside std).
	//
	// Comparison is on the numeric major/minor/patch triple only — it deliberately
	// does NOT implement SemVer pre-release precedence (so 1.0.0-rc and 1.0.0 compare
	// equal, and the tags never affect ordering). Compare toString() (or the tag
	// accessors) when exact identity including tags matters. parse()/toString()
	// round-trip the full value, tags included.
	class Version
	{
	public:
		Version() = default;
		Version(uint32_t major, uint32_t minor, uint32_t patch,
				std::string prerelease = {}, std::string build = {});

		uint32_t major() const { return m_major; }
		uint32_t minor() const { return m_minor; }
		uint32_t patch() const { return m_patch; }
		const std::string& prerelease() const { return m_prerelease; }
		const std::string& build() const { return m_build; }

		// "major.minor.patch", with "-<prerelease>" and "+<build>" appended when set.
		std::string toString() const;

		// Parse "major.minor.patch[-prerelease][+build]". Returns nullopt unless the
		// numeric core is exactly three non-negative integers; tags are taken verbatim
		// (not validated against the SemVer identifier grammar) and may not be empty
		// when their separator is present.
		static std::optional<Version> parse(std::string_view text);

		// Precedence on the numeric triple only (tags ignored — see the class note).
		bool operator==(const Version& rhs) const;
		bool operator!=(const Version& rhs) const { return !(*this == rhs); }
		bool operator<(const Version& rhs) const;
		bool operator<=(const Version& rhs) const { return !(rhs < *this); }
		bool operator>(const Version& rhs) const { return rhs < *this; }
		bool operator>=(const Version& rhs) const { return !(*this < rhs); }

	private:
		uint32_t m_major{0};
		uint32_t m_minor{0};
		uint32_t m_patch{0};
		std::string m_prerelease;
		std::string m_build;
	};
} // namespace lain::core
