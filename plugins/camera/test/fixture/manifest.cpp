#include "manifest.h"

#include <lain/core/platform.h>
#include <lain/core/sha256.h>
#include <lain/core/uri.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/log/log.h>
#include <lain/string/format.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <system_error>

namespace lain::camera::fixture
{
	namespace fs = std::filesystem;

	namespace
	{
		// A path below `dataset` as the manifest spells it: relative, '/'-separated on every platform.
		std::string relativeName(const fs::path& file, const fs::path& dataset)
		{
			return file.lexically_relative(dataset).generic_string();
		}

		bool hidden(const fs::path& file, const fs::path& dataset)
		{
			for (const fs::path& part : file.lexically_relative(dataset))
				if (!part.empty() && part.string().front() == '.')
					return true;
			return false;
		}

		struct Hashed
		{
			std::string sha256;
			std::uint64_t bytes = 0;
		};

		// Streamed, so an extended dataset of hundreds of megabytes is never held whole.
		std::optional<Hashed> hashFile(const fs::path& file)
		{
			std::ifstream stream(file, std::ios::binary);
			if (!stream)
				return std::nullopt;
			core::Sha256 hash;
			Hashed out;
			std::array<char, 1 << 16> buffer{};
			while (stream)
			{
				stream.read(buffer.data(), std::streamsize(buffer.size()));
				const std::streamsize got = stream.gcount();
				if (got > 0)
				{
					hash.update(buffer.data(), std::size_t(got));
					out.bytes += std::uint64_t(got);
				}
			}
			if (stream.bad())
				return std::nullopt;
			out.sha256 = hash.finish().toString();
			return out;
		}

		// Every non-hidden regular file below `dataset`, by manifest name.
		std::map<std::string, fs::path> filesBelow(const fs::path& dataset)
		{
			std::map<std::string, fs::path> files;
			std::error_code ec;
			for (fs::recursive_directory_iterator it(dataset, ec), end; !ec && it != end; it.increment(ec))
			{
				if (!it->is_regular_file(ec) || hidden(it->path(), dataset))
					continue;
				files.emplace(relativeName(it->path(), dataset), it->path());
			}
			return files;
		}
	} // namespace

	Manifest manifestOf(const fs::path& dataset, const std::string& name)
	{
		Manifest manifest;
		manifest.name = name;
		for (const auto& [path, file] : filesBelow(dataset))
		{
			const std::optional<Hashed> hashed = hashFile(file);
			if (!hashed)
			{
				log::error("could not read '{}' to hash it", file.generic_string());
				continue;
			}
			manifest.files.push_back({path, hashed->sha256, hashed->bytes});
		}
		return manifest;
	}

	ManifestRead readManifest(const fs::path& file)
	{
		ManifestRead result;
		const std::optional<data::Value> value = io::data::load(core::Uri::fromPath(file));
		if (!value)
		{
			result.problems.push_back(lain::string::format("'{}' could not be read", file.generic_string()));
			return result;
		}
		data::StrictRead<Manifest> read = data::fromValueStrict<Manifest>(*value);
		result.problems = std::move(read.problems);
		if (read.value && read.value->version != kManifestVersion)
			result.problems.push_back(lain::string::format("the manifest is version {}, and this build reads version {}",
														   read.value->version, kManifestVersion));
		if (read.value && read.value->name.empty())
			result.problems.push_back("the manifest names no dataset");
		if (result.problems.empty())
			result.manifest = std::move(read.value);
		return result;
	}

	bool writeManifest(const fs::path& file, const Manifest& manifest)
	{
		if (!io::data::save(core::Uri::fromPath(file), data::toValue(manifest)))
		{
			log::error("could not write '{}'", file.generic_string());
			return false;
		}
		return true;
	}

	ExtendedTier verifyExtended(const Manifest& manifest, const fs::path& dataRoot)
	{
		ExtendedTier tier;
		if (dataRoot.empty())
		{
			tier.reason = lain::string::format("{} is not set", kDataRootVariable);
			return tier;
		}
		const fs::path dataset = dataRoot / manifest.name;
		std::error_code ec;
		if (!fs::is_directory(dataset, ec))
		{
			tier.reason = lain::string::format("there is no '{}' in '{}'", manifest.name, dataRoot.generic_string());
			return tier;
		}
		tier.root = dataset;

		// From here the data is present, so anything short of exactly the pinned files is a Mismatch:
		// a failure that names each difference, and never a run on data the manifest does not pin.
		std::map<std::string, fs::path> present = filesBelow(dataset);
		std::vector<std::string> differences;
		for (const ManifestFile& pinned : manifest.files)
		{
			const auto found = present.find(pinned.path);
			if (found == present.end())
			{
				differences.push_back(lain::string::format("'{}' is missing", pinned.path));
				continue;
			}
			const fs::path file = found->second;
			present.erase(found);
			const std::optional<Hashed> hashed = hashFile(file);
			if (!hashed)
				differences.push_back(lain::string::format("'{}' could not be read", pinned.path));
			else if (hashed->bytes != pinned.bytes)
				differences.push_back(lain::string::format("'{}' is {} bytes, and {} are pinned", pinned.path,
														   hashed->bytes, pinned.bytes));
			else if (hashed->sha256 != pinned.sha256)
				differences.push_back(lain::string::format("'{}' has another hash than the pinned one", pinned.path));
		}
		// An unpinned file is a difference too: an extended run reads the folder, so a file slipped in
		// beside the pinned ones would be calibrated from without the manifest having said so.
		for (const auto& [path, file] : present)
		{
			(void)file;
			differences.push_back(lain::string::format("'{}' is not in the manifest", path));
		}

		if (differences.empty())
		{
			tier.status = TierStatus::Verified;
			return tier;
		}
		tier.status = TierStatus::Mismatch;
		for (const std::string& d : differences)
			tier.reason += (tier.reason.empty() ? "" : "; ") + d;
		return tier;
	}

	fs::path dataRootFromEnvironment()
	{
		return fs::path(core::envVar(kDataRootVariable));
	}
} // namespace lain::camera::fixture
