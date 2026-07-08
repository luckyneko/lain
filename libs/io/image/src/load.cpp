#include "lain/io/image/load.h"

#include <lain/io/read.h>
#include <lain/log/log.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace lain::io::image
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// The lowercase format key for a uri: its file extension without the leading dot,
	// or empty when there is none. path::extension reads the last component's extension,
	// so a scheme prefix ("file://dir/x.PNG") is harmless and the result here is "png".
	static std::string formatKeyFromUri(std::string_view uri)
	{
		std::string ext = std::filesystem::path(uri).extension().string();
		if (!ext.empty() && ext.front() == '.')
			ext.erase(ext.begin());
		std::transform(ext.begin(), ext.end(), ext.begin(),
					   [](unsigned char c)
					   { return static_cast<char>(std::tolower(c)); });
		return ext;
	}

	lain::core::Factory<ImageReader>& readerRegistry()
	{
		static lain::core::Factory<ImageReader> registry;
		return registry;
	}

	std::optional<lain::image::Image> decode(std::string_view formatKey, const memory::Buffer& bytes)
	{
		const std::string key(formatKey);
		const auto reader = readerRegistry().create(key);
		if (!reader)
		{
			log::warn("io::image::decode: no reader registered for format '{}'", key);
			return std::nullopt;
		}

		lain::image::Image image = reader->decode(bytes);
		if (!image.valid())
		{
			log::error("io::image::decode: reader for '{}' rejected {} bytes", key, bytes.size());
			return std::nullopt;
		}
		return image;
	}

	std::optional<lain::image::Image> load(std::string_view uri)
	{
		const std::string key = formatKeyFromUri(uri);
		if (key.empty())
		{
			log::warn("io::image::load: no file extension to select a reader: {}", std::string(uri));
			return std::nullopt;
		}

		const auto bytes = io::read(uri);
		if (!bytes)
			return std::nullopt; // io::read logged the reason already

		return decode(key, *bytes);
	}
} // namespace lain::io::image
