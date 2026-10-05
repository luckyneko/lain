#pragma once

// A synthetic board calibration scene for the method module's tests: a known camera, a set of board
// poses, and stand-in detector and estimator backends that answer from that truth. The detector
// ignores the pixels and reports lain's exact projection of the board for the frame it is handed;
// the estimator hands back whatever camera the test tells it to. So these tests exercise view
// selection, validation, resampling, the verdict and the failure paths without any real detection
// or estimation, which the OpenCV plugin's tests cover.

#include "testboard.h"

#include <lain/camera/board/detection.h>
#include <lain/camera/board/pose.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/camera/projection.h>
#include <lain/math/rigidtransform.h>
#include <lain/media/framesequence.h>
#include <lain/media/framesource.h>

#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace lain::camera::testing
{
	inline CameraModelParameters trueCamera()
	{
		CameraModelParameters p;
		p.image = {960, 720};
		p.intrinsics = {900.0, 905.0, 479.5, 359.5};
		p.distortion = BrownConrady5{-0.1, 0.05, 0.001, -0.0005, 0.0};
		return p;
	}

	// The board, 7x5 with 24 mm squares, at pose `i` of `count`: a sweep that moves it across the
	// image and tilts it every which way, so views differ in coverage and in tilt.
	inline math::RigidTransformd boardPose(std::size_t i, std::size_t count)
	{
		const double t = double(i) / double(count);
		const double yaw = 0.5 * std::sin(6.28318 * t);
		const double pitch = 0.4 * std::cos(6.28318 * 2 * t);
		const math::Quatd turn = math::angleAxis(yaw, math::Vec3d{0, 1, 0}) * math::angleAxis(pitch, math::Vec3d{1, 0, 0});
		const math::Vec3d centre{0.084, 0.06, 0.0};
		const math::Vec3d offset{0.17 * std::cos(6.28318 * 3 * t), 0.12 * std::sin(6.28318 * 3 * t), 0.45};
		return math::RigidTransformd{turn, offset - turn * centre};
	}

	// Blank frames of the true camera's geometry; the stand-in detector never looks at them.
	class BlankSource : public media::FrameSource
	{
	public:
		BlankSource(std::size_t frames)
			: FrameSource{core::Uri{"/synthetic/calibration"}, spec(), frames}
		{
		}

		static media::FrameSpec spec()
		{
			media::FrameSpec s;
			s.extent = {960, 720};
			s.pixelFormat = image::PixelFormat::Gray8;
			s.colorSpace = image::ColorSpace::sRGB;
			return s;
		}

	protected:
		image::Image decodeFrame(std::size_t) const override
		{
			return image::Image{960, 720, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		}
	};

	inline media::FrameSequence footage(std::size_t frames)
	{
		return media::FrameSequence::over(std::make_shared<BlankSource>(frames));
	}

	// What the scene's frames are: how many, and which of them show no board.
	struct Scene
	{
		std::size_t frames = 40;
		std::vector<std::size_t> boardless; // ordinals the detector reports nothing for

		std::mutex mutex;
		std::set<std::thread::id> threads; // every thread the detector ran on
	};
	inline Scene& scene()
	{
		static Scene instance;
		return instance;
	}

	// Reports lain's exact projection of every corner of the board at the frame's pose.
	class TruthDetector : public board::Detector
	{
	public:
		board::DetectionReport detect(const image::Image&, const media::FrameRef& frame, const board::Specification& spec,
									  const board::DetectionRequest&, double) const override
		{
			{
				std::lock_guard<std::mutex> lock(scene().mutex);
				scene().threads.insert(std::this_thread::get_id());
			}
			board::DetectionReport report;
			report.provenance = {"truth", "1"};
			for (const std::size_t skip : scene().boardless)
			{
				if (skip == frame.ordinal)
				{
					report.rejections.push_back({board::Rejection::NoMarkers, "a boardless frame"});
					return report;
				}
			}
			const CameraModel camera = *CameraModel::create(trueCamera()).model;
			const math::RigidTransformd pose = boardPose(frame.ordinal, scene().frames);
			board::Observation observation;
			observation.frame = frame;
			observation.image = trueCamera().image;
			observation.pattern = spec.pattern().fingerprint();
			for (std::uint32_t id = 0; id < spec.pattern().cornerCount(); ++id)
			{
				const math::Vec3d p = pose.apply(*spec.cornerPosition(id));
				const Projection<double> pixel = project(camera, p.x, p.y, p.z);
				if (pixel.ok() && contains(camera, pixel.u, pixel.v))
					observation.features.push_back({id, {pixel.u, pixel.v}, std::nullopt});
			}
			report.observation = observation;
			report.status = board::DetectionStatus::Detected;
			return report;
		}
	};

	// What the stand-in estimator does, and what it was asked.
	struct EstimatorScript
	{
		std::vector<DistortionModel> estimatable{DistortionModel::BrownConrady5};
		// The camera it reports for a set of views; by default the truth.
		std::function<std::optional<CameraModelParameters>(const std::vector<board::Observation>&)> answer =
			[](const std::vector<board::Observation>&)
		{ return std::optional<CameraModelParameters>{trueCamera()}; };
		std::string failure = "the stand-in was told to fail";

		std::mutex mutex;
		std::optional<CameraModelParameters> lastInitial;
		std::size_t estimates = 0;
	};
	inline EstimatorScript& script()
	{
		static EstimatorScript instance;
		return instance;
	}

	class ScriptedEstimator : public calibration::Estimator
	{
	public:
		Provenance provenance() const override { return {"scripted", "1"}; }

		bool canEstimate(DistortionModel model) const override
		{
			for (const DistortionModel m : script().estimatable)
			{
				if (m == model)
					return true;
			}
			return false;
		}

		calibration::Estimate estimate(const ImageGeometry&, const std::vector<board::Observation>& views,
									   const board::Specification&, DistortionModel,
									   const std::optional<CameraModelParameters>& initial) const override
		{
			calibration::Estimate out;
			out.parameters = script().answer(views);
			if (!out.parameters)
				out.failure = script().failure;
			std::lock_guard<std::mutex> lock(script().mutex);
			script().lastInitial = initial;
			++script().estimates;
			return out;
		}
	};

	// The true pose of the view's frame: validation then measures only the model.
	class TruthPoseSolver : public board::PoseSolver
	{
	public:
		Provenance provenance() const override { return {"truth", "1"}; }

		std::vector<math::RigidTransformd> solve(const CameraModel&, const board::Specification&,
												 const board::Observation& view) const override
		{
			return {testing::boardPose(view.frame.ordinal, scene().frames)};
		}
	};

	inline void registerStandIns()
	{
		static const bool once = []
		{
			board::detectorRegistry().registerType<TruthDetector>("truth");
			calibration::estimatorRegistry().registerType<ScriptedEstimator>("scripted");
			board::poseSolverRegistry().registerType<TruthPoseSolver>("truth");
			return true;
		}();
		(void)once;
	}

	// Every case starts from the default scene and script.
	inline void resetScene()
	{
		registerStandIns();
		Scene& s0 = scene();
		s0.frames = 40;
		s0.boardless.clear();
		s0.threads.clear();
		EstimatorScript& s = script();
		s.estimatable = {DistortionModel::BrownConrady5};
		s.answer = [](const std::vector<board::Observation>&)
		{ return std::optional<CameraModelParameters>{trueCamera()}; };
		s.lastInitial.reset();
		s.estimates = 0;
	}
} // namespace lain::camera::testing
