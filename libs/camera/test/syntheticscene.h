#pragma once

// A scene for the track-extraction tests: fixed cameras on an arc looking at scattered landmarks, or one
// camera moving (resetMoving, resetWandering, resetPanning, resetSliding), and stand-in feature
// backends answering from its truth.
// The scene is process-wide, as the stand-ins' registries are; each case starts from scene::reset() or
// scene::resetMoving() and changes only what it is about.
//
// A frame is a blank image whose first two pixels say which camera and which frame it is, so the
// extractor can answer from the truth without drawing anything. A landmark's descriptor is a number,
// and the matcher compares numbers, so a case says exactly which features look alike.

#include "testcamera.h"

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/feature/extraction.h>
#include <lain/camera/projection.h>
#include <lain/camera/rig.h>
#include <lain/media/framesequence.h>
#include <lain/media/framesource.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lain::camera::testing::scene
{
	// How far a dynamic landmark moves between frames, metres, unless it says otherwise.
	inline const math::Vec3d kVelocity{0.15, 0.0, 0.05};

	struct Landmark
	{
		math::Vec3d point{0.0};								  // reference frame
		std::uint32_t descriptor = 0;						  // how every camera describes it, unless
		std::map<std::size_t, std::uint32_t> descriptorIn;	  // ... a camera describes it otherwise, or
		std::map<std::size_t, std::uint32_t> descriptorAt;	  // ... a frame does (by ordinal), first
		std::set<std::size_t> cameras;						  // who can see it; empty means every camera
		std::set<std::pair<std::size_t, std::size_t>> hidden; // (camera, ordinal) where something covers it
		bool dynamic = false;								  // it moves between frames,
		math::Vec3d velocity = kVelocity;					  // ... this far each
		// A second orientation at its pixel, as SIFT reports a keypoint with two strong orientation
		// peaks: described otherwise, as its rotated descriptor is (turnedOf). `duplicated` gives every
		// camera one; `turnedIn` gives one camera one, described as it says; and a camera in
		// `turnedOnly` finds the second orientation alone, at an angle that makes it its smallest.
		bool duplicated = false;
		std::map<std::size_t, std::uint32_t> turnedIn;
		std::set<std::size_t> turnedOnly;
	};

	// How far a second orientation's descriptor lies from every first one's.
	inline constexpr std::uint32_t kTurned = 1u << 30;

	struct Scene
	{
		std::vector<math::RigidTransformd> referenceFromCamera; // the truth the geometry solver knows
		std::vector<math::RigidTransformd> renderedFrom;		// where each camera's frames were taken from
		// One camera moving (resetMoving, resetPanning): where it was at each frame, referenceFromCamera.
		// Empty for fixed cameras. A moving camera is a pinhole unless the case says otherwise, since a
		// fundamental matrix ignores distortion: a case that extracts tracks keeps the pinhole.
		std::vector<math::RigidTransformd> path;
		std::optional<CameraModel> movingModel; // the moving camera's model, when not the pinhole
		std::vector<Landmark> landmarks;
		std::size_t frames = 9;									   // per camera
		bool approximate = true;								   // whether the matcher runs Approximate search
		std::set<std::pair<std::size_t, std::size_t>> undecodable; // (camera, ordinal) frames that fail to decode
		// What the backends were asked, under logMutex(): decodes per (camera, ordinal), and per
		// nearest() call the first descriptor of each side and their sizes.
		std::map<std::pair<std::size_t, std::size_t>, std::size_t> decodes;
		struct Call
		{
			std::uint32_t firstA = 0, firstB = 0;
			std::size_t sizeA = 0, sizeB = 0;
		};
		std::vector<Call> nearestCalls;
		std::vector<double> fundamentalThresholds; // per fundamentals() call, the threshold handed
		std::vector<double> relativeAngles;		   // per relativePoses() call, the inlier angle handed
	};
	inline Scene& scene()
	{
		static Scene instance;
		return instance;
	}
	inline std::mutex& logMutex()
	{
		static std::mutex instance;
		return instance;
	}

	inline std::string identityOf(std::size_t camera)
	{
		return std::string(camera < 10 ? "cam0" : "cam") + std::to_string(camera);
	}
	inline std::string sourceOf(std::size_t camera)
	{
		return "/scene/" + identityOf(camera);
	}

	inline CameraModel model()
	{
		if (!scene().path.empty())
			return scene().movingModel ? *scene().movingModel : *CameraModel::create(parametersWith(NoDistortion{})).model;
		return *CameraModel::create(parametersWith(brownConrady())).model;
	}

	// A camera at `position` looking at `target`, in the camera frame's convention (X right, Y down,
	// Z forward), with the reference frame's +Y as down.
	inline math::RigidTransformd lookingAt(const math::Vec3d& position, const math::Vec3d& target)
	{
		const math::Vec3d z = math::normalize(target - position);
		const math::Vec3d x = math::normalize(math::cross(math::Vec3d{0, 1, 0}, z));
		const math::Vec3d y = math::cross(z, x);
		return math::RigidTransformd{math::quat_cast(math::Mat3d{x, y, z}), position};
	}

	inline std::uint32_t descriptorOf(const Landmark& l, std::size_t camera, std::size_t ordinal)
	{
		if (const auto at = l.descriptorAt.find(ordinal); at != l.descriptorAt.end())
			return at->second;
		const auto found = l.descriptorIn.find(camera);
		return found != l.descriptorIn.end() ? found->second : l.descriptor;
	}

	// How `camera` describes the landmark's second orientation in frame `ordinal`, if it finds one.
	inline std::optional<std::uint32_t> turnedOf(const Landmark& l, std::size_t camera, std::size_t ordinal)
	{
		const auto found = l.turnedIn.find(camera);
		if (found != l.turnedIn.end())
			return found->second;
		if (l.duplicated || l.turnedOnly.count(camera) > 0)
			return descriptorOf(l, camera, ordinal) + kTurned;
		return std::nullopt;
	}

	inline math::Vec3d pointAt(const Landmark& l, std::size_t ordinal)
	{
		return l.dynamic ? l.point + double(ordinal) * l.velocity : l.point;
	}

	// Where `camera` was for frame `ordinal`: a moving camera's place on its path, or a fixed camera's
	// rendering pose.
	inline const math::RigidTransformd& poseAt(std::size_t camera, std::size_t ordinal)
	{
		return scene().path.empty() ? scene().renderedFrom[camera] : scene().path[ordinal];
	}

	// Where `camera` sees landmark `l` in frame `ordinal`, or nothing when it does not.
	inline std::optional<math::Vec2d> pixelOf(std::size_t camera, std::size_t l, std::size_t ordinal)
	{
		const Landmark& landmark = scene().landmarks[l];
		if (!landmark.cameras.empty() && landmark.cameras.count(camera) == 0)
			return std::nullopt;
		if (landmark.hidden.count({camera, ordinal}) != 0)
			return std::nullopt;
		const math::Vec3d p = poseAt(camera, ordinal).inverse().apply(pointAt(landmark, ordinal));
		if (p.z <= 0)
			return std::nullopt;
		const CameraModel m = model();
		const Projection<double> pixel = project(m, p.x, p.y, p.z);
		if (!pixel.ok() || !contains(m, pixel.u, pixel.v))
			return std::nullopt;
		return math::Vec2d{pixel.u, pixel.v};
	}

	// `cameras` on an arc 3 m from the origin, spanning 0.9 rad, and `landmarks` static points in a box
	// around it, each described by its own number 1000 apart from the next, so a feature's nearest
	// neighbour in another image is itself and its second is far.
	inline void reset(std::size_t cameras = 4, std::size_t landmarks = 60, std::uint64_t seed = 1)
	{
		Scene& s = scene();
		s = Scene{};
		for (std::size_t c = 0; c < cameras; ++c)
		{
			const double angle = cameras > 1 ? -0.45 + 0.9 * double(c) / double(cameras - 1) : 0.0;
			const math::Vec3d position{3.0 * std::sin(angle), -0.2 + 0.05 * double(c), -3.0 * std::cos(angle)};
			s.referenceFromCamera.push_back(lookingAt(position, math::Vec3d{0.0}));
		}
		s.renderedFrom = s.referenceFromCamera;
		std::mt19937_64 engine(seed);
		const auto uniform = [&engine](double lo, double hi)
		{ return lo + (hi - lo) * double(engine() >> 11) * 0x1.0p-53; };
		for (std::size_t l = 0; l < landmarks; ++l)
		{
			Landmark landmark;
			landmark.point = math::Vec3d{uniform(-1.0, 1.0), uniform(-0.6, 0.6), uniform(-0.8, 0.8)};
			landmark.descriptor = std::uint32_t(1000 * (l + 1));
			s.landmarks.push_back(landmark);
		}
	}

	// One camera moving through the reset scene's landmarks, `frames` long: along the fixed cameras' arc
	// from −0.4 to 0.4 rad, rising 5 cm a frame, always looking at the origin. Its frames are its own
	// views, so every pair of them shares the landmarks both see. It sees through `camera`, when given,
	// else the pinhole.
	inline void resetMoving(std::size_t frames = 12, std::size_t landmarks = 60, std::uint64_t seed = 1,
							std::optional<CameraModel> camera = std::nullopt)
	{
		reset(1, landmarks, seed);
		Scene& s = scene();
		s.frames = frames;
		s.movingModel = std::move(camera);
		for (std::size_t k = 0; k < frames; ++k)
		{
			const double angle = frames > 1 ? -0.4 + 0.8 * double(k) / double(frames - 1) : 0.0;
			const math::Vec3d position{3.0 * std::sin(angle), -0.3 + 0.05 * double(k), -3.0 * std::cos(angle)};
			s.path.push_back(lookingAt(position, math::Vec3d{0.0}));
		}
	}

	// One camera moving as resetMoving's does, but re-aiming and rolling as it goes, so no two of its
	// optical axes meet: a camera whose every frame looks at one point is the classic case in which no
	// pair of frames says its focal length (the axes of every pair intersect), which a focal sweep over
	// resetMoving's footage would show as a plateau of equal scores.
	inline void resetWandering(std::size_t frames = 12, std::size_t landmarks = 60, std::uint64_t seed = 1,
							   std::optional<CameraModel> camera = std::nullopt)
	{
		reset(1, landmarks, seed);
		Scene& s = scene();
		s.frames = frames;
		s.movingModel = std::move(camera);
		for (std::size_t k = 0; k < frames; ++k)
		{
			const double t = frames > 1 ? double(k) / double(frames - 1) : 0.0;
			const double angle = -0.4 + 0.8 * t;
			const math::Vec3d position{3.0 * std::sin(angle), -0.2 + 0.05 * double(k), -3.0 * std::cos(angle)};
			const math::Vec3d target{0.4 * std::sin(1.7 * double(k) + 0.3), 0.25 * std::sin(2.3 * double(k)), 0.0};
			const math::RigidTransformd aimed = lookingAt(position, target);
			const math::Quatd roll = math::angleAxis(0.15 * std::sin(1.1 * double(k) + 0.5), math::Vec3d{0.0, 0.0, 1.0});
			s.path.push_back(math::RigidTransformd{aimed.rotation() * roll, position});
		}
	}

	// One camera that only turns, `frames` long: standing 3 m from the origin, panning across the reset
	// scene's landmarks from 0.8 m to one side of the origin to 0.8 m to the other. No two frames have
	// any parallax.
	inline void resetPanning(std::size_t frames = 12, std::size_t landmarks = 60, std::uint64_t seed = 1,
							 std::optional<CameraModel> camera = std::nullopt)
	{
		reset(1, landmarks, seed);
		Scene& s = scene();
		s.frames = frames;
		s.movingModel = std::move(camera);
		const math::Vec3d position{0.0, -0.3, -3.0};
		for (std::size_t k = 0; k < frames; ++k)
		{
			const double x = frames > 1 ? -0.8 + 1.6 * double(k) / double(frames - 1) : 0.0;
			s.path.push_back(lookingAt(position, math::Vec3d{x, 0.0, 0.0}));
		}
	}

	// One camera sliding sideways without turning, `frames` long: 3 m in front of the origin, looking
	// along +Z, from 0.8 m to one side to 0.8 m to the other. Every pair of its frames differs by a
	// translation alone, so every focal length explains its rays: a pinhole of the wrong focal length
	// sees the scene stretched in depth, and poses it the same. Footage that says nothing of its focal
	// length, so every start of a focal sweep reconstructs it.
	inline void resetSliding(std::size_t frames = 12, std::size_t landmarks = 60, std::uint64_t seed = 1,
							 std::optional<CameraModel> camera = std::nullopt)
	{
		reset(1, landmarks, seed);
		Scene& s = scene();
		s.frames = frames;
		s.movingModel = std::move(camera);
		for (std::size_t k = 0; k < frames; ++k)
		{
			const double x = frames > 1 ? -0.8 + 1.6 * double(k) / double(frames - 1) : 0.0;
			s.path.push_back(math::RigidTransformd{math::RigidTransformd{}.rotation(), math::Vec3d{x, -0.2, -3.0}});
		}
	}

	// The truth's fundamental matrix from view `a` to view `b` (frames of the moving camera, or fixed
	// cameras): K⁻ᵀ [t]ₓ R K⁻¹ for x_b = R x_a + t, so x_bᵀ F x_a = 0 in pixels. GLM is column-major.
	inline math::Mat3d fundamentalOf(std::size_t a, std::size_t b)
	{
		const std::vector<math::RigidTransformd>& truth = scene().path.empty() ? scene().referenceFromCamera : scene().path;
		const math::RigidTransformd bFromA = truth[b].inverse() * truth[a];
		const Intrinsics in = model().intrinsics();
		math::Mat3d k{1.0};
		k[0][0] = in.fx;
		k[1][1] = in.fy;
		k[2][0] = in.cx;
		k[2][1] = in.cy;
		const math::Vec3d t = bFromA.translation();
		math::Mat3d tx{0.0};
		tx[1][0] = -t.z;
		tx[2][0] = t.y;
		tx[0][1] = t.z;
		tx[2][1] = -t.x;
		tx[0][2] = -t.y;
		tx[1][2] = t.x;
		const math::Mat3d kInverse = math::inverse(k);
		return math::transpose(kInverse) * tx * math::mat3_cast(bFromA.rotation()) * kInverse;
	}

	// The Sampson distance of a pixel pair from `f`, source pixels: restated here, so the stand-in
	// judges its candidates without asking the facade it stands behind.
	inline double sampsonOf(const math::Mat3d& f, const feature::PixelPair& p)
	{
		const math::Vec3d xa{p.a.x, p.a.y, 1.0}, xb{p.b.x, p.b.y, 1.0};
		const math::Vec3d lb = f * xa, la = math::transpose(f) * xb;
		const double denominator = lb.x * lb.x + lb.y * lb.y + la.x * la.x + la.y * la.y;
		return denominator > 0 ? std::abs(math::dot(xb, lb)) / std::sqrt(denominator) : 1e9;
	}

	// The moving camera's footage, named "cam00".
	inline capture::CameraFootage sequenceFootage();

	// A source of blank frames that say which camera and frame they are, counting its decodes, with its
	// cache off so every request is a decode.
	class SceneSource : public media::FrameSource
	{
	public:
		SceneSource(std::size_t camera, std::size_t frames)
			: FrameSource{core::Uri{sourceOf(camera)}, spec(), frames, 0}
			, m_camera(camera)
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
		image::Image decodeFrame(std::size_t ordinal) const override
		{
			{
				std::lock_guard<std::mutex> lock(logMutex());
				++scene().decodes[{m_camera, ordinal}];
			}
			if (scene().undecodable.count({m_camera, ordinal}) != 0)
				return {};
			image::Image frame{640, 480, image::PixelFormat::Gray8};
			frame.data()[0] = std::uint8_t(m_camera);
			frame.data()[1] = std::uint8_t(ordinal);
			return frame;
		}

	private:
		std::size_t m_camera;
	};

	inline capture::CameraFootage sequenceFootage()
	{
		return {capture::CameraIdentity{identityOf(0)}, media::FrameSequence::over(std::make_shared<SceneSource>(0, scene().frames))};
	}

	inline std::vector<RigFootage> footage()
	{
		std::vector<RigFootage> out;
		for (std::size_t c = 0; c < scene().referenceFromCamera.size(); ++c)
		{
			out.push_back({capture::CameraIdentity{identityOf(c)}, model(),
						   media::FrameSequence::over(std::make_shared<SceneSource>(c, scene().frames))});
		}
		return out;
	}

	// Frame k of every camera is group k.
	inline std::vector<capture::CaptureGroup> groups(const std::vector<RigFootage>& cameras)
	{
		std::vector<capture::CameraFootage> byCamera;
		for (const RigFootage& c : cameras)
			byCamera.push_back({c.camera, c.footage});
		return capture::groupsByPosition(byCamera).groups;
	}

	// The scene's truth as a track set, as extraction would accept it from a perfect matcher: one view
	// per camera (frame 0 of its source), and a track per landmark that two or more cameras see in
	// frame 0, at its true pixel plus `sigma` pixels of Gaussian noise per axis (Box-Muller over a
	// seeded engine, since the standard's distributions are implementation-defined). Identities are the
	// landmark's index, zero-padded ("track00042"), so a case controls the order they sort in. Every
	// observation carries the covariance extraction gives a stand-in feature, whose size is 4.
	inline feature::TrackSet trackSetOf(double sigma = 0.0, std::uint64_t seed = 1)
	{
		feature::TrackSet out;
		const CameraModel m = model();
		const std::size_t cameras = scene().referenceFromCamera.size();
		for (std::size_t c = 0; c < cameras; ++c)
		{
			media::FrameRef frame;
			frame.source = core::Uri{sourceOf(c)};
			out.views.push_back({capture::CameraIdentity{identityOf(c)}, m.image(), {frame}});
		}
		std::mt19937_64 engine(seed);
		const auto gaussian = [&engine]
		{
			const double u1 = (double(engine() >> 11) + 0.5) * 0x1.0p-53;
			const double u2 = double(engine() >> 11) * 0x1.0p-53;
			return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
		};
		const double variance = (0.034 * 4.0) * (0.034 * 4.0);
		for (std::size_t l = 0; l < scene().landmarks.size(); ++l)
		{
			feature::Track track;
			std::string digits = std::to_string(l);
			track.identity = "track" + std::string(5 - std::min<std::size_t>(5, digits.size()), '0') + digits;
			for (std::size_t c = 0; c < cameras; ++c)
			{
				const std::optional<math::Vec2d> pixel = pixelOf(c, l, 0);
				if (!pixel)
					continue;
				const double nx = gaussian() * sigma, ny = gaussian() * sigma;
				track.observations.push_back(
					{std::uint32_t(c), *pixel + math::Vec2d{nx, ny}, 1, std::array<double, 3>{variance, 0.0, variance}});
			}
			if (track.observations.size() >= 2)
				out.tracks.push_back(std::move(track));
		}
		return out;
	}

	// The moving camera's truth as a track set, as sequence extraction would accept it from a perfect
	// matcher: one view per frame, every frame of the path in order, and a track per landmark that two
	// or more frames see, at its true pixels plus `sigma` pixels of Gaussian noise per axis, drawn as
	// trackSetOf draws it. Identities and covariances are trackSetOf's.
	inline feature::TrackSet sequenceTrackSetOf(double sigma = 0.0, std::uint64_t seed = 1)
	{
		feature::TrackSet out;
		const CameraModel m = model();
		const std::size_t frames = scene().path.size();
		for (std::size_t k = 0; k < frames; ++k)
		{
			media::FrameRef frame;
			frame.source = core::Uri{sourceOf(0)};
			frame.ordinal = k;
			out.views.push_back({capture::CameraIdentity{identityOf(0)}, m.image(), {frame}});
		}
		std::mt19937_64 engine(seed);
		const auto gaussian = [&engine]
		{
			const double u1 = (double(engine() >> 11) + 0.5) * 0x1.0p-53;
			const double u2 = double(engine() >> 11) * 0x1.0p-53;
			return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
		};
		const double variance = (0.034 * 4.0) * (0.034 * 4.0);
		for (std::size_t l = 0; l < scene().landmarks.size(); ++l)
		{
			feature::Track track;
			std::string digits = std::to_string(l);
			track.identity = "track" + std::string(5 - std::min<std::size_t>(5, digits.size()), '0') + digits;
			for (std::size_t k = 0; k < frames; ++k)
			{
				const std::optional<math::Vec2d> pixel = pixelOf(0, l, k);
				if (!pixel)
					continue;
				const double nx = gaussian() * sigma, ny = gaussian() * sigma;
				track.observations.push_back(
					{std::uint32_t(k), *pixel + math::Vec2d{nx, ny}, 1, std::array<double, 3>{variance, 0.0, variance}});
			}
			if (track.observations.size() >= 2)
				out.tracks.push_back(std::move(track));
		}
		return out;
	}

	// --- the stand-ins ------------------------------------------------------------------

	inline std::uint32_t valueOf(const feature::Features& f, std::size_t i)
	{
		const std::uint8_t* d = f.descriptor(i);
		return std::uint32_t(d[0]) | std::uint32_t(d[1]) << 8 | std::uint32_t(d[2]) << 16 | std::uint32_t(d[3]) << 24;
	}

	// Registered as "truth": every landmark the frame's camera sees in it, where the truth puts it,
	// described by its number at angle 0, and again at its second orientation where it has one.
	class TruthExtractor : public feature::Extractor
	{
	public:
		Provenance provenance() const override { return {"truth", "1"}; }
		std::optional<feature::Features> extract(const image::Image& image, double) const override
		{
			const std::size_t camera = image.data()[0];
			const std::size_t ordinal = image.data()[1];
			feature::Features out;
			out.kind = "truth";
			out.descriptorBytes = 4;
			const auto add = [&out](const math::Vec2d& pixel, double angle, double response, std::uint32_t value)
			{
				out.keypoints.push_back({pixel, 4.0, angle, response});
				for (int b = 0; b < 4; ++b)
					out.descriptors.push_back(std::uint8_t(value >> (8 * b)));
			};
			for (std::size_t l = 0; l < scene().landmarks.size(); ++l)
			{
				const std::optional<math::Vec2d> pixel = pixelOf(camera, l, ordinal);
				if (!pixel)
					continue;
				const Landmark& landmark = scene().landmarks[l];
				const double response = 1.0 + 1.0 / double(l + 1);
				const bool first = landmark.turnedOnly.count(camera) == 0;
				if (first)
					add(*pixel, 0.0, response, descriptorOf(landmark, camera, ordinal));
				if (const std::optional<std::uint32_t> turned = turnedOf(landmark, camera, ordinal))
					add(*pixel, first ? 1.5 : 0.2, response, *turned);
			}
			return out;
		}
	};

	// Registered as "exact": brute force on the difference of the numbers, ties to the lower index. It
	// runs Approximate search too, the same way, while the scene says so.
	class ExactMatcher : public feature::Matcher
	{
	public:
		Provenance provenance() const override { return {"exact", "1"}; }
		bool accepts(std::string_view kind) const override { return kind == "truth"; }
		bool supports(feature::MatchSearch search) const override
		{
			return search == feature::MatchSearch::Exact || scene().approximate;
		}
		std::vector<std::array<feature::Neighbour, 2>> nearest(const feature::Features& a, const feature::Features& b,
															   feature::MatchSearch) const override
		{
			{
				std::lock_guard<std::mutex> lock(logMutex());
				scene().nearestCalls.push_back({valueOf(a, 0), valueOf(b, 0), a.size(), b.size()});
			}
			std::vector<std::array<feature::Neighbour, 2>> out;
			for (std::size_t i = 0; i < a.size(); ++i)
			{
				std::vector<feature::Neighbour> all;
				for (std::size_t j = 0; j < b.size(); ++j)
					all.push_back({std::uint32_t(j), std::abs(double(valueOf(a, i)) - double(valueOf(b, j)))});
				std::partial_sort(all.begin(), all.begin() + 2, all.end(),
								  [](const feature::Neighbour& x, const feature::Neighbour& y)
								  { return x.distance < y.distance || (x.distance == y.distance && x.index < y.index); });
				out.push_back({all[0], all[1]});
			}
			return out;
		}
	};

	// Which of `count` inputs a stand-in samples to judge a candidate: at most 16, evenly spaced.
	inline std::vector<std::size_t> sampleOf(std::size_t count)
	{
		std::vector<std::size_t> out;
		const std::size_t n = std::min<std::size_t>(16, count);
		for (std::size_t i = 0; i < n; ++i)
			out.push_back(i * count / n);
		return out;
	}

	inline double angleBetween(const math::Vec3d& u, const math::Vec3d& v)
	{
		return std::atan2(math::length(math::cross(u, v)), math::dot(u, v));
	}

	// Of `scored` (a candidate's median sampled residual, the candidate), the best few: the ones a robust
	// estimator would return, in order of residual, ties in the order found.
	template <typename Candidate>
	std::vector<Candidate> bestOf(std::vector<std::pair<double, Candidate>> scored)
	{
		std::stable_sort(scored.begin(), scored.end(), [](const auto& x, const auto& y)
						 { return x.first < y.first; });
		std::vector<Candidate> out;
		for (std::size_t i = 0; i < scored.size() && i < 4; ++i)
			out.push_back(scored[i].second);
		return out;
	}

	inline double medianOf(std::vector<double> values)
	{
		std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(values.size() / 2), values.end());
		return values[values.size() / 2];
	}

	// The true poses the geometry stand-in proposes from: the moving camera's frames, or the fixed
	// cameras.
	inline const std::vector<math::RigidTransformd>& truePoses()
	{
		return scene().path.empty() ? scene().referenceFromCamera : scene().path;
	}

	// Registered as "truth": proposes the scene's true poses, in whatever frame and scale it is asked
	// in, and nothing else; lain's measurement picks the one that explains the input. A candidate is
	// proposed only when it explains at least half of a sample of the input, and only the four that
	// explain it best, as a robust estimator's would: a ring of a hundred cameras is symmetric enough
	// that thousands of its true poses explain any one pair. Since it proposes only true poses, rays
	// from a wrong model are never fitted, as a real estimator would fit them.
	class TruthGeometry : public feature::GeometrySolver
	{
	public:
		Provenance provenance() const override { return {"truth", "1"}; }

		// Every ordered pair of true views' bFromA whose epipolar planes the sampled pairs lie in. Two
		// views at one place (a camera that only turns) have no baseline, so they are proposed with an
		// arbitrary unit one, as a real estimator's essential matrix has: every pair of them is then a
		// point at infinity, explained by the rotation alone. Logs the angle it was handed.
		std::vector<math::RigidTransformd> relativePoses(const std::vector<feature::RayPair>& pairs, double angle,
														 std::uint64_t) const override
		{
			{
				std::lock_guard<std::mutex> lock(logMutex());
				scene().relativeAngles.push_back(angle);
			}
			std::vector<std::pair<double, math::RigidTransformd>> scored;
			const std::vector<math::RigidTransformd>& truth = truePoses();
			const std::vector<std::size_t> sample = sampleOf(pairs.size());
			for (std::size_t a = 0; a < truth.size(); ++a)
			{
				for (std::size_t b = 0; b < truth.size(); ++b)
				{
					if (a == b)
						continue;
					math::RigidTransformd bFromA = truth[b].inverse() * truth[a];
					if (math::length(bFromA.translation()) < 1e-12)
						bFromA = math::RigidTransformd{bFromA.rotation(), math::Vec3d{1.0, 0.0, 0.0}};
					std::vector<double> residuals;
					for (const std::size_t i : sample)
					{
						const math::Vec3d normal = math::cross(bFromA.translation(), bFromA.rotate(pairs[i].a));
						residuals.push_back(math::length(normal) > 0
												? std::asin(std::min(1.0, std::abs(math::dot(math::normalize(pairs[i].b),
																							 math::normalize(normal)))))
												: 1.0);
					}
					const double residual = residuals.empty() ? 1.0 : medianOf(residuals);
					if (residual <= angle)
						scored.push_back({residual, bFromA});
				}
			}
			return bestOf(std::move(scored));
		}

		// For every true view c and every true view a whose frame the points may be in, c's pose in
		// a's frame, its translation scaled to the input's (the median of each sampled point-ray's least
		// squares scale), when the sample fits.
		std::vector<math::RigidTransformd> absolutePoses(const std::vector<feature::PointRay>& pointRays, double angle,
														 std::uint64_t) const override
		{
			std::vector<std::pair<double, math::RigidTransformd>> scored;
			const std::vector<math::RigidTransformd>& truth = truePoses();
			const std::vector<std::size_t> sample = sampleOf(pointRays.size());
			for (std::size_t c = 0; c < truth.size(); ++c)
			{
				for (std::size_t a = 0; a < truth.size(); ++a)
				{
					const math::RigidTransformd cFromA = truth[c].inverse() * truth[a];
					const math::Vec3d t = cFromA.translation();
					double k = 0;
					if (math::length(t) > 0)
					{
						// r parallel to R p + k t: cross(r, R p) + k cross(r, t) = 0, for each ray.
						std::vector<double> ks;
						for (const std::size_t i : sample)
						{
							const math::Vec3d rt = math::cross(pointRays[i].ray, t);
							const double length2 = math::dot(rt, rt);
							if (length2 > 0)
								ks.push_back(-math::dot(math::cross(pointRays[i].ray, cFromA.rotate(pointRays[i].point)), rt) /
											 length2);
						}
						if (ks.empty())
							continue;
						std::nth_element(ks.begin(), ks.begin() + std::ptrdiff_t(ks.size() / 2), ks.end());
						k = ks[ks.size() / 2];
						if (!(k > 0) || !std::isfinite(k))
							continue;
					}
					const math::RigidTransformd candidate{cFromA.rotation(), t * k};
					std::vector<double> residuals;
					for (const std::size_t i : sample)
						residuals.push_back(angleBetween(pointRays[i].ray, candidate.apply(pointRays[i].point)));
					const double residual = residuals.empty() ? 1.0 : medianOf(residuals);
					if (residual <= angle)
						scored.push_back({residual, candidate});
				}
			}
			return bestOf(std::move(scored));
		}

		// Every ordered pair of true views' fundamental matrix (fundamentalOf) that the sampled pairs
		// lie within the threshold of, the best four, as its poses are. Logs the threshold it was handed.
		std::vector<math::Mat3d> fundamentals(const std::vector<feature::PixelPair>& pairs, double threshold,
											  std::uint64_t) const override
		{
			{
				std::lock_guard<std::mutex> lock(logMutex());
				scene().fundamentalThresholds.push_back(threshold);
			}
			const std::size_t views = scene().path.empty() ? scene().referenceFromCamera.size() : scene().path.size();
			const std::vector<std::size_t> sample = sampleOf(pairs.size());
			std::vector<std::pair<double, math::Mat3d>> scored;
			for (std::size_t a = 0; a < views; ++a)
			{
				for (std::size_t b = 0; b < views; ++b)
				{
					if (a == b)
						continue;
					const math::Mat3d f = fundamentalOf(a, b);
					std::vector<double> distances;
					for (const std::size_t i : sample)
						distances.push_back(sampsonOf(f, pairs[i]));
					const double distance = distances.empty() ? 1e9 : medianOf(distances);
					if (distance <= threshold)
						scored.push_back({distance, f});
				}
			}
			return bestOf(std::move(scored));
		}
	};

	inline void registerStandIns()
	{
		static const bool once = []
		{
			feature::extractorRegistry().registerType<TruthExtractor>("truth");
			feature::matcherRegistry().registerType<ExactMatcher>("exact");
			feature::geometryRegistry().registerType<TruthGeometry>("truth");
			return true;
		}();
		(void)once;
	}
} // namespace lain::camera::testing::scene
