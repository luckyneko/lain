#include "lain/render/view.h"

#include <cmath>

namespace lain::render
{
	std::vector<std::string> validate(const View& view)
	{
		std::vector<std::string> problems;
		if (view.image.width == 0 || view.image.height == 0)
			problems.push_back("the view's image is empty");
		const camera::Intrinsics& k = view.intrinsics;
		if (!(std::isfinite(k.fx) && std::isfinite(k.fy) && k.fx > 0 && k.fy > 0))
			problems.push_back("the view's focal length is not positive and finite");
		if (!(std::isfinite(k.cx) && std::isfinite(k.cy)))
			problems.push_back("the view's principal point is not finite");
		const math::Quatd& q = view.worldFromCamera.rotation();
		const math::Vec3d& t = view.worldFromCamera.translation();
		if (!(std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(t.x) &&
			  std::isfinite(t.y) && std::isfinite(t.z)))
			problems.push_back("the view's pose is not finite");
		if (!(std::isfinite(view.nearPlane) && std::isfinite(view.farPlane) && view.nearPlane > 0 && view.nearPlane < view.farPlane))
			problems.push_back("the view's depth range is not 0 < near < far");
		return problems;
	}

	math::Mat4d clipFromCamera(const View& view)
	{
		// Pixel u spans framebuffer [u, u + 1), so lain's u (a pixel's centre) is framebuffer u + 0.5
		// and NDC x = 2 (u + 0.5) / width - 1, where u = fx X / Z + cx. Multiplying through by Z, the
		// clip-space w, makes it linear in the camera point; y the same. Depth is Vulkan's [0, 1].
		const camera::Intrinsics& k = view.intrinsics;
		const double w = view.image.width;
		const double h = view.image.height;
		const double n = view.nearPlane;
		const double f = view.farPlane;
		math::Mat4d m{0.0}; // GLM is column-major: m[column][row]
		m[0][0] = 2 * k.fx / w;
		m[2][0] = 2 * (k.cx + 0.5) / w - 1;
		m[1][1] = 2 * k.fy / h;
		m[2][1] = 2 * (k.cy + 0.5) / h - 1;
		m[2][2] = f / (f - n);
		m[3][2] = -f * n / (f - n);
		m[2][3] = 1;
		return m;
	}
} // namespace lain::render
