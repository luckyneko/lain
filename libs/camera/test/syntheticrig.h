#pragma once

// A rig for the registration tests: cameras on an arc around a moving board, and stand-in backends
// answering from its truth. The scene is process-wide, as the stand-ins' registries are; each case
// starts from rig::reset() and changes only what it is about.

#include "testboard.h"
#include "testcamera.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/pose.h>
#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/projection.h>
#include <lain/camera/registration/board.h>
#include <lain/camera/registration/refiner.h>
#include <lain/media/framesource.h>

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
		const board::Specification spec = specification();
		board::DetectionReport report;
		report.provenance = {"truth", "1"};
		if (!scene().sees[camera][group])
		{
			report.rejections.push_back({board::Rejection::NoMarkers, "the camera did not see the board"});
			return report;
		}
		const CameraModel m = model();
		const math::RigidTransformd pose = trueCameraFromBoard(camera, group);
		board::Observation observation;
		observation.frame = frame;
		observation.image = m.image();
		observation.pattern = spec.pattern().fingerprint();
		for (std::uint32_t id = 0; id < spec.pattern().cornerCount(); ++id)
		{
			const math::Vec3d p = pose.apply(*spec.cornerPosition(id));
			const Projection<double> pixel = project(m, p.x, p.y, p.z);
			if (pixel.ok() && contains(m, pixel.u, pixel.v))
				observation.features.push_back({id, {pixel.u, pixel.v}, std::nullopt});
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

	// What the stand-in refiner was asked.
	struct RefinerScript
	{
		std::mutex mutex;
		struct Asked
		{
			bool freeCameras = true;
			std::size_t bodies = 0;
			std::size_t observations = 0;
		};
		std::vector<Asked> asked;
	};
	inline RefinerScript& refinerScript()
	{
		static RefinerScript instance;
		return instance;
	}

	// Hands back the starting estimate, unrefined: what the method module does around a refinement
	// is then the only thing under test. The Ceres refiner has its own tests, in plugins/camera/ceres.
	class PassThroughRefiner : public registration::Refiner
	{
	public:
		Provenance provenance() const override { return {"passthrough", "1"}; }

		registration::Solution refine(const registration::Problem& problem) const override
		{
			{
				std::lock_guard<std::mutex> lock(refinerScript().mutex);
				refinerScript().asked.push_back({problem.freeCameras, problem.referenceFromBody.size(), problem.observations.size()});
			}
			registration::Solution out;
			out.status = registration::RefinementStatus::Converged;
			out.cameraFromReference = problem.cameraFromReference;
			out.referenceFromBody = problem.referenceFromBody;
			return out;
		}
	};

	inline void registerStandIns()
	{
		static const bool once = []
		{
			board::detectorRegistry().registerType<TruthDetector>("truth");
			board::poseSolverRegistry().registerType<TruthPoseSolver>("truth");
			registration::refinerRegistry().registerType<PassThroughRefiner>("passthrough");
			return true;
		}();
		(void)once;
	}

	// The default scene: `cameras` cameras on an arc 1 m from the origin, spread over 1.4 rad and
	// facing the board's front, and `groups` board poses near the origin, each seen by every camera.
	inline void reset(std::size_t cameras = 4, std::size_t groups = 24)
	{
		registerStandIns();
		Scene& s = scene();
		s = Scene{};
		const math::Vec3d centre = centroid(specification());
		for (std::size_t c = 0; c < cameras; ++c)
		{
			const double a = cameras == 1 ? 0.0 : -0.7 + 1.4 * double(c) / double(cameras - 1);
			const math::Vec3d position{std::sin(a), -0.1 * double(c % 2), -std::cos(a)};
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

	inline capture::CaptureGroup group(std::size_t g)
	{
		std::vector<capture::CaptureMember> members;
		for (std::size_t c = 0; c < scene().referenceFromCamera.size(); ++c)
			members.push_back({capture::CameraIdentity{identityOf(c)}, frameOf(c, g)});
		return *capture::CaptureGroup::create(std::move(members)).group;
	}

	// Every capture group with every camera's detection: a camera that did not see the board is still
	// a member, since its frame was captured with the others, and its detection failed.
	inline std::vector<registration::board::GroupObservations> groups()
	{
		std::vector<registration::board::GroupObservations> out;
		for (std::size_t g = 0; g < scene().referenceFromBoard.size(); ++g)
		{
			registration::board::GroupObservations entry{group(g), {}};
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
