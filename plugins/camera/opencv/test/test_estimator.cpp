// The OpenCV calibration estimator, through the production method module. Per model, from exact
// synthetic observations with seeded noise (so each model's recovery is measured against its own
// truth); then once end to end, from rendered, distorted, detected footage.

#include "syntheticview.h"

#include <lain/camera/board/rendering.h>
#include <lain/camera/calibration/board.h>
#include <lain/camera/calibration/estimator.h>
#include <lain/camera/opencv/register.h>
#include <lain/camera/projection.h>
#include <lain/media/framesequence.h>
#include <lain/media/framesource.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <random>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::calibration;
namespace method = lain::camera::calibration::board;
namespace cb = lain::camera::board;

namespace
{
	void ensureBackend()
	{
		static const bool once = []
		{
			opencv::registerBackend();
			return true;
		}();
		(void)once;
	}

	cb::Specification boardSpec()
	{
		cb::PatternParameters p;
		p.dictionary = cb::Dictionary::Aruco5x5_100;
		p.squaresX = 7;
		p.squaresY = 5;
		p.markerToSquare = 0.75;
		cb::Instance instance;
		instance.identity = "synthetic";
		instance.squareLength.value = core::Length::from<core::Length::Millimetres>(24.0);
		return *cb::Specification::create(*cb::Pattern::create(p).pattern, instance).specification;
	}

