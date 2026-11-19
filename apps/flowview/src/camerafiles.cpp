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
		Footage, // every entry: a folder (of stills) named by its whole name, a file (a video) less its extension
		Models,	 // every .json, named by its name less ".json"
	};

	// A rig folder's cameras: every entry of `kind` whose name does not start with '.', in the order of
	// the cameras' NAMES, the one order a footage folder and a models folder share, and entries giving one
	// name in the order of their own. None, with a problem saying why, when the folder cannot be listed.
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
			std::error_code unknown; // what cannot be asked is taken for a file, which will not open as one
			if (kind == RigFolder::Footage && listed.is_directory(unknown))
				cameras.push_back({entry.filename().string(), entry});
			else if (kind == RigFolder::Footage || entry.extension() == ".json")
				cameras.push_back({entry.stem().string(), entry});
		}
		if (ec)
		{
			problems.push_back(folder.string() + ": could not be listed");
			return {};
		}
		std::sort(cameras.begin(), cameras.end(), [](const RigEntry& a, const RigEntry& b)
				  { return a.camera != b.camera ? a.camera < b.camera : a.entry < b.entry; });
		return cameras;
	}

	// One problem for each name more than one entry gives, naming them all: a folder cam0 beside a video
	// cam0.mp4, or cam0.mp4 beside cam0.mov, would each be camera cam0, and a model cam0.json could not say
	// which it is for. A graph keeps such cameras apart by their whole names (capture::identitiesByLocation),
	// but a model is named for one camera, so a rig folder holding them is refused. `cameras` is in name
	// order, so the entries giving one name are adjacent.
	static std::vector<std::string> sharedNames(const std::vector<RigEntry>& cameras)
	{
		std::vector<std::string> problems;
		std::size_t first = 0;
		while (first < cameras.size())
		{
			std::size_t end = first + 1;
			while (end < cameras.size() && cameras[end].camera == cameras[first].camera)
				++end;
			if (end - first > 1)
			{
				std::string entries = cameras[first].entry.string();
				for (std::size_t k = first + 1; k < end; ++k)
					entries += (k + 1 == end ? " and " : ", ") + cameras[k].entry.string();
				problems.push_back(cameras[first].camera + ": " + entries + " would each be camera " + cameras[first].camera +
								   ", and a model " + cameras[first].camera + ".json could not say which it is for");
			}
			first = end;
		}
		return problems;
	}

	// One value per camera of `folder`, each through `one` (an entry to a FileResult<T>). Every camera is
	// tried, so a refusal names each bad one rather than only the first; `none` says what a folder with no
	// camera lacks. Entries that would be one camera are refused, each name with all its entries.
	template <typename T, typename One>
	static FileResult<std::vector<T>> eachCamera(const std::filesystem::path& folder, RigFolder kind, const char* none, One one)
	{
		FileResult<std::vector<T>> result;
		std::vector<T> values;
		const std::vector<RigEntry> cameras = rigCameras(folder, kind, result.problems);
		const std::vector<std::string> shared = sharedNames(cameras);
		result.problems.insert(result.problems.end(), shared.begin(), shared.end());
		for (const RigEntry& found : cameras)
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
		std::vector<RigEntry> filmed = rigCameras(footage, RigFolder::Footage, unlisted);
		const std::vector<RigEntry> modelled = rigCameras(models, RigFolder::Models, unlisted);
		if (!unlisted.empty())
			return {};

		// A name two footage entries give is reported as such, and walked once below.
		std::vector<std::string> problems = sharedNames(filmed);
		filmed.erase(std::unique(filmed.begin(), filmed.end(), [](const RigEntry& a, const RigEntry& b)
								 { return a.camera == b.camera; }),
					 filmed.end());

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
				problems.push_back(m->camera + ": camera model " + m->entry.string() + " has no footage named " +
								   m->camera + " in " + footage.string());
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
