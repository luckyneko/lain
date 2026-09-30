// lain's projection agrees with OpenCV's for every distortion model OpenCV implements, so the
// coefficient orders distortion.h writes down are OpenCV's orders, checked against the reference
// implementation rather than against a second reading of the same formula. Inverse and modified
// Brown-Conrady have no OpenCV counterpart; their golden values are in lain::camera's own tests.

#include <lain/camera/projection.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <opencv2/calib3d.hpp>

#include <vector>

using namespace lain::camera;

namespace
{
	CameraModel modelWith(const Distortion& distortion)
	{
		CameraModelParameters p;
		p.image = {640, 480};
		p.intrinsics = {500.0, 510.0, 320.5, 239.5};
		p.distortion = distortion;
		ModelResult result = CameraModel::create(p);
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	const std::vector<cv::Point3d> kPoints = {{0.3, -0.2, 1.0}, {-0.25, 0.3, 1.2}, {0.05, 0.02, 0.8}, {-0.4, -0.3, 1.5}};
	const cv::Matx33d kCameraMatrix(500.0, 0, 320.5, 0, 510.0, 239.5, 0, 0, 1);

	void requireAgreement(const CameraModel& model, const std::vector<cv::Point2d>& opencv)
	{
		for (std::size_t i = 0; i < kPoints.size(); ++i)
		{
			const Projection<double> lain = project(model, kPoints[i].x, kPoints[i].y, kPoints[i].z);
			REQUIRE(lain.ok());
			CHECK(lain.u == Catch::Approx(opencv[i].x).margin(1e-6));
			CHECK(lain.v == Catch::Approx(opencv[i].y).margin(1e-6));
		}
	}
} // namespace

TEST_CASE("Brown-Conrady 5 projects as OpenCV's default model does", "[camera][opencv][projection]")
{
	const BrownConrady5 d{0.1, -0.05, 0.001, -0.002, 0.01};
	std::vector<cv::Point2d> pixels;
	cv::projectPoints(kPoints, cv::Vec3d(0, 0, 0), cv::Vec3d(0, 0, 0), kCameraMatrix,
					  std::vector<double>{d.k1, d.k2, d.p1, d.p2, d.k3}, pixels);
	requireAgreement(modelWith(d), pixels);
}

TEST_CASE("rational Brown-Conrady 8 projects as OpenCV's rational model does", "[camera][opencv][projection]")
{
	const RationalBrownConrady8 d{0.1, -0.05, 0.001, -0.002, 0.01, 0.02, -0.01, 0.005};
	std::vector<cv::Point2d> pixels;
	cv::projectPoints(kPoints, cv::Vec3d(0, 0, 0), cv::Vec3d(0, 0, 0), kCameraMatrix,
					  std::vector<double>{d.k1, d.k2, d.p1, d.p2, d.k3, d.k4, d.k5, d.k6}, pixels);
	requireAgreement(modelWith(d), pixels);
}

TEST_CASE("Kannala-Brandt 4 projects as OpenCV's fisheye model does", "[camera][opencv][projection]")
{
	const KannalaBrandt4 d{0.05, -0.01, 0.002, -0.0005};
	std::vector<cv::Point2d> pixels;
	cv::fisheye::projectPoints(kPoints, pixels, cv::Vec3d(0, 0, 0), cv::Vec3d(0, 0, 0), kCameraMatrix,
							   cv::Vec4d(d.k1, d.k2, d.k3, d.k4));
	requireAgreement(modelWith(d), pixels);
}
