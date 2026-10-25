#include "lain/rigplan/truth.h"

#include "lain/rigplan/mover.h"

#include <lain/camera/projection.h>

#include <algorithm>
#include <optional>

namespace lain::rigplan
{
	std::vector<CameraPose> cameraPoses(const Realisation& realisation)
	{
		std::vector<CameraPose> poses;
		for (const RealisedCamera& camera : realisation.cameras)
			poses.push_back({camera.id, camera.name, camera.planFromCamera});
		return poses;
	}

	// Whether `pixel` lies on `image`: pixel (0, 0) is the centre of the top-left pixel, so the image
	// covers [-0.5, width - 0.5) across and [-0.5, height - 0.5) down.
	static bool onImage(const camera::ImageGeometry& image, double u, double v)
	{
		return u >= -0.5 && v >= -0.5 && u < double(image.width) - 0.5 && v < double(image.height) - 0.5;
	}

	std::vector<BoardTruth> boardTruth(const RigPlan& plan, const Realisation& realisation, std::size_t frame)
	{
		std::vector<BoardTruth> out;
		for (const BoardMover& board : plan.boards)
		{
			const std::optional<camera::board::Specification> specification = specificationOf(board);
			if (!specification || board.path.keys.empty())
				continue;
			BoardTruth truth;
			truth.board = board.id;
			truth.name = board.name;
			truth.frame = frame;
			truth.planFromBoard = planFromBoard(board, frame);
			for (const RealisedCamera& camera : realisation.cameras)
			{
				CameraCorners corners;
				corners.camera = camera.id;
				corners.name = camera.name;
				const math::RigidTransformd cameraFromBoard = camera.planFromCamera.inverse() * truth.planFromBoard;
				// The camera's centre in the board frame: in front of the print is Z < 0, since Z runs into
				// the board. From behind, the corners are hidden by the board itself.
				const math::Vec3d centre = cameraFromBoard.inverse().translation();
				if (centre.z < 0)
				{
					const camera::ImageGeometry& image = camera.model.parameters().image;
					for (std::uint32_t id = 0; id < specification->pattern().cornerCount(); ++id)
					{
						const math::Vec3d p = cameraFromBoard.apply(*specification->cornerPosition(id));
						const camera::Projection<double> projected = camera::project(camera.model, p.x, p.y, p.z);
						if (projected.ok() && onImage(image, projected.u, projected.v))
							corners.inView.push_back({id, {projected.u, projected.v}});
					}
				}
				truth.cameras.push_back(std::move(corners));
			}
			out.push_back(std::move(truth));
		}
		return out;
	}

	const RealisedCamera* referenceCamera(const RigPlan& plan, const Realisation& realisation)
	{
		if (realisation.cameras.empty())
			return nullptr;
		if (plan.reference)
		{
			const auto found = std::find_if(realisation.cameras.begin(), realisation.cameras.end(),
											[&](const RealisedCamera& c)
											{ return c.id == *plan.reference; });
			if (found != realisation.cameras.end())
				return &*found;
		}
		return &realisation.cameras.front();
	}

	camera::registration::RegisteredRig rigRelativeTo(const std::vector<CameraPose>& poses, const std::string& reference)
	{
		const auto found = std::find_if(poses.begin(), poses.end(), [&](const CameraPose& p)
										{ return p.name == reference; });
		const math::RigidTransformd referenceFromPlan = found->planFromCamera.inverse();
		camera::registration::RegisteredRig rig;
		rig.reference = camera::capture::CameraIdentity{reference};
		rig.scale = {camera::registration::Scale::Metric, "the realisation a synthetic capture was rendered with"};
		for (const CameraPose& pose : poses)
		{
			// The reference is the identity exactly, as a rig document requires, not a product that
			// rounds to it.
			const math::RigidTransformd referenceFromCamera =
				pose.name == reference ? math::RigidTransformd{} : referenceFromPlan * pose.planFromCamera;
			rig.cameras.push_back({camera::capture::CameraIdentity{pose.name}, referenceFromCamera});
		}
		std::sort(rig.cameras.begin(), rig.cameras.end(), [](const auto& a, const auto& b)
				  { return a.camera < b.camera; });
		return rig;
	}

	camera::registration::RegisteredRig truthRig(const RigPlan& plan, const Realisation& realisation)
	{
		return rigRelativeTo(cameraPoses(realisation), referenceCamera(plan, realisation)->name);
	}
} // namespace lain::rigplan
