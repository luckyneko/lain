#include "clibinders.h"

#include <lain/camera/calibration/attempts.h>	 // calibration::Method, bound by name
#include <lain/camera/serialize/cameramodel.h>	 // cameraModelFromValue — a model file's reader
#include <lain/camera/serialize/registeredrig.h> // registeredRigFromValue — a rig file's reader
#include <lain/core/parse.h>
#include <lain/core/uri.h>
#include <lain/image/image.h>
#include <lain/io/data/load.h>
#include <lain/io/image/load.h>
#include <lain/io/sequence/open.h>
#include <lain/log/log.h>
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>
#include <lain/meta/enums.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	void BoundaryBinders::add(std::type_index type, Binder binder, std::vector<std::string> spellings)
	{
		m_binders[type] = Entry{std::move(binder), std::move(spellings)};
	}

	bool BoundaryBinders::has(std::type_index type) const
	{
		return m_binders.count(type) != 0;
	}

	std::optional<flow::PortValue> BoundaryBinders::bind(std::type_index type, const std::string& value) const
	{
		const auto it = m_binders.find(type);
		if (it == m_binders.end())
			return std::nullopt;
		return it->second.binder(value);
	}

	const std::vector<std::string>& BoundaryBinders::spellings(std::type_index type) const
	{
		static const std::vector<std::string> none;
		const auto it = m_binders.find(type);
		return it == m_binders.end() ? none : it->second.spellings;
	}

	std::string kebabCase(std::string_view name)
	{
		const auto upper = [](char c)
		{ return std::isupper(static_cast<unsigned char>(c)) != 0; };
		const auto lower = [](char c)
		{ return std::islower(static_cast<unsigned char>(c)) != 0; };
		const auto digit = [](char c)
		{ return std::isdigit(static_cast<unsigned char>(c)) != 0; };

		std::string out;
		for (std::size_t i = 0; i < name.size(); ++i)
		{
			const char c = name[i];
			// A word starts at a capital that follows a lower-case letter or a digit (boardThen), or that
			// ends a run of capitals before a lower-case letter (the S of "HTTPServer"). A digit after
			// capitals belongs to them, so BT709 and RGBA8 stay one word.
			const bool starts = i > 0 && upper(c) &&
								(lower(name[i - 1]) || digit(name[i - 1]) ||
								 (upper(name[i - 1]) && i + 1 < name.size() && lower(name[i + 1])));
			if (starts)
				out.push_back('-');
			out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return out;
	}

	// Two spellings of one name agree when they differ only in case and in '-' or '_', so
	// board-then-targetless, BoardThenTargetless and BOARD_THEN_TARGETLESS all name one method.
	static std::string folded(std::string_view text)
	{
		std::string out;
		for (const char c : text)
		{
			if (c != '-' && c != '_')
				out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return out;
	}

	// --- built-in parsers (each: string -> optional<PortValue>) --------------------------

	static std::optional<flow::PortValue> bindInt(const std::string& s)
	{
		const std::optional<int> v = core::parse<int>(s);
		if (!v.has_value())
			return std::nullopt; // not a number, out of range, or trailing junk ("12x")
		flow::PortValue pv;
		pv.set<int>(*v);
		return pv;
	}

	static std::optional<flow::PortValue> bindFloat(const std::string& s)
	{
		const std::optional<float> v = core::parse<float>(s);
		if (!v.has_value())
			return std::nullopt;
		flow::PortValue pv;
		pv.set<float>(*v);
		return pv;
	}

	static std::optional<flow::PortValue> bindBool(const std::string& s)
	{
		std::string t;
		t.reserve(s.size());
		for (const char c : s)
			t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		flow::PortValue pv;
		if (t == "true" || t == "1")
		{
			pv.set<bool>(true);
			return pv;
		}
		if (t == "false" || t == "0")
		{
			pv.set<bool>(false);
			return pv;
		}
		return std::nullopt;
	}

	static std::optional<flow::PortValue> bindString(const std::string& s)
	{
		flow::PortValue pv;
		pv.set<std::string>(s);
		return pv;
	}

	static std::optional<flow::PortValue> bindImage(const std::string& s)
	{
		auto image = io::image::load(s);
		if (!image)
			return std::nullopt;
		flow::PortValue pv;
		pv.set<image::Image>(std::move(*image));
		return pv;
	}

	// A frame sequence binds by OPENING the uri, the way an image binds by loading one — a folder
	// of stills, a "shot.<frame:04>.png" pattern, or a video file, dispatched by
	// io::sequence::open so the cli names no medium.
	static std::optional<flow::PortValue> bindFrameSequence(const std::string& s)
	{
		auto sequence = io::sequence::open(s);
		if (!sequence)
			return std::nullopt;
		flow::PortValue pv;
		pv.set<media::FrameSequence>(std::move(*sequence));
		return pv;
	}

	// A single position, for binding one frame without a sweep. The RANGE form (`0-499`) is the
	// sweep's business and is parsed by framerange.h: a binder answers "what value is this string",
	// and a range is not one value.
	static std::optional<flow::PortValue> bindFramePosition(const std::string& s)
	{
		// core::parse REFUSES a negative here, where std::stoull accepted one and wrapped it: "-12"
		// used to bind position 18446744073709551604, a wrong answer that said nothing about being
		// wrong. A frame position has no negative, so refusing is the only honest reading.
		const std::optional<std::size_t> position = core::parse<std::size_t>(s);
		if (!position.has_value())
			return std::nullopt;

		flow::PortValue pv;
		pv.set<media::FramePosition>(media::FramePosition{*position});
		return pv;
	}

	// A camera model binds by READING a camera-model document, the way an image binds by loading one:
	// what a calibration's `run --model model.json` wrote, or a manufacturer's model typed out. The
	// reader is strict, and its problems are logged here, since a binder can only say yes or no and
	// "could not parse" alone would leave a typo in a coefficient name to be found by hand.
	static std::optional<flow::PortValue> bindCameraModel(const std::string& s)
	{
		const std::optional<data::Value> document = io::data::load(s);
		if (!document)
			return std::nullopt;
		camera::CameraModelRead read = camera::cameraModelFromValue(*document);
		for (const std::string& problem : read.problems)
			log::error("flowview: {}: {}", s, problem);
		if (!read.model)
			return std::nullopt;
		flow::PortValue pv;
		pv.set<camera::CameraModel>(std::move(*read.model));
		return pv;
	}

	// A registered rig binds by READING a rig document: what a registration's `run --rig rig.json` wrote.
	// Strict, with its problems logged, as a camera model's is.
	static std::optional<flow::PortValue> bindRegisteredRig(const std::string& s)
	{
		const std::optional<data::Value> document = io::data::load(s);
		if (!document)
			return std::nullopt;
		camera::registration::RegisteredRigRead read = camera::registration::registeredRigFromValue(*document);
		for (const std::string& problem : read.problems)
			log::error("flowview: {}: {}", s, problem);
		if (!read.rig)
			return std::nullopt;
		flow::PortValue pv;
		pv.set<camera::registration::RegisteredRig>(std::move(*read.rig));
		return pv;
	}

	// The entries of a folder, sorted by name, so element i is the same camera on every run, as
	// listDir's are. Empty when the folder cannot be listed.
	static std::vector<std::filesystem::path> sortedEntries(const std::string& folder, bool (*keep)(const std::filesystem::path&))
	{
		std::error_code ec;
		std::vector<std::filesystem::path> entries;
		if (!std::filesystem::is_directory(folder, ec))
			return entries;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, ec))
		{
			if (keep(entry.path()))
				entries.push_back(entry.path());
		}
		std::sort(entries.begin(), entries.end());
		return entries;
	}

	// A rig's footage binds from a FOLDER: every entry in it, by name, opened as one camera's
	// sequence (a folder of stills, or a video file). Pairs by position with a models folder named
	// the same way. Hidden entries are skipped.
	static std::optional<flow::PortValue> bindFootageList(const std::string& s)
	{
		std::vector<media::FrameSequence> footage;
		for (const std::filesystem::path& entry :
			 sortedEntries(s, [](const std::filesystem::path& p)
						   { return p.filename().string().rfind('.', 0) != 0; }))
		{
			std::optional<media::FrameSequence> sequence = io::sequence::open(core::Uri::fromPath(entry));
			if (!sequence)
			{
				log::error("flowview: {} is not a frame sequence", entry.string());
				return std::nullopt;
			}
			footage.push_back(std::move(*sequence));
		}
		if (footage.empty())
			return std::nullopt;
		flow::PortValue pv;
		pv.set(std::move(footage));
		return pv;
	}

	// A rig's camera models bind from a FOLDER of camera-model documents: every .json in it, by name.
	static std::optional<flow::PortValue> bindModelList(const std::string& s)
	{
		std::vector<camera::CameraModel> models;
		for (const std::filesystem::path& file :
			 sortedEntries(s, [](const std::filesystem::path& p)
						   { return p.extension() == ".json"; }))
		{
			std::optional<flow::PortValue> model = bindCameraModel(file.string());
			if (!model)
				return std::nullopt;
			models.push_back(model->get<camera::CameraModel>());
		}
		if (models.empty())
			return std::nullopt;
		flow::PortValue pv;
		pv.set(std::move(models));
		return pv;
	}

	// An enum binds by NAME, whatever its spelling (see folded), and never by number: a number is how
	// an enum is stored, not how anyone means a choice, and it would change meaning if an enumerator
	// were inserted.
	template <typename E>
	static std::optional<flow::PortValue> bindEnum(const std::string& s)
	{
		const std::string wanted = folded(s);
		for (const auto& [value, name] : meta::enums::entries<E>())
		{
			if (folded(name) == wanted)
			{
				flow::PortValue pv;
				pv.set<E>(value);
				return pv;
			}
		}
		return std::nullopt;
	}

	// One line per enum carried on a port. Its choices are listed in kebab case, in declaration order.
	template <typename E>
	static void addEnum(BoundaryBinders& binders)
	{
		std::vector<std::string> spellings;
		for (const std::string_view name : meta::enums::names<E>())
		{
			// Folding must not join two enumerators, or one spelling would name both and the binder
			// would answer with whichever is declared first.
			assert(std::none_of(spellings.begin(), spellings.end(), [&](const std::string& other)
								{ return folded(other) == folded(name); }));
			spellings.push_back(kebabCase(name));
		}
		binders.add(typeid(E), &bindEnum<E>, std::move(spellings));
	}

	void registerBoundaryBinders(BoundaryBinders& binders)
	{
		binders.add(typeid(int), &bindInt);
		binders.add(typeid(float), &bindFloat);
		binders.add(typeid(bool), &bindBool);
		binders.add(typeid(std::string), &bindString);
		binders.add(typeid(image::Image), &bindImage);
		binders.add(typeid(media::FrameSequence), &bindFrameSequence);
		binders.add(typeid(media::FramePosition), &bindFramePosition);
		binders.add(typeid(camera::CameraModel), &bindCameraModel);
		binders.add(typeid(camera::registration::RegisteredRig), &bindRegisteredRig);
		binders.add(typeid(std::vector<media::FrameSequence>), &bindFootageList);
		binders.add(typeid(std::vector<camera::CameraModel>), &bindModelList);
		addEnum<camera::calibration::Method>(binders);
	}
} // namespace flowview
