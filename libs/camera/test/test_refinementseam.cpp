// The refine() facade over two stand-in refiners: which backend a problem reaches, by the models it
// frees, as board calibration picks its estimator. "a-pinhole" can estimate only a model without
// distortion; "b-polynomial" can estimate every model but Kannala-Brandt 4. Keys are ordered, so
// a-pinhole is the first. The Ceres refiner has its own tests, in plugins/camera/ceres.

#include "testcamera.h"

#include <lain/camera/refinement/refiner.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace lain::camera;
using namespace lain::camera::testing;

namespace
{
	// Which stand-in a problem reached, and how many times any was asked.
	struct Reached
	{
		std::vector<std::string> refiners;
	};
	Reached& reached()
	{
		static Reached instance;
		return instance;
	}

	template <const char* Name>
	class StandInRefiner : public refinement::Refiner
	{
	public:
		Provenance provenance() const override { return {Name, "1"}; }
		refinement::Solution refine(const refinement::Problem&) const override
		{
			reached().refiners.emplace_back(Name);
			refinement::Solution out;
			out.status = refinement::Status::Converged;
			return out;
		}
	};

	constexpr char kPinhole[] = "a-pinhole";
	constexpr char kPolynomial[] = "b-polynomial";

	class PinholeRefiner : public StandInRefiner<kPinhole>
	{
	public:
		bool canEstimate(DistortionModel model) const override { return model == DistortionModel::None; }
	};

	class PolynomialRefiner : public StandInRefiner<kPolynomial>
	{
	public:
		bool canEstimate(DistortionModel model) const override { return model != DistortionModel::KannalaBrandt4; }
	};

	void registerStandIns()
	{
		static const bool once = []
		{
			refinement::refinerRegistry().registerType<PinholeRefiner>(kPinhole);
			refinement::refinerRegistry().registerType<PolynomialRefiner>(kPolynomial);
			return true;
		}();
		(void)once;
		reached().refiners.clear();
	}

	// A problem whose models are `models`, each freed by `freedom`; the facade reads nothing else.
	refinement::Problem problemOf(const std::vector<Distortion>& models, const std::vector<refinement::ModelFreedom>& freedom)
	{
		refinement::Problem problem;
		for (const Distortion& d : models)
			problem.models.push_back(cameraWith(d));
		problem.modelFreedom = freedom;
		return problem;
	}

	refinement::ModelFreedom focalLength()
	{
		refinement::ModelFreedom out;
		out.focalLength = true;
		return out;
	}
} // namespace

TEST_CASE("a problem that frees no model reaches the first refiner", "[camera][refinement]")
{
	registerStandIns();
	SECTION("no freedoms at all")
	{
		const refinement::Solution s = refinement::refine(problemOf({KannalaBrandt4{}}, {}));
		CHECK(s.status == refinement::Status::Converged);
		CHECK(s.provenance.backend == kPinhole);
	}
	SECTION("a freedom that frees nothing")
	{
		const refinement::Solution s = refinement::refine(problemOf({KannalaBrandt4{}}, {refinement::ModelFreedom{}}));
		CHECK(s.provenance.backend == kPinhole);
	}
	CHECK(reached().refiners == std::vector<std::string>{kPinhole});

	// Freeing only the coefficients of a model that has none frees nothing.
	refinement::ModelFreedom coefficients;
	coefficients.coefficients = true;
	CHECK_FALSE(coefficients.frees(cameraWith(NoDistortion{})));
	CHECK(coefficients.frees(cameraWith(brownConrady())));
	CHECK(focalLength().frees(cameraWith(NoDistortion{})));
}

TEST_CASE("a freed model reaches the first refiner that can estimate it", "[camera][refinement]")
{
	registerStandIns();
	SECTION("no distortion: the first can")
	{
		CHECK(refinement::refine(problemOf({NoDistortion{}}, {focalLength()})).provenance.backend == kPinhole);
		CHECK(reached().refiners == std::vector<std::string>{kPinhole});
	}
	SECTION("Brown-Conrady 5: only the second can")
	{
		CHECK(refinement::refine(problemOf({brownConrady()}, {focalLength()})).provenance.backend == kPolynomial);
		CHECK(reached().refiners == std::vector<std::string>{kPolynomial});
	}
	SECTION("both freed: the refiner must estimate every one")
	{
		const refinement::Solution s =
			refinement::refine(problemOf({NoDistortion{}, brownConrady()}, {focalLength(), focalLength()}));
		CHECK(s.provenance.backend == kPolynomial);
	}
	SECTION("a held model the refiner cannot estimate does not matter")
	{
		const refinement::Solution s =
			refinement::refine(problemOf({NoDistortion{}, kannalaBrandt()}, {focalLength(), refinement::ModelFreedom{}}));
		CHECK(s.provenance.backend == kPinhole);
	}
}

TEST_CASE("a freed model no refiner can estimate is refused", "[camera][refinement]")
{
	registerStandIns();
	const refinement::Solution s = refinement::refine(problemOf({kannalaBrandt()}, {focalLength()}));
	CHECK(s.status == refinement::Status::Unsupported);
	CHECK_FALSE(s.usable());
	CHECK(s.detail == "a refiner is registered, but none can estimate Kannala-Brandt 4");
	CHECK(reached().refiners.empty());
}

TEST_CASE("canEstimate answers whether any refiner can", "[camera][refinement]")
{
	registerStandIns();
	CHECK(refinement::canEstimate(DistortionModel::None));
	CHECK(refinement::canEstimate(DistortionModel::BrownConrady5));
	CHECK(refinement::canEstimate(DistortionModel::RationalBrownConrady8));
	CHECK_FALSE(refinement::canEstimate(DistortionModel::KannalaBrandt4));
}
