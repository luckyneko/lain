#include "opencvestimator.h"

#include <lain/camera/projection.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/core/utility.hpp>

#include <string>
#include <vector>

namespace lain::camera::opencv
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Each view's board-frame corner positions and measured pixels, paired by id.
	static void correspondences(const std::vector<board::Observation>& views, const board::Specification& board,
								std::vector<std::vector<cv::Point3f>>& objects, std::vector<std::vector<cv::Point2f>>& pixels)
	{
		for (const board::Observation& view : views)
		{
			std::vector<cv::Point3f> object;
			std::vector<cv::Point2f> pixel;
			for (const board::FeatureObservation& f : view.features)
			{
				const std::optional<math::Vec3d> p = board.cornerPosition(f.id);
				if (!p)
					continue;
				object.emplace_back(float(p->x), float(p->y), float(p->z));
				pixel.emplace_back(float(f.pixel.x), float(f.pixel.y));
			}
			objects.push_back(std::move(object));
			pixels.push_back(std::move(pixel));
		}
	}

	// OpenCV's coefficient vector for lain's forward models, in OpenCV's order (which distortion.h
	// already writes down, and the projection agreement test checks).
	static std::vector<double> coefficientsOf(const Distortion& distortion)
	{
		if (const auto* d = std::get_if<BrownConrady5>(&distortion))
			return {d->k1, d->k2, d->p1, d->p2, d->k3};
		if (const auto* d = std::get_if<RationalBrownConrady8>(&distortion))
			return {d->k1, d->k2, d->p1, d->p2, d->k3, d->k4, d->k5, d->k6};
		if (const auto* d = std::get_if<KannalaBrandt4>(&distortion))
			return {d->k1, d->k2, d->k3, d->k4};
		return {0, 0, 0, 0, 0};
	}

	static double at(const cv::Mat& m, int i)
	{
		return i < int(m.total()) ? m.at<double>(i) : 0.0;
	}

	// --- OpenCVEstimator -------------------------------------------------------------

	Provenance OpenCVEstimator::provenance() const
	{
		return {"opencv", cv::getVersionString()};
	}

	bool OpenCVEstimator::canEstimate(DistortionModel model) const
	{
		return model == DistortionModel::None || model == DistortionModel::BrownConrady5 ||
			   model == DistortionModel::RationalBrownConrady8 || model == DistortionModel::KannalaBrandt4;
	}

	calibration::Estimate OpenCVEstimator::estimate(const ImageGeometry& image, const std::vector<board::Observation>& views,
													const board::Specification& board, DistortionModel model,
													const std::optional<CameraModelParameters>& initial) const
	{
		calibration::Estimate out;
		if (!canEstimate(model))
		{
			out.failure = "OpenCV does not estimate " + std::string(displayName(model));
			return out;
		}

		std::vector<std::vector<cv::Point3f>> objects;
		std::vector<std::vector<cv::Point2f>> pixels;
		correspondences(views, board, objects, pixels);
		const cv::Size size(int(image.width), int(image.height));

		cv::Matx33d k = cv::Matx33d::eye();
		std::vector<double> coefficients;
		if (initial)
		{
			// The Initial policy: start from the imported parameters and refine them.
			k = cv::Matx33d(initial->intrinsics.fx, 0, initial->intrinsics.cx, 0, initial->intrinsics.fy,
							initial->intrinsics.cy, 0, 0, 1);
			coefficients = coefficientsOf(initial->distortion);
		}

		try
		{
			CameraModelParameters parameters;
			parameters.image = image;
			const cv::TermCriteria criteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, 1e-12);
			if (model == DistortionModel::KannalaBrandt4)
			{
				cv::Vec4d d(0, 0, 0, 0);
				if (initial && coefficients.size() == 4)
					d = cv::Vec4d(coefficients[0], coefficients[1], coefficients[2], coefficients[3]);
				int flags = cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC | cv::fisheye::CALIB_FIX_SKEW;
				if (initial)
					flags |= cv::fisheye::CALIB_USE_INTRINSIC_GUESS;
				std::vector<cv::Vec3d> rotations, translations;
				out.rmsPixels = cv::fisheye::calibrate(objects, pixels, size, k, d, rotations, translations, flags, criteria);
				parameters.distortion = KannalaBrandt4{d[0], d[1], d[2], d[3]};
			}
			else
			{
				int flags = 0;
				if (model == DistortionModel::RationalBrownConrady8)
					flags |= cv::CALIB_RATIONAL_MODEL;
				if (model == DistortionModel::None)
					flags |= cv::CALIB_FIX_K1 | cv::CALIB_FIX_K2 | cv::CALIB_FIX_K3 | cv::CALIB_ZERO_TANGENT_DIST;
				if (initial)
					flags |= cv::CALIB_USE_INTRINSIC_GUESS;
				cv::Mat distortion(1, model == DistortionModel::RationalBrownConrady8 ? 8 : 5, CV_64F, cv::Scalar(0));
				for (std::size_t i = 0; i < coefficients.size() && int(i) < distortion.cols; ++i)
					distortion.at<double>(int(i)) = coefficients[i];
				cv::Mat kMat(k);
				std::vector<cv::Mat> rotations, translations;
				cv::Mat intrinsicDeviations, extrinsicDeviations, perViewErrors;
				out.rmsPixels = cv::calibrateCamera(objects, pixels, size, kMat, distortion, rotations, translations,
													intrinsicDeviations, extrinsicDeviations, perViewErrors, flags, criteria);
				k = cv::Matx33d(kMat);
				if (model == DistortionModel::None)
					parameters.distortion = NoDistortion{};
				else if (model == DistortionModel::BrownConrady5)
					parameters.distortion = BrownConrady5{at(distortion, 0), at(distortion, 1), at(distortion, 2),
														  at(distortion, 3), at(distortion, 4)};
				else
					parameters.distortion = RationalBrownConrady8{at(distortion, 0), at(distortion, 1), at(distortion, 2),
																  at(distortion, 3), at(distortion, 4), at(distortion, 5),
																  at(distortion, 6), at(distortion, 7)};

				// OpenCV's standard deviations: fx, fy, cx, cy, then k1 k2 p1 p2 k3 k4 k5 k6, then
				// thin-prism and tilt terms lain has no model for. Kept only for the coefficients the
				// MODEL has, counted from the model: OpenCV hands the rational model back as a
				// 14-element vector, and counting that reported deviations for terms it does not have.
				calibration::ParameterUncertainty uncertainty;
				uncertainty.fx = at(intrinsicDeviations, 0);
				uncertainty.fy = at(intrinsicDeviations, 1);
				uncertainty.cx = at(intrinsicDeviations, 2);
				uncertainty.cy = at(intrinsicDeviations, 3);
				const int count = int(coefficientNames(model).size());
				for (int i = 0; i < count; ++i)
					uncertainty.coefficients.push_back(at(intrinsicDeviations, 4 + i));
				out.uncertainty = uncertainty;
			}
			parameters.intrinsics = {k(0, 0), k(1, 1), k(0, 2), k(1, 2)};
			out.parameters = parameters;
		}
		catch (const cv::Exception& e)
		{
			out.failure = std::string("OpenCV could not calibrate: ") + e.what();
		}
		return out;
	}

	std::optional<math::RigidTransformd> OpenCVEstimator::boardPose(const CameraModel& model,
																	const board::Specification& board,
																	const board::Observation& view) const
	{
		// Pinhole normalised coordinates of each corner's ray, through lain's own model.
		std::vector<cv::Point3d> objects;
		std::vector<cv::Point2d> normalised;
		for (const board::FeatureObservation& f : view.features)
		{
			const std::optional<math::Vec3d> p = board.cornerPosition(f.id);
			const Unprojection<double> ray = unproject(model, f.pixel.x, f.pixel.y);
			if (!p || !ray.ok())
				continue;
			objects.emplace_back(p->x, p->y, p->z);
			normalised.emplace_back(ray.x / ray.z, ray.y / ray.z);
		}
		if (objects.size() < 4)
			return std::nullopt;

		try
		{
			// IPPE is exact for a planar target; a Levenberg-Marquardt pass then refines it.
			cv::Vec3d rotation, translation;
			const cv::Matx33d identity = cv::Matx33d::eye();
			if (!cv::solvePnP(objects, normalised, identity, cv::noArray(), rotation, translation, false, cv::SOLVEPNP_IPPE))
				return std::nullopt;
			cv::solvePnPRefineLM(objects, normalised, identity, cv::noArray(), rotation, translation);

			cv::Matx33d r;
			cv::Rodrigues(rotation, r);
			math::Mat3d m; // GLM is column-major: m[column][row]
			for (int row = 0; row < 3; ++row)
			{
				for (int col = 0; col < 3; ++col)
					m[col][row] = r(row, col);
			}
			return math::RigidTransformd{math::quat_cast(m), math::Vec3d{translation[0], translation[1], translation[2]}};
		}
		catch (const cv::Exception&)
		{
			return std::nullopt;
		}
	}
} // namespace lain::camera::opencv
