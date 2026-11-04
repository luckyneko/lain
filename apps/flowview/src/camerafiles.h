#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/camera/registration/report.h>
#include <lain/core/uri.h>
#include <lain/media/framesequence.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace flowview
{
	// Camera models and a rig's inputs as FILES, read one way for both media that bind a boundary input:
	// the cli's binders (`--imported model.json`, `--footage rig`, `--models models`) and the gui's
	// Interface-pane editors. They cannot then disagree about which entries of a folder are cameras, the
	// order those come in, or how strictly a model file is read. Writing a model is here too, for `run
	// --model` and the gui's Save... alike, so a model either one saves is a document `--imported` reads
	// back. A registration's rig document is read and written here as well, for `run --rig` and the
	// `--rig` binder, so it has one reader however it arrives.
	//
	// Driver-free (no window, no device), so apps/flowview/test compiles it as it is.

	// What a load hands back: the value, or every reason there is none. Each problem names the file or
	// folder it is about, so a caller logs or shows it as it stands.
	template <typename T>
	struct FileResult
	{
		std::optional<T> value;
		std::vector<std::string> problems; // empty exactly when `value` is set
	};

	// A camera-model document, read STRICTLY (camera::cameraModelFromValue): an unknown key or a
	// misspelled coefficient is a problem naming it, never a silent default.
	FileResult<lain::camera::CameraModel> loadCameraModel(const lain::core::Uri& uri);

	// `model` as a camera-model document, in the data format the uri's extension names. False when it
	// could not be written; io::data::save logs why.
	[[nodiscard]] bool saveCameraModel(const lain::core::Uri& uri, const lain::camera::CameraModel& model);

	// A rig document, what a registration's rig output was written as, read STRICTLY
	// (camera::registration::registeredRigFromValue), as a camera model is.
	FileResult<lain::camera::registration::RegisteredRig> loadRegisteredRig(const lain::core::Uri& uri);

	// `rig` as a rig document, in the data format the uri's extension names. False when it could not be
	// written; io::data::save logs why.
	[[nodiscard]] bool saveRegisteredRig(const lain::core::Uri& uri, const lain::camera::registration::RegisteredRig& rig);

	// A rig folder holds one entry per camera, every entry whose name does not start with '.', and each
	// entry NAMES its camera, as a graph names it (capture::identitiesByLocation): a folder of stills by
	// its whole name, a video file less its extension, so cam0/ and cam0.mp4 are both camera cam0. A model
	// document is named by its name less ".json", so either camera's model is cam0.json: a models folder
	// names each camera as a report and a rig document do. Two footage entries that would be one camera
	// (cam0/ beside cam0.mp4, or cam0.mp4 beside cam0.mov) are refused, naming both, since a model named
	// cam0.json could not say which it is for; a graph built another way keeps them apart by their whole
	// names.
	//
	// Both folders list their cameras in the order of those NAMES, so element i is the same camera on
	// every run, and a camera's footage and its model sit at the same position whenever the two folders
	// name the same cameras. That is how the registration nodes pair them: by position
	// (RigRegistrationNode). Listed by file name instead, a and a-b came one way as folders and the other
	// as a-b.json and a.json, since '-' sorts below '.', and the two cameras were registered with each
	// other's models, silently (2026-11-03; found by M15). What ordering cannot catch, a camera one
	// folder names and the other does not, unpairedCameras names.

	// A rig's camera models: every `.json` entry, each read as loadCameraModel reads one. Refused, with
	// every problem, when any of them is, or when there is none.
	FileResult<std::vector<lain::camera::CameraModel>> loadRigModels(const std::filesystem::path& folder);

	// A rig's footage: every entry opened as one camera's sequence (a folder of stills, or a video file).
	// Refused, with every problem, when any of them does not open, when two would be one camera, or when
	// there is none.
	FileResult<std::vector<lain::media::FrameSequence>> openRigFootage(const std::filesystem::path& folder);

	// Every camera one of a rig's two folders names and the other does not, by name, one problem each:
	// footage with no model, and a model with no footage, each naming the entry it has and what it lacks;
	// and before them, each name two footage entries would share. Empty when the two folders name the same
	// cameras, once each, which is when pairing them by position pairs each camera with its own model;
	// with the same number of cameras named differently, position would pair the rest wrongly and say
	// nothing. Lists the folders without opening an entry, so one that would not open, or a folder that
	// cannot be listed, is the loaders' to report, not this.
	//
	// The cli asks it whenever a run binds one rig's footage and models from two folders (runmode.cpp).
	// The gui does not yet: an Interface-pane editor binds one input from one folder, and never sees the
	// other's. (Footage entries sharing a name it does refuse, since openRigFootage does.)
	std::vector<std::string> unpairedCameras(const std::filesystem::path& footage, const std::filesystem::path& models);
} // namespace flowview
