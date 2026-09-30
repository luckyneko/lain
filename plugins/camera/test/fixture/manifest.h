#pragma once

#include <lain/data/data.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// The EXTENDED tier of a real-camera fixture (ADR-0016): a larger capture kept outside the repository,
// addressed by a committed, versioned manifest of content hashes. Ordinary CI runs the compact tier;
// the extended tier runs where its data is present, and reports that it was unavailable where it is
// not, never substituting different data.
//
// The manifest is committed beside the compact fixture as extended.json. The data is found under the
// directory LAIN_CAMERA_FIXTURE_DATA names, in a folder called by the manifest's name.
namespace lain::camera::fixture
{
	constexpr std::uint32_t kManifestVersion = 1;
	constexpr const char* kExtendedManifest = "extended.json";
	constexpr const char* kDataRootVariable = "LAIN_CAMERA_FIXTURE_DATA";

	struct ManifestFile
	{
		std::string path;	// relative to the dataset folder, '/'-separated
		std::string sha256; // lower-case hex
		std::uint64_t bytes = 0;
	};
	LAIN_SERIALIZE(ManifestFile, path, sha256, bytes)

	struct Manifest
	{
		std::uint32_t version = kManifestVersion;
		std::string name; // the dataset's folder under the data root
		std::vector<ManifestFile> files;
	};
	LAIN_SERIALIZE(Manifest, version, name, files)

	// Every regular file under `dataset`, hashed with core::sha256, in order of path. Dotfiles (a
	// .DS_Store, say) are left out, since a file browser adds them to any folder it opens. The same
	// hash verifies, so writing and checking cannot disagree about what a file's hash is.
	Manifest manifestOf(const std::filesystem::path& dataset, const std::string& name);

	struct ManifestRead
	{
		std::optional<Manifest> manifest;
		std::vector<std::string> problems;
	};
	ManifestRead readManifest(const std::filesystem::path& file);
	bool writeManifest(const std::filesystem::path& file, const Manifest& manifest);

	enum class TierStatus
	{
		Verified,	 // every listed file is present with its pinned size and hash, and nothing else is
		Unavailable, // there is no data here to check: not a failure, and said so
		Mismatch,	 // data is here and it is not the pinned data: a failure, never a substitution
	};

	struct ExtendedTier
	{
		TierStatus status = TierStatus::Unavailable;
		std::string reason;			// why Unavailable, or every way it is a Mismatch
		std::filesystem::path root; // the dataset folder, when one was found
	};

	// Check the dataset `manifest` pins, looked for as dataRoot / manifest.name. An empty `dataRoot`
	// is Unavailable, as is a data root without the dataset.
	ExtendedTier verifyExtended(const Manifest& manifest, const std::filesystem::path& dataRoot);

	// The data root LAIN_CAMERA_FIXTURE_DATA names, or empty when it is unset.
	std::filesystem::path dataRootFromEnvironment();
} // namespace lain::camera::fixture
