#include "lain/io/image/sequence.h"

#include "lain/io/image/load.h"
#include "lain/io/image/save.h" // formatKeyOf

#include <lain/io/uri.h>
#include <lain/log/log.h>
#include <lain/media/framesource.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace lain::io::image
{
	namespace fs = std::filesystem;

	// --- the source ------------------------------------------------------------

	// The image medium's FrameSource: a list of still paths, decoded one at a time through the
	// ordinary load() facade. It holds NO Stream — a still is read whole by io::read, and only a
	// container that must be seeked into needs something kept open. The base class supplies
	// everything else (the ring cache, the lock, the range check, the spec enforcement), so this
	// is the whole of what the image medium has to say.
	class ImageSequenceSource : public lain::media::FrameSource
	{
	public:
		ImageSequenceSource(std::string uri, lain::media::FrameSpec spec, std::vector<std::string> paths)
			: FrameSource{std::move(uri), spec, paths.size()}
			, m_paths{std::move(paths)}
		{
		}

	protected:
		lain::image::Image decodeFrame(std::size_t ordinal) const override
		{
			// load() already logs its own reason for failing, and an invalid Image is exactly
			// what the base class expects back.
			std::optional<lain::image::Image> image = load(m_paths[ordinal]);
			return image.has_value() ? std::move(*image) : lain::image::Image{};
		}

	private:
		std::vector<std::string> m_paths;
	};

	// --- resolving a uri to an ordered list of files ---------------------------

	// Whether a reader is registered for this file's extension — the same question load() asks,
	// so a directory yields exactly the files load() could open.
	static bool isReadable(const fs::path& path)
	{
		const std::string key = formatKeyOf(path.string());
		return !key.empty() && readerRegistry().contains(key);
	}

	// One matched still: its path, and the number that orders it.
	struct Numbered
	{
		std::string path;
		unsigned long long number = 0;
	};

	// Files matching a "shot.####.png" pattern, ordered NUMERICALLY. The run marks where the
	// number is, not how many digits to insist on — so an unpadded "shot.7.png" is found and
	// sorted as 7, which is the case a lexicographic directory listing gets wrong.
	static std::vector<std::string> filesMatchingPattern(const fs::path& pattern)
	{
		const std::string name = pattern.filename().string();
		// io::numberField, not a local copy: the render sweep substitutes into these patterns
		// through io::substituteNumber, and a matcher that disagreed with the substituter would
		// mean what a sweep writes cannot be read back.
		const lain::io::NumberField field = lain::io::numberField(name);
		const std::string prefix = name.substr(0, field.offset);
		const std::string suffix = name.substr(field.offset + field.width);

		const fs::path directory = pattern.has_parent_path() ? pattern.parent_path() : fs::path{"."};

		std::vector<Numbered> matched;
		std::error_code error;
		for (const fs::directory_entry& entry : fs::directory_iterator{directory, error})
		{
			if (!entry.is_regular_file())
				continue;

			const std::string candidate = entry.path().filename().string();
			if (candidate.size() <= prefix.size() + suffix.size())
				continue;
			if (candidate.compare(0, prefix.size(), prefix) != 0)
				continue;
			if (candidate.compare(candidate.size() - suffix.size(), suffix.size(), suffix) != 0)
				continue;

			const std::string digits = candidate.substr(prefix.size(), candidate.size() - prefix.size() - suffix.size());
			const bool numeric = !digits.empty() && std::all_of(digits.begin(), digits.end(),
																[](unsigned char c)
																{ return std::isdigit(c) != 0; });
			if (!numeric)
				continue;

			matched.push_back({entry.path().string(), std::stoull(digits)});
		}

		std::sort(matched.begin(), matched.end(),
				  [](const Numbered& a, const Numbered& b)
				  { return a.number < b.number; });

		std::vector<std::string> paths;
		paths.reserve(matched.size());
		for (Numbered& entry : matched)
			paths.push_back(std::move(entry.path));
		return paths;
	}

	// Every readable image in a directory, ordered by filename. Lexicographic to match flow's
	// ListDir — two orderings of the same folder would be worse than one imperfect one.
	static std::vector<std::string> filesInDirectory(const fs::path& directory)
	{
		std::vector<std::string> paths;
		std::error_code error;
		for (const fs::directory_entry& entry : fs::directory_iterator{directory, error})
		{
			if (entry.is_regular_file() && isReadable(entry.path()))
				paths.push_back(entry.path().string());
		}
		std::sort(paths.begin(), paths.end());
		return paths;
	}

	// --- the opener ------------------------------------------------------------

	std::optional<lain::media::FrameSequence> openSequence(std::string_view uri, lain::media::FrameRate rate)
	{
		const std::string canonical = lain::io::canonicalUri(uri);
		const std::optional<fs::path> local = lain::io::localPath(canonical);
		if (!local)
		{
			// A still sequence is read frame-by-frame with io::read, which serves the local
			// scheme only. Saying so is the point of asking: building a path out of the whole
			// uri instead would make "s3:" a relative directory name and report the wrong
			// reason for the wrong thing.
			lain::log::error("io::image: {} is not a local path — only local stills open as a sequence", canonical);
			return std::nullopt;
		}
		const fs::path& path = *local;

		std::error_code error;
		const bool isDirectory = fs::is_directory(path, error);
		const bool isPattern = lain::io::numberField(canonical).found();

		if (!isDirectory && !isPattern)
		{
			lain::log::error("io::image: {} is neither a directory nor a ####-numbered pattern", canonical);
			return std::nullopt;
		}

		// The containing directory must exist. This is the missing-vs-empty distinction: a folder
		// that is not there is a failure (nullopt), a folder with nothing in it is an empty
		// sequence — the same rule flow's ListDir already follows.
		const fs::path directory = isDirectory ? path : (path.has_parent_path() ? path.parent_path() : fs::path{"."});
		if (!fs::is_directory(directory, error))
		{
			lain::log::error("io::image: {} does not name an existing directory", directory.string());
			return std::nullopt;
		}

		std::vector<std::string> paths = isDirectory ? filesInDirectory(directory) : filesMatchingPattern(path);
		if (paths.empty())
		{
			lain::log::info("io::image: no images found for {} — an empty sequence", canonical);
			return lain::media::FrameSequence{};
		}

		// The first frame establishes the spec every later frame is checked against. It is the
		// one eager decode: a sequence must be able to state its shape before anything reads it
		// (an encoder is opened from it), and the only honest source for that is a real frame.
		std::optional<lain::image::Image> first = load(paths.front());
		if (!first.has_value())
		{
			lain::log::error("io::image: cannot open {} as a sequence — its first frame {} would not decode",
							 canonical, paths.front());
			return std::nullopt;
		}

		auto source = std::make_shared<ImageSequenceSource>(canonical, lain::media::specOf(*first, rate),
															std::move(paths));
		return lain::media::FrameSequence::over(std::move(source));
	}
} // namespace lain::io::image
