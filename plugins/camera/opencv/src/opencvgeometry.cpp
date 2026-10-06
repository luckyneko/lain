#include "opencvgeometry.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/core/utility.hpp>

#include <cmath>
#include <optional>

namespace lain::camera::opencv
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// The most a ray may lean from the optical axis and still reach OpenCV: past 80 degrees its
	// normalised coordinates run off towards infinity. lain's own measurement still sees it; only
	// what OpenCV proposes from is narrowed.
	static const double kLeastAxial = std::cos(80.0 * 3.141592653589793 / 180.0);

	// OpenCV is handed rays as pixels of a virtual pinhole of this focal length, and the inlier angle
	// as pixels of it too, not as normalised coordinates with an identity camera. Measured on two
	// rendered views with 2% wrong matches: on normalised coordinates USAC's MAGSAC++ proposed
	// essential matrices 0.6 to 15 mrad out across 20 seeds, while the same rays at this scale gave
	// 0.25 to 0.47 mrad on every seed. Its internals are tuned for pixel-scale coordinates.
	static constexpr double kVirtualFocal = 1000.0;

	// A unit ray as a pixel of the virtual pinhole, or nothing when it leans too far.
	static std::optional<cv::Point2d> virtualPixel(const math::Vec3d& ray)
	{
		if (!(ray.z > kLeastAxial * math::length(ray)))
			return std::nullopt;
		return cv::Point2d{kVirtualFocal * ray.x / ray.z, kVirtualFocal * ray.y / ray.z};
	}

	static cv::Mat virtualCamera()
	{
		return cv::Mat(cv::Matx33d(kVirtualFocal, 0, 0, 0, kVirtualFocal, 0, 0, 0, 1));
	}

	static math::RigidTransformd transformOf(const cv::Matx33d& r, const cv::Vec3d& t)
	{
		math::Mat3d m; // GLM is column-major: m[column][row]
		for (int row = 0; row < 3; ++row)
		{
			for (int col = 0; col < 3; ++col)
				m[col][row] = r(row, col);
		}
		return math::RigidTransformd{math::quat_cast(m), math::Vec3d{t[0], t[1], t[2]}};
	}

	static math::RigidTransformd transformOfVectors(const cv::Mat& rotation, const cv::Mat& translation)
	{
		cv::Matx33d r;
		cv::Rodrigues(rotation, r);
		return transformOf(r, cv::Vec3d{translation.at<double>(0), translation.at<double>(1), translation.at<double>(2)});
	}

	// USAC as MAGSAC++, serial and seeded. The threshold is the inlier angle in pixels of the virtual
	// pinhole, which near the axis is the angle itself. The neighbourhood is the grid, never FLANN's,
	// whose index would draw from the process-global std::rand.
	static cv::UsacParams usac(double angle, std::uint64_t seed)
	{
		cv::UsacParams params;
		params.confidence = 0.999;
		params.isParallel = false;
		params.loMethod = cv::LOCAL_OPTIM_SIGMA;
		params.neighborsSearch = cv::NEIGH_GRID;
		params.randomGeneratorState = int(seed & 0x7fffffff);
		params.sampler = cv::SAMPLING_UNIFORM;
		params.score = cv::SCORE_METHOD_MAGSAC;
		params.threshold = angle * kVirtualFocal;
		params.final_polisher = cv::MAGSAC;
		return params;
	}

	// --- OpenCVGeometrySolver ------------------------------------------------------------

	Provenance OpenCVGeometrySolver::provenance() const
	{
		return {"opencv", cv::getVersionString()};
	}

	std::vector<math::RigidTransformd> OpenCVGeometrySolver::relativePoses(const std::vector<feature::RayPair>& pairs,
																		   double angle, std::uint64_t seed) const
	{
		std::vector<cv::Point2d> a, b;
		for (const feature::RayPair& p : pairs)
		{
			const std::optional<cv::Point2d> na = virtualPixel(p.a);
			const std::optional<cv::Point2d> nb = virtualPixel(p.b);
			if (na && nb)
			{
				a.push_back(*na);
				b.push_back(*nb);
			}
		}
		if (a.size() < 5)
			return {};

		std::vector<math::RigidTransformd> out;
		try
		{
			const cv::Mat camera = virtualCamera();
			cv::Mat mask;
			const cv::Mat essential =
				cv::findEssentialMat(a, b, camera, camera, cv::noArray(), cv::noArray(), mask, usac(angle, seed));
			// Every 3x3 solution, and every one of its four decompositions: x_b = R x_a + t, so each
			// is B from A. Which of them is the camera's is lain's to measure.
			for (int block = 0; block + 3 <= essential.rows; block += 3)
			{
				cv::Mat r1, r2, t;
				cv::decomposeEssentialMat(essential.rowRange(block, block + 3), r1, r2, t);
				const cv::Vec3d tv{t.at<double>(0), t.at<double>(1), t.at<double>(2)};
				for (const cv::Mat& r : {r1, r2})
				{
					const cv::Matx33d rm(r);
					out.push_back(transformOf(rm, tv));
					out.push_back(transformOf(rm, -tv));
				}
			}
		}
		catch (const cv::Exception&)
		{
			return {};
		}
		return out;
	}

	std::vector<math::RigidTransformd> OpenCVGeometrySolver::absolutePoses(const std::vector<feature::PointRay>& pointRays,
																		   double angle, std::uint64_t seed) const
	{
		std::vector<cv::Point3d> objects;
		std::vector<cv::Point2d> rays;
		for (const feature::PointRay& p : pointRays)
		{
			if (const std::optional<cv::Point2d> n = virtualPixel(p.ray))
			{
				objects.emplace_back(p.point.x, p.point.y, p.point.z);
				rays.push_back(*n);
			}
		}
		if (objects.size() < 4)
			return {};

		std::vector<math::RigidTransformd> out;
		try
		{
			cv::Mat camera = virtualCamera();
			cv::Mat rotation, translation;
			std::vector<int> inliers;
			if (!cv::solvePnPRansac(objects, rays, camera, cv::noArray(), rotation, translation, inliers, usac(angle, seed)))
				return {};
			out.push_back(transformOfVectors(rotation, translation));
			// The same pose refined on its own inliers; both are candidates, since lain measures each.
			if (inliers.size() >= 4)
			{
				std::vector<cv::Point3d> inObjects;
				std::vector<cv::Point2d> inRays;
				for (const int i : inliers)
				{
					inObjects.push_back(objects[std::size_t(i)]);
					inRays.push_back(rays[std::size_t(i)]);
				}
				cv::Mat r = rotation.clone(), t = translation.clone();
				cv::solvePnPRefineLM(inObjects, inRays, camera, cv::noArray(), r, t);
				out.push_back(transformOfVectors(r, t));
			}
		}
		catch (const cv::Exception&)
		{
			return {};
		}
		return out;
	}
} // namespace lain::camera::opencv
