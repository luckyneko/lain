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

	// A camera entry of a rig folder, and the camera it names (camerafiles.h).
	struct RigEntry
	{
		std::string camera;
		std::filesystem::path entry;
	};

	// The two kinds of rig folder, which differ in which entries hold a camera and in how one names it.
	enum class RigFolder
	{
		Footage, // every entry, named by its whole name
		Models,	 // every .json, named by its name less ".json"
	};

	// A rig folder's cameras: every entry of `kind` whose name does not start with '.', in the order of
	// the cameras' NAMES, the one order a footage folder and a models folder share. None, with a problem
	// saying why, when the folder cannot be listed.
	static std::vector<RigEntry> rigCameras(const std::filesystem::path& folder, RigFolder kind, std::vector<std::string>& problems)
	{
		std::vector<RigEntry> cameras;
		std::error_code ec;
		if (!std::filesystem::is_directory(folder, ec))
		{
			problems.push_back(folder.string() + ": not a folder");
			return cameras;
		}
		for (const std::filesystem::directory_entry& listed : std::filesystem::directory_iterator(folder, ec))
		{
			const std::filesystem::path& entry = listed.path();
			if (entry.filename().string().rfind('.', 0) == 0)
				continue;
			if (kind == RigFolder::Footage)
				cameras.push_back({entry.filename().string(), entry});
			else if (entry.extension() == ".json")
				cameras.push_back({entry.stem().string(), entry});
		}
		if (ec)
		{
			problems.push_back(folder.string() + ": could not be listed");
			return {};
		}
		std::sort(cameras.begin(), cameras.end(), [](const RigEntry& a, const RigEntry& b)
				  { return a.camera < b.camera; });
		return cameras;
	}

	// One value per camera of `folder`, each through `one` (an entry to a FileResult<T>). Every camera is
	// tried, so a refusal names each bad one rather than only the first; `none` says what a folder with no
	// camera lacks.
	template <typename T, typename One>
	static FileResult<std::vector<T>> eachCamera(const std::filesystem::path& folder, RigFolder kind, const char* none, One one)
	{
		FileResult<std::vector<T>> result;
		std::vector<T> values;
		for (const RigEntry& found : rigCameras(folder, kind, result.problems))
		{
			FileResult<T> value = one(found.entry);
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
		return eachCamera<camera::CameraModel>(folder, RigFolder::Models, "holds no camera-model documents (.json)",
											   [](const std::filesystem::path& entry)
											   { return loadCameraModel(core::Uri::fromPath(entry)); });
	}

	FileResult<std::vector<media::FrameSequence>> openRigFootage(const std::filesystem::path& folder)
	{
		return eachCamera<media::FrameSequence>(folder, RigFolder::Footage, "holds no footage",
												[](const std::filesystem::path& entry)
												{
													FileResult<media::FrameSequence> footage;
													footage.value = io::sequence::open(core::Uri::fromPath(entry));
													if (!footage.value)
														footage.problems.push_back(entry.string() + ": could not be opened as footage");
													return footage;
												});
	}

	std::vector<std::string> unpairedCameras(const std::filesystem::path& footage, const std::filesystem::path& models)
	{
		std::vector<std::string> unlisted; // a folder that cannot be listed is the loaders' to report
		const std::vector<RigEntry> filmed = rigCameras(footage, RigFolder::Footage, unlisted);
		const std::vector<RigEntry> modelled = rigCameras(models, RigFolder::Models, unlisted);
		std::vector<std::string> problems;
		if (!unlisted.empty())
			return problems;

		// Both are in name order, so one walk meets each name once, and reports them in that order.
		auto f = filmed.begin();
		auto m = modelled.begin();
		while (f != filmed.end() || m != modelled.end())
		{
			if (m == modelled.end() || (f != filmed.end() && f->camera < m->camera))
			{
				problems.push_back(f->camera + ": footage " + f->entry.string() + " has no camera model " +
								   (models / (f->camera + ".json")).string());
				++f;
			}
			else if (f == filmed.end() || m->camera < f->camera)
			{
				problems.push_back(m->camera + ": camera model " + m->entry.string() + " has no footage " +
								   (footage / m->camera).string());
				++m;
			}
			else
			{
				++f;
				++m;
			}
		}
		return problems;
	}
} // namespace flowview
