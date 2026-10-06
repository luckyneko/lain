#include "lain/camera/scalepolicy.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace lain::camera
{
	double resolveScale(const ScalePolicy& policy, const ImageGeometry& image)
	{
		return std::visit(
			[&image](const auto& p) -> double
			{
				using P = std::decay_t<decltype(p)>;
				if constexpr (std::is_same_v<P, NativeScale>)
				{
					(void)p;
					return 1.0;
				}
				else if constexpr (std::is_same_v<P, ScaleFactor>)
				{
					// Zero, negative or non-finite asks for nothing coherent; native is the one
					// answer that cannot lose a board.
					return std::isfinite(p.factor) && p.factor > 0 ? std::min(p.factor, 1.0) : 1.0;
				}
				else
				{
					const std::uint32_t longest = std::max(image.width, image.height);
					if (p.pixels == 0 || longest == 0)
						return 1.0;
					return std::min(1.0, double(p.pixels) / double(longest));
				}
			},
			policy);
	}
} // namespace lain::camera
