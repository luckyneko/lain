#pragma once

// What the camera method goldens share (golden/*.txt): how a number, an enum, a provenance, a
// detection request and a detection report are printed, the registration report's transforms,
// thresholds and graph edges, and how what a test produced is compared with its committed golden. One
// spelling, so a field printed by two goldens means the same thing in both.
//
// Numbers are snapped to a 1e-9 grid, then printed to nine significant digits. That hides the last
// bits in which two platforms' maths libraries differ, while any change of computation moves them
// visibly.
//
// A test that includes this is compiled with LAIN_CAMERA_GOLDEN_DIR (where the goldens are
// committed) and LAIN_CAMERA_GOLDEN_OUT (where a differing result is written).

#include <lain/camera/board/detection.h>
#include <lain/camera/provenance.h>
#include <lain/camera/registration/fitness.h>
#include <lain/camera/registration/report.h>
#include <lain/math/rigidtransform.h>
#include <lain/meta/enums.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

namespace lain::camera::testing::golden
{
	inline std::string number(double x)
	{
		if (std::isnan(x))
			return "nan";
		if (std::isinf(x))
			return x > 0 ? "inf" : "-inf";
		double snapped = std::abs(x) < 1e6 ? std::round(x * 1e9) / 1e9 : x;
		if (snapped == 0)
			snapped = 0; // never "-0"
		std::ostringstream text;
		text.imbue(std::locale::classic());
		text << std::setprecision(9) << snapped;
		return text.str();
	}

	template <typename E>
	std::string name(E value)
	{
		return std::string(meta::enums::name(value));
	}

	// The camera a registration's resampling names as worst, or "-" when every camera's rotation
	// variation prints as 0: the choice is then made by rounding noise, which differs between
	// platforms (cam01 on macOS arm64 where linux-x86_64 gave cam02), so it is no part of what a
	// golden pins.
	inline std::string worstCamera(double rotationVariation, const std::string& camera)
	{
		return number(rotationVariation) == "0" ? "-" : camera;
	}

	inline std::string provenance(const Provenance& p)
	{
		return p.backend + "/" + p.version;
	}

	inline std::string scale(const board::ScalePolicy& policy)
	{
		if (const auto* f = std::get_if<board::ScaleFactor>(&policy))
			return "factor " + number(f->factor);
		if (const auto* l = std::get_if<board::LongestSide>(&policy))
			return "longest " + std::to_string(l->pixels);
		return "native";
	}

	inline std::string detectionRequest(const board::DetectionRequest& r)
	{
		return scale(r.scale) + ", refine native " + std::to_string(r.refineAtNativeResolution) + ", detail " +
			   name(r.detail) + ", minimum corners " + std::to_string(r.minimumCorners);
	}

	template <typename T>
	std::string optional(const std::optional<T>& value)
	{
		if (!value)
			return "unset";
		if constexpr (std::is_floating_point_v<T>)
			return number(*value);
		else
			return std::to_string(*value);
	}

	// One detection, compactly: its outcome, and its corners as sums, which change if any corner does.
	inline std::string detection(const board::DetectionReport& d)
	{
		std::string text = name(d.status) + " " + name(d.refinement) + " by " + provenance(d.provenance);
		text += " stats(" + std::to_string(d.stats.markersFound) + " " + std::to_string(d.stats.markersRejected) + " " +
				std::to_string(d.stats.cornersFound) + " " + std::to_string(d.stats.cornersExpected) + ")";
		text += " transform(" + number(d.transform.scaleX) + " " + number(d.transform.scaleY) + ")";
		text += " request(" + detectionRequest(d.request) + ")";
		for (const board::RejectionReason& r : d.rejections)
			text += " rejected(" + name(r.reason) + ": " + r.detail + ")";
		text += " evidence " + std::to_string(d.evidence.has_value());
		if (d.observation)
		{
			const board::Observation& o = *d.observation;
			double ids = 0, u = 0, v = 0, cov = 0;
			for (const board::FeatureObservation& f : o.features)
			{
				ids += f.id;
				u += f.pixel.x;
				v += f.pixel.y;
				if (f.covariance)
					cov += (*f.covariance)[0] + (*f.covariance)[1] + (*f.covariance)[2];
			}
			text += " observation(" + o.frame.toString() + ", " + std::to_string(o.image.width) + "x" +
					std::to_string(o.image.height) + ", pattern " + o.pattern.toString().substr(0, 12) + ", " +
					std::to_string(o.features.size()) + " corners, ids " + number(ids) + ", u " + number(u) + ", v " +
					number(v) + ", covariance " + number(cov) + ")";
		}
		return text;
	}

	inline std::string transform(const math::RigidTransformd& t)
	{
		const math::Quatd q = t.rotation();
		const math::Vec3d p = t.translation();
		return "q(" + number(q.w) + " " + number(q.x) + " " + number(q.y) + " " + number(q.z) + ") t(" + number(p.x) + " " +
			   number(p.y) + " " + number(p.z) + ")";
	}

	inline std::string thresholds(const registration::FitnessThresholds& t)
	{
		return std::to_string(t.minimumSharedPerCamera) + " " + std::to_string(t.minimumBridgeShared) + " " +
			   number(t.maximumHeldOutAngle) + " " + number(t.maximumRotationVariation) + " " +
			   number(t.maximumTranslationVariation) + " " + number(t.maximumOutlierFraction) + " " +
			   std::to_string(t.minimumHeldOutPerCamera);
	}

	inline std::string heldOutCamera(const registration::HeldOutCamera& c)
	{
		return c.camera.value + " predictions " + std::to_string(c.predictions) + " residuals " +
			   std::to_string(c.residuals) + " robustAngle " + number(c.robustAngle) + " rmsAngle " + number(c.rmsAngle);
	}

	inline std::string edge(const registration::GraphEdgeReport& e)
	{
		return e.a.value + "-" + e.b.value + " shared " + std::to_string(e.shared) + " bridge " + std::to_string(e.bridge);
	}

	inline std::string firstDifference(const std::string& expected, const std::string& actual)
	{
		std::istringstream a(expected), b(actual);
		std::string la, lb;
		for (std::size_t line = 1;; ++line)
		{
			const bool ha = static_cast<bool>(std::getline(a, la));
			const bool hb = static_cast<bool>(std::getline(b, lb));
			if (!ha && !hb)
				return "none";
			if (!ha || !hb || la != lb)
				return "line " + std::to_string(line) + ":\n  golden:   " + (ha ? la : "<end>") + "\n  produced: " + (hb ? lb : "<end>");
		}
	}

	// `produced` must be exactly the committed golden `file`. When it differs, what was produced is
	// written beside the build and the test fails saying where: to accept a deliberate change, copy
	// that file over the golden and commit it with the change.
	inline void requireGolden(const std::string& file, const std::string& produced)
	{
		const std::string golden = std::string(LAIN_CAMERA_GOLDEN_DIR) + "/" + file;
		std::ifstream in(golden, std::ios::binary);
		std::ostringstream expected;
		expected << in.rdbuf();
		if (expected.str() == produced)
			return;

		const std::string written = std::string(LAIN_CAMERA_GOLDEN_OUT) + "/" + file;
		std::ofstream(written, std::ios::binary) << produced;
		FAIL("the report differs from " << golden << " at " << firstDifference(expected.str(), produced)
										<< "\nwhat was produced is in " << written
										<< "; copy it over the golden only for a deliberate change");
	}
} // namespace lain::camera::testing::golden
