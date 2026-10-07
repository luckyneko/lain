#pragma once

// A rig for the registration tests: cameras on an arc around a moving board, and stand-in backends
// answering from its truth. The scene is process-wide, as the stand-ins' registries are; each case
// starts from rig::reset() and changes only what it is about.

#include "passthroughrefiner.h"
#include "testboard.h"
#include "testcamera.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/pose.h>
#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/projection.h>
#include <lain/camera/registration/board.h>
#include <lain/camera/registration/refiner.h>
#include <lain/media/framesource.h>

#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lain::camera::testing::rig
{
	// A camera at `position` looking at `target`, in the camera frame's convention (X right, Y down,
	// Z forward), with the reference frame's +Y as down.
	inline math::RigidTransformd lookingAt(const math::Vec3d& position, const math::Vec3d& target)
	{
		const math::Vec3d z = math::normalize(target - position);
		const math::Vec3d x = math::normalize(math::cross(math::Vec3d{0, 1, 0}, z));
		const math::Vec3d y = math::cross(z, x);
		return math::RigidTransformd{math::quat_cast(math::Mat3d{x, y, z}), position};
	}

	// The board's centre in its own frame: the mean of its corners.
	inline math::Vec3d centroid(const board::Specification& spec)
	{
		math::Vec3d sum{0.0};
		const std::uint32_t count = spec.pattern().cornerCount();
		for (std::uint32_t id = 0; id < count; ++id)
			sum += *spec.cornerPosition(id) / double(count);
		return sum;
	}

	// A planar board's other pose: its normal reflected across the ray to its centre, turned about
	// that centre. What IPPE returns as a far, nearly square-on board's second solution.
	inline math::RigidTransformd planarFlip(const math::RigidTransformd& cameraFromBoard, const math::Vec3d& boardCentre)
	{
		const math::Vec3d c = cameraFromBoard.apply(boardCentre);
		const math::Vec3d v = math::normalize(c);
		const math::Vec3d n = cameraFromBoard.rotate(math::Vec3d{0, 0, 1});
		const math::Vec3d reflected = 2.0 * math::dot(n, v) * v - n;
		const math::Vec3d axis = math::cross(n, reflected);
		const double s = math::length(axis);
		if (s < 1e-12)
			return cameraFromBoard;
		const math::Quatd turn = math::angleAxis(std::atan2(s, math::dot(n, reflected)), axis / s);
		const math::Quatd rotation = turn * cameraFromBoard.rotation();
		return math::RigidTransformd{rotation, c - rotation * boardCentre};
	}

	struct Scene
	{
		std::vector<math::RigidTransformd> referenceFromCamera; // the truth
		std::vector<math::RigidTransformd> referenceFromBoard;	// per capture group
		std::vector<std::vector<bool>> sees;					// [camera][group]
		// (camera, group) whose frame shows the board somewhere else: a frame from another instant.
		std::map<std::pair<std::size_t, std::size_t>, math::RigidTransformd> elsewhere;
		// (camera, group) whose pose solver ranks the plane's other pose first.
		std::set<std::pair<std::size_t, std::size_t>> flipped;
		double poseNoise = 0;		  // radians: each pose solved is turned about the board's centre by up to this
		std::size_t modelWidth = 640; // the cameras' models; a different width makes one Incompatible
		// Pixel noise on every corner detected, standard deviations in x and y (seeded by camera and
		// group, so every run is the same), and whether the detector reports it as a covariance.
		double pixelNoiseX = 0;
		double pixelNoiseY = 0;
		bool reportCovariance = false;
	};
	inline Scene& scene()
	{
		static Scene instance;
		return instance;
	}

	inline std::string identityOf(std::size_t camera)
	{
		return std::string(camera < 10 ? "cam0" : "cam") + std::to_string(camera);
	}
	inline std::string sourceOf(std::size_t camera)
	{
		return "/rig/" + identityOf(camera);
	}

	inline CameraModel model(std::size_t width = 640)
	{
		CameraModelParameters p = parametersWith(brownConrady());
		p.image.width = std::uint32_t(width);
		p.image.height = std::uint32_t(width * 3 / 4);
		return *CameraModel::create(p).model;
	}

	inline math::RigidTransformd trueCameraFromBoard(std::size_t camera, std::size_t group)
	{
		const Scene& s = scene();
		const auto moved = s.elsewhere.find({camera, group});
		const math::RigidTransformd& referenceFromBoard =
			moved != s.elsewhere.end() ? moved->second : s.referenceFromBoard[group];
		return s.referenceFromCamera[camera].inverse() * referenceFromBoard;
	}

	// The truth relative to `reference`: what a registration should report for `camera`.
	inline math::RigidTransformd trueReferenceFromCamera(std::size_t reference, std::size_t camera)
	{
		return scene().referenceFromCamera[reference].inverse() * scene().referenceFromCamera[camera];
	}

	inline std::size_t cameraOf(const media::FrameRef& frame)
	{
		for (std::size_t c = 0; c < scene().referenceFromCamera.size(); ++c)
		{
			if (frame.source.toString() == sourceOf(c))
				return c;
		}
		FAIL("no rig camera has the source " << frame.source.toString());
		return 0;
	}

	inline media::FrameRef frameOf(std::size_t camera, std::size_t group)
	{
		media::FrameRef f;
		f.source = core::Uri{sourceOf(camera)};
		f.ordinal = group;
		return f;
	}

	// What the camera's detector reports in the group: every corner of the board projected in view,
	// or Failed when the camera did not see the board or saw fewer than four corners.
	inline board::DetectionReport detection(std::size_t camera, std::size_t group, const media::FrameRef& frame)
	{
		static const board::Specification spec = specification();
		board::DetectionReport report;
		report.provenance = {"truth", "1"};
		if (!scene().sees[camera][group])
		{
			report.rejections.push_back({board::Rejection::NoMarkers, "the camera did not see the board"});
			return report;
		}
		static const CameraModel m = model();
		const math::RigidTransformd pose = trueCameraFromBoard(camera, group);
		// Box-Muller over a seeded engine: the standard's distributions are implementation-defined.
		std::mt19937_64 engine(camera * 15485863 + group * 32452843 + 7);
		const auto gaussian = [&]
		{
			const double u1 = (double(engine() >> 11) + 0.5) * 0x1.0p-53;
			const double u2 = double(engine() >> 11) * 0x1.0p-53;
			return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
		};
		const Scene& s = scene();
		board::Observation observation;
		observation.frame = frame;
		observation.image = m.image();
		observation.pattern = spec.pattern().fingerprint();
		for (std::uint32_t id = 0; id < spec.pattern().cornerCount(); ++id)
		{
			const math::Vec3d p = pose.apply(*spec.cornerPosition(id));
			const Projection<double> pixel = project(m, p.x, p.y, p.z);
			const double nx = gaussian() * s.pixelNoiseX, ny = gaussian() * s.pixelNoiseY;
			if (!pixel.ok() || !contains(m, pixel.u, pixel.v))
				continue;
			std::optional<std::array<double, 3>> covariance;
			if (s.reportCovariance)
				covariance = std::array<double, 3>{s.pixelNoiseX * s.pixelNoiseX, 0.0, s.pixelNoiseY * s.pixelNoiseY};
			observation.features.push_back({id, {pixel.u + nx, pixel.v + ny}, covariance});
		}
		if (observation.features.size() < 4)
		{
			report.rejections.push_back({board::Rejection::NoMarkers, "fewer than four corners in view"});
			return report;
		}
		report.observation = observation;
		report.status = board::DetectionStatus::Detected;
		return report;
	}

	// The board's true pose, turned by up to the scene's noise about its centre (seeded by camera and
	// group, so every run is the same), with the plane's other pose first where the scene says so.
	class TruthPoseSolver : public board::PoseSolver
	{
	public:
		Provenance provenance() const override { return {"truth", "1"}; }

		std::vector<math::RigidTransformd> solve(const CameraModel&, const board::Specification& spec,
												 const board::Observation& view) const override
		{
			const std::size_t camera = cameraOf(view.frame);
			const std::size_t group = view.frame.ordinal;
			const math::Vec3d centre = centroid(spec);
			math::RigidTransformd pose = trueCameraFromBoard(camera, group);
			if (scene().poseNoise > 0)
			{
				std::mt19937_64 engine(camera * 7919 + group * 104729 + 1);
				const auto unit = [&]
				{ return double(engine() % 2000001) / 1000000.0 - 1.0; };
				const math::Vec3d axis = math::normalize(math::Vec3d{unit(), unit(), unit() + 1e-6});
				const math::Quatd turn = math::angleAxis(scene().poseNoise * unit(), axis);
				const math::Vec3d c = pose.apply(centre);
				const math::Quatd rotation = turn * pose.rotation();
				pose = math::RigidTransformd{rotation, c - rotation * centre};
			}
			if (scene().flipped.count({camera, group}))
				return {planarFlip(pose, centre), pose};
			return {pose};
		}
	};

	// Reports the scene's detection for the frame, as the footage overload decodes it.
	class TruthDetector : public board::Detector
	{
	public:
		board::DetectionReport detect(const image::Image&, const media::FrameRef& frame, const board::Specification&,
									  const board::DetectionRequest&, double) const override
		{
			return detection(cameraOf(frame), frame.ordinal, frame);
		}
	};

	// The refiner stand-in is shared with the targetless tests (passthroughrefiner.h); named here too.
	using testing::PassThroughRefiner;
	using testing::refinerScript;
	using testing::RefinerScript;

	// The detector and the pose solver. A refiner is the test's choice: the pass-through one above,
	// or the Ceres plugin's.
	inline void registerStandIns()
	{
		static const bool once = []
		{
			board::detectorRegistry().registerType<TruthDetector>("truth");
			board::poseSolverRegistry().registerType<TruthPoseSolver>("truth");
			return true;
		}();
		(void)once;
	}

	using testing::registerPassThroughRefiner;

	// The default scene: `cameras` cameras on an arc `distance` metres from the origin, spread over
	// 1.4 rad and facing the board's front, and `groups` board poses near the origin, each seen by
	// every camera. At 1 m the 168 mm board is small enough that its tilt barely shows (20 mrad of
	// tilt moves a corner 0.07 px), which is no matter to tests of the method module but limits what
	// a real refinement can recover; the Ceres tests stand closer.
	inline void reset(std::size_t cameras = 4, std::size_t groups = 24, double distance = 1.0)
	{
		registerStandIns();
		Scene& s = scene();
		s = Scene{};
		const math::Vec3d centre = centroid(specification());
		for (std::size_t c = 0; c < cameras; ++c)
		{
			const double a = cameras == 1 ? 0.0 : -0.7 + 1.4 * double(c) / double(cameras - 1);
			const math::Vec3d position = distance * math::Vec3d{std::sin(a), -0.1 * double(c % 2), -std::cos(a)};
			s.referenceFromCamera.push_back(lookingAt(position, math::Vec3d{0.0}));
		}
		for (std::size_t g = 0; g < groups; ++g)
		{
			const double t = double(g) / double(groups);
			const math::Quatd turn = math::angleAxis(0.35 * std::sin(6.28318 * t), math::Vec3d{0, 1, 0}) *
									 math::angleAxis(0.3 * std::cos(6.28318 * 2 * t), math::Vec3d{1, 0, 0});
			const math::Vec3d at{0.08 * std::cos(6.28318 * 3 * t), 0.06 * std::sin(6.28318 * 3 * t),
								 0.05 * std::sin(6.28318 * t)};
			s.referenceFromBoard.push_back(math::RigidTransformd{turn, at - turn * centre});
		}
		s.sees.assign(cameras, std::vector<bool>(groups, true));
		std::lock_guard<std::mutex> lock(refinerScript().mutex);
		refinerScript().asked.clear();
	}

	// Groups [first, last) seen by exactly `cameras`.
	inline void seenBy(std::size_t first, std::size_t last, const std::vector<std::size_t>& cameras)
	{
		Scene& s = scene();
		for (std::size_t g = first; g < last; ++g)
		{
			for (std::size_t c = 0; c < s.sees.size(); ++c)
				s.sees[c][g] = false;
			for (const std::size_t c : cameras)
				s.sees[c][g] = true;
		}
	}

	inline std::vector<registration::board::RigCamera> cameras()
	{
		std::vector<registration::board::RigCamera> out;
		for (std::size_t c = 0; c < scene().referenceFromCamera.size(); ++c)
			out.push_back({capture::CameraIdentity{identityOf(c)}, model(scene().modelWidth)});
		return out;
	}

	// Capture group g: every camera, or only those that saw the board.
	inline capture::CaptureGroup group(std::size_t g, bool onlySeeing = false)
	{
		std::vector<capture::CaptureMember> members;
		for (std::size_t c = 0; c < scene().referenceFromCamera.size(); ++c)
		{
			if (!onlySeeing || scene().sees[c][g])
				members.push_back({capture::CameraIdentity{identityOf(c)}, frameOf(c, g)});
		}
		return *capture::CaptureGroup::create(std::move(members)).group;
	}

	// Every capture group with its members' detections. By default every camera is a member, since
	// its frame was captured with the others, and a camera that did not see the board failed to
	// detect it; `onlySeeing` leaves those out, as a large rig's grouping might.
	inline std::vector<registration::board::GroupObservations> groups(bool onlySeeing = false)
	{
		std::vector<registration::board::GroupObservations> out;
		for (std::size_t g = 0; g < scene().referenceFromBoard.size(); ++g)
		{
			registration::board::GroupObservations entry{group(g, onlySeeing), {}};
			for (const capture::CaptureMember& m : entry.group.members())
				entry.detections.push_back(detection(cameraOf(m.frame), g, m.frame));
			out.push_back(std::move(entry));
		}
		return out;
	}

	// Blank frames the stand-in detector never looks at, named as the rig's frames are.
	class BlankSource : public media::FrameSource
	{
	public:
		BlankSource(std::size_t camera, std::size_t frames)
			: FrameSource{core::Uri{sourceOf(camera)}, spec(), frames}
		{
		}
		static media::FrameSpec spec()
		{
			media::FrameSpec s;
			s.extent = {640, 480};
			s.pixelFormat = image::PixelFormat::Gray8;
			return s;
		}

	protected:
		image::Image decodeFrame(std::size_t) const override { return image::Image{640, 480, image::PixelFormat::Gray8}; }
	};

	inline std::vector<registration::board::RigFootage> footage()
	{
		std::vector<registration::board::RigFootage> out;
		for (std::size_t c = 0; c < scene().referenceFromCamera.size(); ++c)
			out.push_back({capture::CameraIdentity{identityOf(c)}, model(scene().modelWidth),
						   media::FrameSequence::over(std::make_shared<BlankSource>(c, scene().referenceFromBoard.size()))});
		return out;
	}
} // namespace lain::camera::testing::rig
