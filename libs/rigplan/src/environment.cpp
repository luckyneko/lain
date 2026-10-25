#include "lain/rigplan/environment.h"

#include "lain/rigplan/rigplan.h" // kPlanFrame

namespace lain::rigplan
{
	math::AxisConvention conventionOf(UpAxis up)
	{
		switch (up)
		{
			case UpAxis::Y:
				return math::AxisConvention::XRightYUpZBackward();
			case UpAxis::Z:
				return kPlanFrame;
		}
		return kPlanFrame;
	}

	math::Mat4d planFromProp(const Prop& prop, std::size_t frame)
	{
		const math::Mat3d basis = math::basisChange<double>(conventionOf(prop.up), kPlanFrame);
		math::Mat4d placed;
		if (prop.path)
		{
			// A path's pose is in camera-frame axes. Undoing the camera's rest orientation leaves the turn
			// from an upright prop, so a key with only a pan is a yaw.
			const math::Quatd rest = math::quat_cast(math::basisChange<double>(camera::kCameraFrame, kPlanFrame));
			const math::RigidTransformd pose = poseAt(*prop.path, frame);
			placed = math::translate(math::Mat4d(1.0), pose.translation()) * math::mat4_cast(pose.rotation() * math::inverse(rest));
		}
		else
		{
			placed = math::rotate(math::translate(math::Mat4d(1.0), prop.position), math::radians(prop.yawDegrees), math::Vec3d{0.0, 0.0, 1.0});
		}
		return math::scale(placed, math::Vec3d(prop.unitMetres)) * math::Mat4d(basis);
	}
} // namespace lain::rigplan
