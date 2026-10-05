#include "opencvposesolver.h"

#include <lain/camera/projection.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/core/utility.hpp>

namespace lain::camera::opencv
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	static math::RigidTransformd transformOf(const cv::Mat& rotation, const cv::Mat& translation)
	{
		cv::Matx33d r;
		cv::Rodrigues(rotation, r);
		math::Mat3d m; // GLM is column-major: m[column][row]
		for (int row = 0; row < 3; ++row)
		{
			for (int col = 0; col < 3; ++col)
				m[col][row] = r(row, col);
		}
		return math::RigidTransformd{math::quat_cast(m),
									 math::Vec3d{translation.at<double>(0), translation.at<double>(1), translation.at<double>(2)}};
	}

	// --- OpenCVPoseSolver ------------------------------------------------------------

	Provenance OpenCVPoseSolver::provenance() const
	{
		return {"opencv", cv::getVersionString()};
	}

	std::vector<math::RigidTransformd> OpenCVPoseSolver::solve(const CameraModel& model, const board::Specification& board,
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
			return {};

		std::vector<math::RigidTransformd> out;
		try
		{
			// IPPE is exact for a planar target and returns both of its poses, best first; a
			// Levenberg-Marquardt pass then refines each.
			const cv::Matx33d identity = cv::Matx33d::eye();
			std::vector<cv::Mat> rotations, translations;
			cv::solvePnPGeneric(objects, normalised, identity, cv::noArray(), rotations, translations, false,
								cv::SOLVEPNP_IPPE);
			for (std::size_t i = 0; i < rotations.size() && i < translations.size(); ++i)
			{
				cv::Mat rotation = rotations[i].clone(), translation = translations[i].clone();
				cv::solvePnPRefineLM(objects, normalised, identity, cv::noArray(), rotation, translation);
				out.push_back(transformOf(rotation, translation));
			}
		}
		catch (const cv::Exception&)
		{
			return {};
		}
		return out;
	}
} // namespace lain::camera::opencv
