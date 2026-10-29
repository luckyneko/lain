#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Which evidence a camera method holds out to validate on, and how it draws its bootstrap resamples:
// every registration method and every calibration method decides these the same way. Private to
// lain::camera, so the methods share one answer without any naming another (ADR-0016). Each method
// has its own evidence unit (a capture group, a feature track, a frame); these take units as numbers.
namespace lain::camera::detail
{
	// Which of `count` units are held out to validate on: every k-th, in order, starting half a stride
	// in, where k is 1/fraction rounded (at least 2), each only when `mayHold(u)` agrees. Nothing is held
	// out with a fraction of zero or fewer than `minimum` units. `mayHold` is asked once per candidate,
	// in order, and may keep state: when it answers true, the unit is held out. A method's guard says
	// what must survive the hold-out: what is held out must not decide whether the method can succeed
	// at all.
	std::vector<bool> holdOut(std::size_t count, double fraction, std::size_t minimum,
							  const std::function<bool(std::size_t)>& mayHold);

	// The same, guarded by connectivity: a unit is kept in when holding it out would cut a camera pair's
	// last shared unit AND so split the camera graph. `unitCameras[u]` lists the cameras that saw unit u.
	std::vector<bool> holdOut(std::size_t cameras, const std::vector<std::vector<std::uint32_t>>& unitCameras,
							  double fraction, std::size_t minimum);

	// `resamples` bootstrap draws from `units`, each as many as there are units, with replacement.
	// They come from one engine seeded with `seed`, in order, before anything runs, so they are the same
	// under either execution policy. A plain modulo maps a draw to an index, since the standard's
	// distributions are implementation-defined.
	std::vector<std::vector<std::uint32_t>> bootstrapDraws(const std::vector<std::uint32_t>& units,
														   std::uint32_t resamples, std::uint64_t seed);
} // namespace lain::camera::detail
