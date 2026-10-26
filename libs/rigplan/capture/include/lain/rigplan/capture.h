#pragma once

#include <lain/render/capture.h>
#include <lain/rigplan/rigplan.h>

#include <archimedes/acmForward.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lain::rigplan
{
	// Writing a synthetic capture (CONTEXT.md, "Synthetic capture"; WORK.md M15 slice 5e): a rig plan
	// rendered to a folder in the shape M9 binds as a rig's footage, with its ground truth beside it.
	//
	//   rig/<camera>/frame.<n>.<ext>  each camera's frames, <n> its capture frame zero-padded to the
	//                                 digits of the last one (at least four), so a name sort is frame
	//                                 order; <ext> is png, tiff or jpg, as its sensor's output says
	//   nominal/<camera>.json         each camera's nominal model, a camera-model document
	//   truth/models/<camera>.json    each camera's realised model
	//   truth/poses.json              every camera's plan-frame pose
	//   truth/rig.json                the realised rig relative to the reference camera, a rig document
	//   truth/board.json              every board's corners in view, per frame
	//   capture.json                  the manifest (rigplan/serialize/capture.h), written LAST
	//
	// So `rig/` binds as M9's --footage and `nominal/` or `truth/models/` as its --models: an
	// uncalibrated rig and a perfectly calibrated one from the same pixels. A folder without
	// capture.json is an incomplete capture.

	// One frame on disk, as onFrameWritten reports it.
	struct WrittenFrame
	{
		std::string camera; // its name
		std::size_t frame = 0;
		std::filesystem::path file;
		std::size_t bytes = 0;
	};

	// What a capture is rendered with beyond its rig plan: where it goes, and the plan's own seed and
	// scales overridden (capturedPlan).
	struct CaptureOptions
	{
		std::filesystem::path out;		   // a folder that does not exist yet, or is empty
		std::optional<std::uint64_t> seed; // the rig plan's otherwise
		// Both deviation scales, locked together (CONTEXT.md, "Deviation scale"); the rig plan's otherwise.
		std::optional<double> deviation;
		std::optional<double> noise; // the noise scale; the rig plan's otherwise
		render::CaptureSettings settings;
		// How many frames may be recorded, encoded and written at once, on the process pool, while the
		// device renders the next; unset is one a pool worker (at least one), since each frame is one
		// worker's job. This is what bounds memory: one ray table, one scene and this many frames,
		// whatever the rig's size. A frame holds its light (16 bytes a pixel) until it is recorded, then
		// its recording (1 to 6) until it is written. Measured at 1080p with noise (M15 slice 5e, 14
		// cores): 4 in flight took 4.0 s for 30 frames, 13 took 1.9 s, since encoding is the slow part.
		std::optional<std::size_t> maxInFlight;
		std::string generator = "lain::rigplan"; // what capture.json names as its writer
		// Called on a pool worker after each frame is written, in no particular order, so it must be
		// thread-safe. Unset is fine.
		std::function<void(const WrittenFrame&)> onFrameWritten;
	};

	// `plan` as a capture renders it: its seed, its deviation scales (both, locked) and its noise scale
	// replaced by the options' where they give one. capture.json embeds this plan, so it alone
	// reproduces the capture.
	RigPlan capturedPlan(const RigPlan& plan, const CaptureOptions& options);

	// How a capture went.
	struct CaptureReport
	{
		// Empty when the capture was written whole. A refusal (anything found before writing) leaves the
		// folder untouched; a failure while writing leaves it without capture.json, an incomplete capture.
		std::vector<std::string> problems;
		bool refused = false; // the problems were found before anything was written
		std::size_t framesWritten = 0;
		std::uint64_t bytesWritten = 0; // the frames' bytes; the documents are not counted
		std::size_t maxInFlight = 0;	// the bound the capture kept to
		// The most frames ever in flight at once (maxInFlight bounds it), and the most ray tables ever on
		// the device at once (one, since a capture streams camera by camera; ADR-0028).
		std::size_t peakInFlight = 0;
		std::size_t peakRayTables = 0;
		// Wall-clock seconds: preparing (every check, the scenes, every camera's ray table built once to
		// be sure it can be), then rendering and writing.
		double prepareSeconds = 0;
		double renderSeconds = 0;
		// Of renderSeconds: the time the device spent capturing, and the time it waited for room among
		// the frames in flight. Waiting much means the pool is the bottleneck, not the device.
		double captureSeconds = 0;
		double waitSeconds = 0;
	};

	// Render `plan`'s synthetic capture on `device` into `options.out`. In order:
	//   1. refuse before writing anything: a folder that exists and is not empty; a rig plan check()
	//      refuses; a realisation that refuses a draw; an asset that will not load (a board with no
	//      board renderer among them); a frame's scene that is incomplete; a camera whose ray table
	//      cannot be built, or whose sensor's codec cannot write what it records (format and curve);
	//      camera names a rig folder's listing would hide (a leading '.') or would pair wrongly with
	//      their models (M9 pairs a rig's footage and models by sorted position, and "a" and "a-b" sort
	//      one way as folders and the other as "a.json" and "a-b.json"); no json writer registered;
	//   2. write the truth and the nominal models (no GPU);
	//   3. render camera by camera, each frame's scene updated on the device (Renderer::update), its
	//      light handed to the process pool to be recorded (Recorder, keyed by the camera's identity and
	//      the frame), encoded and written, at most maxInFlight at once;
	//   4. write capture.json, only when every frame was written.
	// The codecs (image, mesh and data) and any board renderer are the caller's to register.
	CaptureReport writeCapture(const RigPlan& plan, acm::Device& device, const CaptureOptions& options);

	// What one camera's frames will take on disk.
	struct CameraEstimate
	{
		std::string camera;
		std::uint64_t raw = 0;	 // every frame uncompressed, with its file's overhead: the ceiling
		std::uint64_t floor = 0; // every frame as a flat mid-grey picture, with the capture's noise
		// What one file costs whatever its pixels: headers, and a 16-bit TIFF's transfer function, which
		// alone is 128 kB a frame.
		std::uint64_t overhead = 0;
		// Noise has taken away most of what compression could do: a flat picture's pixels, overhead
		// aside, take half their uncompressed size or more.
		bool noiseDefeatsCompression = false;
	};

	// What a capture will take on disk, before anything is rendered: per camera, the ceiling (every
	// frame uncompressed) and the floor (every frame a flat picture at half of full scale, recorded at
	// the capture's noise and encoded through the camera's own codec: a tile and half of it, so a file's
	// fixed overhead and its cost per pixel are each scaled as they grow). A real frame lands between them: texture costs bytes a flat picture does not. When the
	// floor is near the ceiling, noise is what the bytes are spent on, and it says so rather than
	// leaving a person to find out from a full disk (WORK.md M15, "Noise defeats PNG compression").
	struct SizeEstimate
	{
		std::vector<CameraEstimate> cameras; // the rig plan's, in its order
		std::size_t frames = 0;
		std::uint64_t raw = 0;
		std::uint64_t floor = 0;
		std::vector<std::string> problems; // why there is no estimate; empty when there is one

		// A few lines for a person: the totals, then the cameras whose noise defeats compression.
		std::string toString() const;
	};

	// `capturedPlan(plan, options)`'s size on disk. Needs the image codecs registered; no GPU.
	SizeEstimate estimateSize(const RigPlan& plan, const CaptureOptions& options);
} // namespace lain::rigplan
