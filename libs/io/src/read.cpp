#include "lain/io/read.h"

#include "scheme.h" // parseUri / isLocalScheme (shared with write.cpp)

#include <lain/log/log.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

namespace lain::io
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Read an entire local file into a Buffer, or nullopt (reason logged) on failure.
	static std::optional<memory::Buffer> readLocal(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec))
		{
			log::warn("io::read: not a readable file: {}", path.string());
			return std::nullopt;
		}

		// ate: open positioned at the end so tellg() yields the size in one seek.
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
		{
			log::warn("io::read: cannot open: {}", path.string());
			return std::nullopt;
		}

		const std::streamoff end = file.tellg();
		if (end < 0)
		{
			log::error("io::read: cannot determine size: {}", path.string());
			return std::nullopt;
		}

		const auto size = static_cast<std::size_t>(end);
		memory::Buffer buffer{size};
		if (size > 0)
		{
			file.seekg(0);
			file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
			if (static_cast<std::size_t>(file.gcount()) != size)
			{
				log::error("io::read: short read ({} of {} bytes): {}", file.gcount(), size, path.string());
				return std::nullopt;
			}
		}
		return buffer;
	}

	std::optional<memory::Buffer> read(std::string_view uri)
	{
		const ParsedUri parsed = parseUri(uri);
		if (isLocalScheme(parsed.scheme))
			return readLocal(std::filesystem::path(parsed.rest));

		log::warn("io::read: unsupported scheme '{}' in uri: {}", std::string(parsed.scheme), std::string(uri));
		return std::nullopt;
	}
} // namespace lain::io
