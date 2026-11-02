#pragma once

#include <lain/render/capture.h>
#include <lain/render/renderer.h>
#include <lain/render/view.h>
#include <lain/rigplan/visibility.h>
#include <lain/task/task.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace rigplan
{
	// A face of one camera's cube to draw, for the map being made (lain::rigplan::visibilityFaces).
	struct VisibilityFaceJob
	{
		std::uint64_t revision = 0; // which map it is for: a face of an abandoned one is not kept
		std::size_t camera = 0;		// into the map's cameras
		std::size_t face = 0;		// into that camera's faces
		lain::render::CaptureFace capture;
		lain::render::View view; // faceView of it, from the camera's nominal pose
	};

	// How the visibility pane makes its map, without a device (CONTEXT.md, "Visibility map"). The pane
	// draws the faces it hands out on the frame loop, a few a frame, and hands back what each drew; a
	// camera whose faces are all back is counted on the process pool (lain::rigplan::seenBy), never on
	// the frame loop, which may not wait on the pool.
	//
	//   * Each face a map needs is handed out once, camera by camera in the plan's order; a camera's
	//     faces are worked out when its turn comes, since that unprojects its image's border.
	//   * At most `heldCameras` cameras' faces are held at once: a camera's are let go of once its count
	//     is collected (poll), and no further camera is started until one is.
	//   * A new revision starts again, abandoning the map under way: a face drawn for it is not kept,
	//     and a count still running finishes and is dropped. One is never started during a gesture (a
	//     drag changes the plan every frame), nor while the static scene waits on a mesh still loading:
	//     the last map stays, out of date, until then.
	//   * The map shown is the last one completed, so a remade map replaces the old one whole rather
	//     than filling in over it. Until a first is complete, the one under way is shown.
	class VisibilitySchedule
	{
	public:
		static constexpr std::size_t kHeldCameras = 4;

		// What a map is counted for: its grid and the cameras it counts (lain::rigplan::visibilityGrid,
		// visibilityCameras).
		struct Request
		{
			lain::rigplan::VisibilityGrid grid;
			std::vector<lain::rigplan::VisibilityCamera> cameras;
		};

		struct Progress
		{
			std::size_t camerasCounted = 0;
			std::size_t cameras = 0;
			std::size_t facesDrawn = 0;
		};

		explicit VisibilitySchedule(std::size_t heldCameras = kHeldCameras, std::uint32_t faceSize = lain::rigplan::kVisibilityFaceSize);
		VisibilitySchedule(const VisibilitySchedule&) = delete;
		VisibilitySchedule& operator=(const VisibilitySchedule&) = delete;
		~VisibilitySchedule(); // waits for the counts still running

		// Once a frame: the map wanted is of `revision`, made from `request()`. A revision other than the
		// one under way (or shown, with none under way) starts it, unless a `gesture` is under way or the
		// scene is not `ready` (a mesh it needs is still loading).
		void want(std::uint64_t revision, bool gesture, bool ready, const std::function<Request()>& request);

		// The next face to draw, or none: every face of the map is handed out, or `heldCameras` cameras'
		// faces are held until poll() collects a count.
		std::optional<VisibilityFaceJob> next();

		// What drawing `job` gave. A face of an abandoned map, or one handed back twice, is not kept; one
		// the device refused is counted as seeing nothing, and named in problems().
		void deliver(const VisibilityFaceJob& job, lain::render::DistanceResult drawn);

		// Keeps the counts that have finished, letting go of their cameras' faces. Never waits.
		void poll();

		// Waits for every count still running, keeping none: before the pool stops.
		void wait();

		// The map shown, the cameras it counted, and a version that changes whenever it does.
		const lain::rigplan::VisibilityMap& map() const;
		const std::vector<lain::rigplan::VisibilityCamera>& cameras() const;
		std::uint64_t shownVersion() const { return m_shownVersion; }

		// The revision of the map under way, or of the one shown when none is; 0 before any.
		std::uint64_t revision() const;
		// Whether the map shown is not of the revision last wanted: under way, or held.
		bool outOfDate() const;
		// Whether the last revision wanted is held for the scene to be ready.
		bool waiting() const { return m_waiting; }
		// Whether a map is under way, and how far it has got.
		bool working() const { return m_making.has_value(); }
		Progress progress() const;
		// The faces the device would not draw, for the map under way or shown.
		const std::vector<std::string>& problems() const;

	private:
		enum class Stage
		{
			Waiting,  // its faces not yet handed out
			Drawing,  // some handed out
			Counting, // every face back, its count on the pool or finished and not yet collected
			Counted,
		};
		struct Camera
		{
			Stage stage = Stage::Waiting;
			std::vector<lain::render::CaptureFace> faces;
			std::size_t handedOut = 0;
			std::vector<lain::rigplan::FaceDistances> drawn;
			std::vector<bool> delivered;
			std::optional<lain::task::Handle<std::vector<bool>>> count;
		};
		struct Made
		{
			std::uint64_t revision = 0;
			Request request;
			lain::rigplan::VisibilityMap map;
			std::vector<Camera> cameras;
			std::size_t facesDrawn = 0;
			std::size_t counted = 0;
			std::vector<std::string> problems;
		};

		void start(std::uint64_t revision, Request request);
		void abandon();
		void startCamera(Made& made, std::size_t c) const;

		std::size_t m_heldCameras;
		std::uint32_t m_faceSize;
		std::optional<Made> m_making;
		std::optional<Made> m_shown;
		std::uint64_t m_shownVersion = 0;
		std::uint64_t m_wanted = 0;
		bool m_waiting = false;
		// Counts of abandoned maps, still running. A list: a Handle moves but cannot be assigned, so it is
		// dropped by unlinking rather than by shuffling a vector.
		std::list<lain::task::Handle<std::vector<bool>>> m_abandoned;
	};
} // namespace rigplan
