#pragma once

namespace lain::rigplan
{
	// How a camera or light is aimed on its mount, degrees, in the mount's frame (which is the plan
	// frame turned by the mount's yaw). At rest, every aim zero, it looks along the mount's +Y with its
	// image's right along +X and its image's down along -Z. Then, in this order:
	//   * roll turns it about its forward axis — positive is clockwise as seen from behind it;
	//   * tilt turns it about the mount's +X — positive looks up;
	//   * pan turns it about the mount's +Z — positive turns left, counter-clockwise from above.
	struct Aim
	{
		double panDegrees = 0;
		double tiltDegrees = 0;
		double rollDegrees = 0;
	};
} // namespace lain::rigplan
