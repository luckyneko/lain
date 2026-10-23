#include "lain/io/mesh/load.h"

#include <lain/io/framepattern.h>
#include <lain/io/transport.h>
#include <lain/log/log.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace lain::io::mesh
{
	namespace fs = std::filesystem;

	lain::core::Factory<MeshReader>& readerRegistry()
	{
		static lain::core::Factory<MeshReader> registry;
		return registry;
	}

	// A name a mesh gives a file, as a relative path: a backslash is a separator too, since a mesh written
	// on Windows names its files that way and would otherwise name one file with backslashes in it.
	static fs::path relative(std::string_view name)
	{
		std::string portable(name);
		std::replace(portable.begin(), portable.end(), '\\', '/');
		return fs::path(portable);
	}

	// The folder of a mesh loaded from a local file.
	class LocalFolder : public MeshFolder
	{
	public:
		explicit LocalFolder(fs::path folder)
			: m_folder(std::move(folder))
		{
		}

		std::optional<memory::Buffer> read(std::string_view name) const override
		{
			return io::read(lain::core::Uri::fromPath(m_folder / relative(name)));
		}

		std::optional<lain::core::Uri> locate(std::string_view name) const override
		{
			return lain::core::Uri::fromPath((m_folder / relative(name)).lexically_normal());
		}

	private:
		fs::path m_folder;
	};

	std::optional<lain::mesh::Mesh> decode(std::string_view formatKey, const memory::Buffer& bytes, const MeshFolder& folder)
	{
		const std::string key(formatKey);
		const auto reader = readerRegistry().create(key);
		if (!reader)
		{
			log::warn("io::mesh::decode: no reader registered for format '{}'", key);
			return std::nullopt;
		}

		std::optional<lain::mesh::Mesh> mesh = reader->decode(bytes, folder);
		if (!mesh)
		{
			log::error("io::mesh::decode: reader for '{}' rejected {} bytes", key, bytes.size());
			return std::nullopt;
		}
		const std::vector<std::string> problems = lain::mesh::validate(*mesh);
		if (!problems.empty())
		{
			for (const std::string& problem : problems)
				log::error("io::mesh::decode: reader for '{}' produced an unusable mesh: {}", key, problem);
			return std::nullopt;
		}
		return mesh;
	}

	std::optional<lain::mesh::Mesh> load(const lain::core::Uri& uri)
	{
		const std::string key = uri.extension();
		if (key.empty())
		{
			log::warn("io::mesh::load: no file extension to select a reader: {}", uri);
			return std::nullopt;
		}

		const auto bytes = io::read(uri);
		if (!bytes)
			return std::nullopt; // io::read logged the reason already

		const std::optional<fs::path> path = uri.path();
		if (path)
			return decode(key, *bytes, LocalFolder(path->parent_path()));
		return decode(key, *bytes, NoFolder{}); // a remote mesh: nothing beside it can be reached yet
	}

	std::optional<lain::mesh::MeshClip> loadClip(const lain::core::Uri& pattern, media::FrameRate rate)
	{
		const std::optional<fs::path> path = pattern.path();
		if (!path)
		{
			log::warn("io::mesh::loadClip: only a local pattern can be listed: {}", pattern);
			return std::nullopt;
		}

		const std::vector<std::string> files = io::numberedFiles(*path);
		if (files.empty())
		{
			log::warn("io::mesh::loadClip: no file matches {}", pattern);
			return std::nullopt;
		}

		lain::mesh::MeshClip clip;
		clip.rate = rate;
		for (const std::string& file : files)
		{
			std::optional<lain::mesh::Mesh> frame = load(lain::core::Uri::fromPath(file));
			if (!frame)
			{
				log::error("io::mesh::loadClip: frame {} of {} did not load, so the clip would jump", file, pattern);
				return std::nullopt;
			}
			clip.frames.push_back(std::move(*frame));
		}
		return clip;
	}
} // namespace lain::io::mesh