	CameraModel cameraWith(const Distortion& distortion, double focal = 900.0)
	{
		CameraModelParameters p;
		p.image = {960, 720};
		p.intrinsics = {focal, focal * 1.004, 481.3, 357.9};
		p.distortion = distortion;
		ModelResult result = CameraModel::create(p);
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	// Gaussian noise from a seeded engine through Box-Muller: std::normal_distribution is
	// implementation-defined, and these numbers must be the same on every platform.
	class Noise
	{
	public:
		explicit Noise(std::uint64_t seed)
			: m_engine(seed)
		{
		}
		double next(double sigma)
		{
			const double u1 = (double(m_engine() >> 11) + 0.5) / 9007199254740992.0;
			const double u2 = double(m_engine() >> 11) / 9007199254740992.0;
			return sigma * std::sqrt(-2 * std::log(u1)) * std::cos(6.283185307179586 * u2);
		}

	private:
		std::mt19937_64 m_engine;
	};

	// The detections a perfect detector would make of the board at 36 swept poses `distance` away,
	// with `sigma` pixels of noise on every corner.
	std::vector<cb::DetectionReport> synthetic(const CameraModel& camera, const cb::Specification& spec, double sigma,
											   double distance = 0.45)
	{
		Noise noise(7);
		std::vector<cb::DetectionReport> detections;
		for (std::size_t i = 0; i < 36; ++i)
		{
			const math::RigidTransformd pose = testing::sweepPose(spec, i, 36, distance);
			cb::Observation observation;
			observation.frame.source = core::Uri{"/synthetic/views"};
			observation.frame.ordinal = i;
			observation.image = camera.image();
			observation.pattern = spec.pattern().fingerprint();
			for (std::uint32_t id = 0; id < spec.pattern().cornerCount(); ++id)
			{
				const math::Vec3d p = pose.apply(*spec.cornerPosition(id));
				const Projection<double> pixel = project(camera, p.x, p.y, p.z);
				if (!pixel.ok() || !contains(camera, pixel.u, pixel.v))
					continue;
				observation.features.push_back({id, {pixel.u + noise.next(sigma), pixel.v + noise.next(sigma)}, std::nullopt});
			}
			cb::DetectionReport report;
			if (observation.features.size() >= 4)
			{
				report.status = cb::DetectionStatus::Partial;
				report.observation = observation;
			}
			detections.push_back(report);
		}
		return detections;
	}

	void requireRecovered(const Report& report, const CameraModel& truth)
	{
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		const Intrinsics& got = report.model->intrinsics();
		const Intrinsics& want = truth.intrinsics();
		INFO("fx " << got.fx << " (" << want.fx << "), fy " << got.fy << " (" << want.fy << "), cx " << got.cx << " ("
				   << want.cx << "), cy " << got.cy << " (" << want.cy << ")");
		CHECK(got.fx == Catch::Approx(want.fx).epsilon(0.003));
		CHECK(got.fy == Catch::Approx(want.fy).epsilon(0.003));
		CHECK(got.cx == Catch::Approx(want.cx).margin(2.0));
		CHECK(got.cy == Catch::Approx(want.cy).margin(2.0));
		CHECK(modelOf(report.model->distortion()) == modelOf(truth.distortion()));
	}
} // namespace

TEST_CASE("the plugin registers an estimator for the models OpenCV estimates", "[camera][opencv][calibration]")
{
	ensureBackend();
	CHECK(canEstimate(DistortionModel::None));
	CHECK(canEstimate(DistortionModel::BrownConrady5));
	CHECK(canEstimate(DistortionModel::RationalBrownConrady8));
	CHECK(canEstimate(DistortionModel::KannalaBrandt4));
	CHECK_FALSE(canEstimate(DistortionModel::InverseBrownConrady5));
	CHECK_FALSE(canEstimate(DistortionModel::ModifiedBrownConrady5));
}

TEST_CASE("each estimatable model is recovered from its own synthetic views", "[camera][opencv][calibration]")
{
	ensureBackend();
	const cb::Specification spec = boardSpec();
	struct Case
	{
		const char* name;
		CameraModel truth;
		double distance; // the wide Kannala-Brandt camera needs the board nearer to see it as large
		bool ready;		 // whether this scene should earn Ready (see below)
	};
	// The rational model is recovered as accurately as the others (its held-out rays are within the
	// noise), but with eight coefficients on one planar board its principal point moves about 2.2 mrad
	// across resamples, against Ready's 2.0: Exploratory, which is the stability criterion doing its
	// job. So Ready is asserted where the scene earns it, and "not Rejected" elsewhere.
	const Case cases[] = {
		{"no distortion", cameraWith(NoDistortion{}), 0.45, true},
		{"Brown-Conrady 5", cameraWith(BrownConrady5{-0.12, 0.05, 0.0008, -0.0005, 0.0}), 0.45, true},
		{"rational 8", cameraWith(RationalBrownConrady8{-0.1, 0.03, 0.0005, -0.0003, 0.0, 0.02, 0.0, 0.0}), 0.45, false},
		{"Kannala-Brandt 4", cameraWith(KannalaBrandt4{0.02, -0.01, 0.003, -0.001}, 600.0), 0.3, false},
	};
	for (const Case& c : cases)
	{
		INFO(c.name);
		Request request;
		request.model = modelOf(c.truth.distortion());
		const Report report = method::calibrate(synthetic(c.truth, spec, 0.1, c.distance), c.truth.image(), spec, request);
		requireRecovered(report, c.truth);
		CHECK(report.reproducibility.estimator.backend == "opencv");
		// OpenCV's parameter deviations come with calibrateCamera, one per coefficient the model has;
		// its fisheye calibration computes none, and absence stays absence rather than zeros.
		if (modelOf(c.truth.distortion()) == DistortionModel::KannalaBrandt4)
			CHECK_FALSE(report.uncertainty.has_value());
		else
		{
			REQUIRE(report.uncertainty.has_value());
			const std::size_t coefficients = std::visit([](const auto& d)
														{ return sizeof(d) / sizeof(double); },
														c.truth.distortion());
			CHECK(report.uncertainty->coefficients.size() == coefficients);
			CHECK(report.uncertainty->fx > 0);
			CHECK(report.uncertainty->fx < 0.01 * c.truth.intrinsics().fx);
		}
		// Held-out rays within the noise: 0.1 px of noise is about 0.14 px of error at f = 900.
		const auto* heldOut = std::get_if<HeldOutEvidence>(&report.heldOut);
		REQUIRE(heldOut != nullptr);
		INFO("held-out RMS " << heldOut->rmsPixels << " px, " << heldOut->rmsAngle << " rad");
		CHECK(heldOut->rmsPixels < 0.3);
		CHECK(heldOut->viewsWithoutPose == 0);
		INFO("notes: " << (report.fitnessNotes.empty() ? std::string("none") : report.fitnessNotes.front()));
		if (c.ready)
			CHECK(report.verdict == Verdict::Ready);
		else
			CHECK(report.verdict != Verdict::Rejected);
	}
}

TEST_CASE("from exact views, each model's coefficients come back in their own slots", "[camera][opencv][calibration]")
{
	// The accuracy checks above cannot see a coefficient read out of the wrong slot or a model fitted
	// without its own terms: a swapped p1 / p2, or a rational model fitted as Brown-Conrady, still
	// reprojects within tolerance. From noiseless views the fit is exact, so each coefficient is.
	ensureBackend();
	const cb::Specification spec = boardSpec();
	const auto recovered = [&](const CameraModel& truth, double distance)
	{
		Request request;
		request.model = modelOf(truth.distortion());
		request.resamples = 0;
		const Report report = method::calibrate(synthetic(truth, spec, 0.0, distance), truth.image(), spec, request);
		REQUIRE(report.status == CalibrationStatus::Succeeded);
		return report.model->distortion();
	};

	SECTION("Brown-Conrady 5")
	{
		const BrownConrady5 truth{-0.12, 0.05, 0.002, -0.001, 0.0};
		const BrownConrady5 got = std::get<BrownConrady5>(recovered(cameraWith(truth), 0.45));
		CHECK(got.k1 == Catch::Approx(truth.k1).margin(1e-5));
		CHECK(got.k2 == Catch::Approx(truth.k2).margin(1e-4));
		CHECK(got.p1 == Catch::Approx(truth.p1).margin(1e-6));
		CHECK(got.p2 == Catch::Approx(truth.p2).margin(1e-6));
	}
	SECTION("rational 8")
	{
		// The rational model's radial coefficients are NOT identifiable, even from exact data:
		// multiplying numerator and denominator by a common (1 + a r^2) leaves every projection
		// unchanged and moves every coefficient. So what is pinned is what is identifiable: the
		// tangential terms, and that the recovered model PROJECTS like the truth across the image.
		// The truth is strongly rational (a denominator Brown-Conrady's polynomial cannot follow), so
		// a fit made without the rational terms misses by pixels, not by hundredths.
		const CameraModel truth = cameraWith(RationalBrownConrady8{1.0, 0.0, 0.002, -0.001, 0.0, 1.3, 0.0, 0.0});
		const Distortion fitted = recovered(truth, 0.45);
		const RationalBrownConrady8& got = std::get<RationalBrownConrady8>(fitted);
		CHECK(got.p1 == Catch::Approx(0.002).margin(1e-5));
		CHECK(got.p2 == Catch::Approx(-0.001).margin(1e-5));
		CameraModelParameters p = truth.parameters();
		p.distortion = fitted;
		const CameraModel model = *CameraModel::create(p).model;
		double worst = 0;
		for (double u = 0; u < 960; u += 120)
		{
			for (double v = 0; v < 720; v += 120)
			{
				const Unprojection<double> ray = unproject(truth, u, v);
				const Projection<double> back = project(model, ray.x, ray.y, ray.z);
				REQUIRE(back.ok());
				worst = std::max(worst, std::hypot(back.u - u, back.v - v));
			}
		}
		INFO("worst disagreement with the true projection: " << worst << " px");
		CHECK(worst < 0.05);
	}
	SECTION("Kannala-Brandt 4")
	{
		const KannalaBrandt4 truth{0.02, -0.01, 0.003, -0.001};
		const KannalaBrandt4 got = std::get<KannalaBrandt4>(recovered(cameraWith(truth, 600.0), 0.3));
		CHECK(got.k1 == Catch::Approx(truth.k1).margin(1e-4));
		CHECK(got.k2 == Catch::Approx(truth.k2).margin(1e-3));
	}
}

TEST_CASE("a model OpenCV does not estimate is refused, never substituted", "[camera][opencv][calibration]")
{
	ensureBackend();
	const cb::Specification spec = boardSpec();
	for (const DistortionModel model : {DistortionModel::InverseBrownConrady5, DistortionModel::ModifiedBrownConrady5})
	{
		Request request;
		request.model = model;
		const Report report =
			method::calibrate(synthetic(cameraWith(NoDistortion{}), spec, 0.0), ImageGeometry{960, 720}, spec, request);
		CHECK(report.status == CalibrationStatus::Failed);
		REQUIRE_FALSE(report.failures.empty());
		CHECK(report.failures[0].failure == Failure::NoEstimator);
	}
}

TEST_CASE("a held model OpenCV cannot estimate is still validated", "[camera][opencv][calibration]")
{
	// Board poses are solved on lain's own unprojection, so validation works for any model: here an
	// inverse Brown-Conrady camera, which OpenCV has no model for, validates against its own views.
	ensureBackend();
	const cb::Specification spec = boardSpec();
	const CameraModel truth = cameraWith(InverseBrownConrady5{0.08, -0.02, 0.0005, -0.0003, 0.0});
	Request request;
	request.importedPolicy = ImportedModelPolicy::HoldAndValidate;
	request.imported = truth;
	const Report report = method::calibrate(synthetic(truth, spec, 0.0), truth.image(), spec, request);
	REQUIRE(report.status == CalibrationStatus::Succeeded);
	const auto* heldOut = std::get_if<HeldOutEvidence>(&report.heldOut);
	REQUIRE(heldOut != nullptr);
	CHECK(heldOut->viewsWithoutPose == 0);
	CHECK(heldOut->rmsPixels < 1e-6);
}

TEST_CASE("an imported model seeds the OpenCV estimate and it still converges", "[camera][opencv][calibration]")
{
	ensureBackend();
	const cb::Specification spec = boardSpec();
	const CameraModel truth = cameraWith(BrownConrady5{-0.12, 0.05, 0.0008, -0.0005, 0.0});
	CameraModelParameters guess = truth.parameters();
	guess.intrinsics.fx *= 1.05; // a manufacturer's figure a few percent out
	guess.intrinsics.fy *= 1.05;
	Request request;
	request.importedPolicy = ImportedModelPolicy::Initial;
	request.imported = *CameraModel::create(guess).model;
	const Report report = method::calibrate(synthetic(truth, spec, 0.1), truth.image(), spec, request);
	requireRecovered(report, truth);
	CHECK(report.seededFields.size() == 9);
}

namespace
{
	// Footage of a distorted camera looking at a rendered board, frame by frame on demand.
	class DistortedFootage : public media::FrameSource
	{
	public:
		DistortedFootage(cb::Rendering rendering, cb::Specification spec, CameraModel camera, std::size_t frames)
			: FrameSource{core::Uri{"/synthetic/distorted"}, specOf(camera), frames}
			, m_rendering(std::move(rendering))
			, m_spec(std::move(spec))
			, m_camera(std::move(camera))
		{
		}

