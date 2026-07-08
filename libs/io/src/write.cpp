#include "lain/io/write.h"

#include "scheme.h" // parseUri / isLocalScheme (shared with read.cpp)

#include <lain/log/log.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

namespace lain::io
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Write a Buffer to a local file (created / truncated), or false (reason logged) on a
	// failure. The parent directory must already exist (a missing one fails the open).
	static bool writeLocal(const std::filesystem::path& path, const memory::Buffer& bytes)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			log::warn("io::write: cannot open for writing: {}", path.string());
			return false;
		}

		if (bytes.size() > 0)
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

		if (!file.good())
		{
			log::error("io::write: write failed ({} bytes): {}", bytes.size(), path.string());
			return false;
		}
		return true;
	}

	bool write(std::string_view uri, const memory::Buffer& bytes)
	{
		const ParsedUri parsed = parseUri(uri);
		if (isLocalScheme(parsed.scheme))
			return writeLocal(std::filesystem::path(parsed.rest), bytes);

		log::warn("io::write: unsupported scheme '{}' in uri: {}", std::string(parsed.scheme), std::string(uri));
		return false;
	}
} // namespace lain::io
