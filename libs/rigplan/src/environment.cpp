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

	math::Mat4d planFromProp(const Prop& prop)
	{
		const math::Mat3d basis = math::basisChange<double>(conventionOf(prop.up), kPlanFrame);
		const math::Mat4d placed = math::rotate(math::translate(math::Mat4d(1.0), prop.position), math::radians(prop.yawDegrees),
												math::Vec3d{0.0, 0.0, 1.0});
		return math::scale(placed, math::Vec3d(prop.unitMetres)) * math::Mat4d(basis);
	}
} // namespace lain::rigplan