		static media::FrameSpec specOf(const CameraModel& camera)
		{
			media::FrameSpec s;
			s.extent = {int(camera.image().width), int(camera.image().height)};
			s.pixelFormat = image::PixelFormat::Gray8;
			s.colorSpace = image::ColorSpace::sRGB;
			return s;
		}

	protected:
		image::Image decodeFrame(std::size_t ordinal) const override
		{
			return testing::distortedView(m_rendering, m_spec, m_camera, testing::sweepPose(m_spec, ordinal, frameCount(), 0.42));
		}

	private:
		cb::Rendering m_rendering;
		cb::Specification m_spec;
		CameraModel m_camera;
	};
} // namespace

TEST_CASE("footage of a distorted camera calibrates to that camera, end to end", "[camera][opencv][calibration]")
{
	// Render, distort, detect, select, estimate, validate, resample, judge: every stage production.
	ensureBackend();
	const cb::Specification spec = boardSpec();
	const std::optional<cb::Rendering> rendering = cb::render(spec.pattern(), cb::RenderRequest{60, 20}).rendering;
	REQUIRE(rendering.has_value());
	CameraModelParameters p;
	p.image = {640, 480};
	p.intrinsics = {600.0, 602.0, 321.4, 238.6};
	p.distortion = BrownConrady5{-0.15, 0.06, 0.0006, -0.0004, 0.0};
	const CameraModel truth = *CameraModel::create(p).model;

	const media::FrameSequence footage =
		media::FrameSequence::over(std::make_shared<DistortedFootage>(*rendering, spec, truth, 30));
	Request request;
	request.resamples = 8;
	const Report report = method::calibrate(footage, spec, request);

	CHECK(report.diagnostics.framesUsable >= 25);
	requireRecovered(report, truth);
	const auto* heldOut = std::get_if<HeldOutEvidence>(&report.heldOut);
	REQUIRE(heldOut != nullptr);
	INFO("held-out RMS " << heldOut->rmsPixels << " px; fit RMS " << report.diagnostics.fitRmsPixels << " px");
	CHECK(heldOut->rmsPixels < 0.25);
	INFO("notes: " << (report.fitnessNotes.empty() ? std::string("none") : report.fitnessNotes.front()));
	CHECK(report.verdict == Verdict::Ready);
	CHECK(report.reproducibility.detector.backend == "opencv");
}
