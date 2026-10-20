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

	// A rig folder holds one entry per camera: every entry whose name does not start with '.', sorted by
	// name, so element i is the same camera on every run and the order is the one listDir gives. The two
	// below pair by position when both folders name their cameras alike.

	// A rig's camera models: every `.json` entry, each read as loadCameraModel reads one. Refused, with
	// every problem, when any of them is, or when there is none.
	FileResult<std::vector<lain::camera::CameraModel>> loadRigModels(const std::filesystem::path& folder);

	// A rig's footage: every entry opened as one camera's sequence (a folder of stills, or a video file).
	// Refused, with every problem, when any of them does not open, or when there is none.
	FileResult<std::vector<lain::media::FrameSequence>> openRigFootage(const std::filesystem::path& folder);
} // namespace flowview
