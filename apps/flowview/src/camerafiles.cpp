#include "camerafiles.h"

#include <lain/camera/serialize/cameramodel.h>	 // the camera-model document, both ways
#include <lain/camera/serialize/registeredrig.h> // the rig document, both ways
#include <lain/data/value.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/io/sequence/open.h>

#include <algorithm>
#include <system_error>
#include <utility>

namespace flowview
{
	using namespace lain;

	FileResult<camera::CameraModel> loadCameraModel(const core::Uri& uri)
	{
		FileResult<camera::CameraModel> result;
		const std::optional<data::Value> document = io::data::load(uri);
		if (!document)
		{
			result.problems.push_back(uri.toString() + ": could not be loaded as a document");
			return result;
		}
		camera::CameraModelRead read = camera::cameraModelFromValue(*document);
		for (const std::string& problem : read.problems)
			result.problems.push_back(uri.toString() + ": " + problem);
		result.value = std::move(read.model);
		return result;
	}

	bool saveCameraModel(const core::Uri& uri, const camera::CameraModel& model)
	{
		return io::data::save(uri, camera::cameraModelToValue(model));
	}

	FileResult<camera::registration::RegisteredRig> loadRegisteredRig(const core::Uri& uri)
	{
		FileResult<camera::registration::RegisteredRig> result;
		const std::optional<data::Value> document = io::data::load(uri);
		if (!document)
		{
			result.problems.push_back(uri.toString() + ": could not be loaded as a document");
			return result;
		}
		camera::registration::RegisteredRigRead read = camera::registration::registeredRigFromValue(*document);
		for (const std::string& problem : read.problems)
			result.problems.push_back(uri.toString() + ": " + problem);
		result.value = std::move(read.rig);
		return result;
	}

	bool saveRegisteredRig(const core::Uri& uri, const camera::registration::RegisteredRig& rig)
	{
		return io::data::save(uri, camera::registration::registeredRigToValue(rig));
	}

	// A rig folder's camera entries: every one whose name does not start with '.', sorted by name as
	// listDir sorts. None, with a problem saying why, when the folder cannot be listed.
	static std::vector<std::filesystem::path> rigEntries(const std::filesystem::path& folder, std::vector<std::string>& problems)
	{
		std::vector<std::filesystem::path> entries;
		std::error_code ec;
		if (!std::filesystem::is_directory(folder, ec))
		{
			problems.push_back(folder.string() + ": not a folder");
			return entries;
		}
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, ec))
		{
			if (entry.path().filename().string().rfind('.', 0) != 0)
				entries.push_back(entry.path());
		}
		if (ec)
		{
			problems.push_back(folder.string() + ": could not be listed");
			return {};
		}
		std::sort(entries.begin(), entries.end());
		return entries;
	}

	// One value per camera entry of `folder` that `keep` accepts, each through `one` (an entry to a
	// FileResult<T>). Every entry is tried, so a refusal names each bad one rather than only the first;
	// `none` says what a folder with no such entry lacks.
	template <typename T, typename Keep, typename One>
	static FileResult<std::vector<T>> eachCamera(const std::filesystem::path& folder, const char* none, Keep keep, One one)
	{
		FileResult<std::vector<T>> result;
		std::vector<T> values;
		for (const std::filesystem::path& entry : rigEntries(folder, result.problems))
		{
			if (!keep(entry))
				continue;
			FileResult<T> value = one(entry);
			if (value.value)
				values.push_back(std::move(*value.value));
			result.problems.insert(result.problems.end(), value.problems.begin(), value.problems.end());
		}
		if (result.problems.empty() && values.empty())
			result.problems.push_back(folder.string() + ": " + none);
		if (result.problems.empty())
			result.value = std::move(values);
		return result;
	}

	FileResult<std::vector<camera::CameraModel>> loadRigModels(const std::filesystem::path& folder)
	{
		return eachCamera<camera::CameraModel>(
			folder, "holds no camera-model documents (.json)",
			[](const std::filesystem::path& entry)
			{ return entry.extension() == ".json"; },
			[](const std::filesystem::path& entry)
			{ return loadCameraModel(core::Uri::fromPath(entry)); });
	}

	FileResult<std::vector<media::FrameSequence>> openRigFootage(const std::filesystem::path& folder)
	{
		return eachCamera<media::FrameSequence>(
			folder, "holds no footage",
			[](const std::filesystem::path&)
			{ return true; },
			[](const std::filesystem::path& entry)
			{
				FileResult<media::FrameSequence> footage;
				footage.value = io::sequence::open(core::Uri::fromPath(entry));
				if (!footage.value)
					footage.problems.push_back(entry.string() + ": could not be opened as footage");
				return footage;
			});
	}
} // namespace flowview
