#pragma once

// A scene for the track-extraction tests: fixed cameras on an arc looking at scattered landmarks, and
// stand-in feature backends answering from its truth. The scene is process-wide, as the stand-ins'
// registries are; each case starts from scene::reset() and changes only what it is about.
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
	struct Landmark
	{
		math::Vec3d point{0.0};								  // reference frame
		std::uint32_t descriptor = 0;						  // how every camera describes it, unless
		std::map<std::size_t, std::uint32_t> descriptorIn;	  // ... a camera describes it otherwise
		std::set<std::size_t> cameras;						  // who can see it; empty means every camera
		std::set<std::pair<std::size_t, std::size_t>> hidden; // (camera, ordinal) where something covers it
		bool dynamic = false;								  // it moves between frames
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
		std::vector<Landmark> landmarks;
		std::size_t frames = 9;	 // per camera
		bool approximate = true; // whether the matcher runs Approximate search
		// What the backends were asked, under logMutex(): decodes per (camera, ordinal), and per
		// nearest() call the first descriptor of each side and their sizes.
		std::map<std::pair<std::size_t, std::size_t>, std::size_t> decodes;
		struct Call
		{
			std::uint32_t firstA = 0, firstB = 0;
			std::size_t sizeA = 0, sizeB = 0;
		};
		std::vector<Call> nearestCalls;
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

	// How far a dynamic landmark moves between frames, metres.
	inline const math::Vec3d kVelocity{0.15, 0.0, 0.05};

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

	inline std::uint32_t descriptorOf(const Landmark& l, std::size_t camera)
	{
		const auto found = l.descriptorIn.find(camera);
		return found != l.descriptorIn.end() ? found->second : l.descriptor;
	}

	// How `camera` describes the landmark's second orientation, if it finds one.
	inline std::optional<std::uint32_t> turnedOf(const Landmark& l, std::size_t camera)
	{
		const auto found = l.turnedIn.find(camera);
		if (found != l.turnedIn.end())
			return found->second;
		if (l.duplicated || l.turnedOnly.count(camera) > 0)
			return descriptorOf(l, camera) + kTurned;
		return std::nullopt;
	}

	inline math::Vec3d pointAt(const Landmark& l, std::size_t ordinal)
	{
		return l.dynamic ? l.point + double(ordinal) * kVelocity : l.point;
	}

	// Where `camera` sees landmark `l` in frame `ordinal`, or nothing when it does not.
	inline std::optional<math::Vec2d> pixelOf(std::size_t camera, std::size_t l, std::size_t ordinal)
	{
		const Landmark& landmark = scene().landmarks[l];
		if (!landmark.cameras.empty() && landmark.cameras.count(camera) == 0)
			return std::nullopt;
		if (landmark.hidden.count({camera, ordinal}) != 0)
			return std::nullopt;
		const math::Vec3d p = scene().renderedFrom[camera].inverse().apply(pointAt(landmark, ordinal));
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
			image::Image frame{640, 480, image::PixelFormat::Gray8};
			frame.data()[0] = std::uint8_t(m_camera);
			frame.data()[1] = std::uint8_t(ordinal);
			return frame;
		}

	private:
		std::size_t m_camera;
	};

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
					add(*pixel, 0.0, response, descriptorOf(landmark, camera));
				if (const std::optional<std::uint32_t> turned = turnedOf(landmark, camera))
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

	// Registered as "truth": every pair of cameras' true relative pose, and every camera's true pose,
	// whatever it is handed; lain's measurement picks the one that explains the input.
	class TruthGeometry : public feature::GeometrySolver
	{
	public:
		Provenance provenance() const override { return {"truth", "1"}; }
		std::vector<math::RigidTransformd> relativePoses(const std::vector<feature::RayPair>&, double,
														 std::uint64_t) const override
		{
			std::vector<math::RigidTransformd> out;
			const std::vector<math::RigidTransformd>& truth = scene().referenceFromCamera;
			for (std::size_t a = 0; a < truth.size(); ++a)
			{
				for (std::size_t b = 0; b < truth.size(); ++b)
				{
					if (a != b)
						out.push_back(truth[b].inverse() * truth[a]);
				}
			}
			return out;
		}
		std::vector<math::RigidTransformd> absolutePoses(const std::vector<feature::PointRay>&, double,
														 std::uint64_t) const override
		{
			std::vector<math::RigidTransformd> out;
			for (const math::RigidTransformd& t : scene().referenceFromCamera)
				out.push_back(t.inverse());
			return out;
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
