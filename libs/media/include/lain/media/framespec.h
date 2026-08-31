#pragma once

#include <lain/image/colorspace.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/math/types.h>

#include <cstdint>
#include <optional>
#include <string>

namespace lain::media
{
	// A nominal frame rate as an exact rational — 30000/1001, not 29.97.
	//
	// Rational rather than a double because the common broadcast rates are not representable
	// in decimal, and a rate that has been through a double cannot be written back into a
	// container's timebase without drifting. A sequence's rate is NOMINAL: variable-frame-rate
	// material is handled by frames being ordinal and only their timestamps irregular
	// (CONTEXT.md, *Frame table*), so this describes the intent, never the spacing.
	//
	// Zero numerator means UNSPECIFIED, which is a real state rather than a missing value: a
	// folder of stills genuinely has no rate, and ADR-0018 promises a sequence can span "a
	// video file and a folder of stills".
	struct FrameRate
	{
		std::uint32_t numerator = 0;
		std::uint32_t denominator = 1;

		constexpr bool specified() const { return numerator != 0 && denominator != 0; }

		// Frames per second, or 0.0 when unspecified. For display and for a writer's default;
		// never round-trip a rate through this.
		double hz() const;

		std::string toString() const; // "29.97 fps" / "unspecified"
	};

	bool operator==(const FrameRate& a, const FrameRate& b);
	bool operator!=(const FrameRate& a, const FrameRate& b);

	// The single declared shape of every frame in a sequence.
	//
	// A SEQUENCE IS HOMOGENEOUS (ADR-0018): composing sources whose frames do not match is
	// refused at the point of composition, because the only way a consumer could cope is by
	// converting, and a conversion chosen downstream on a frame it did not know would differ is
	// exactly the silent lossy conversion this design exists to prevent. It is also what lets an
	// encoder be opened before the first frame arrives.
	struct FrameSpec
	{
		lain::math::Vec2i extent{0, 0};
		lain::image::PixelFormat pixelFormat = lain::image::PixelFormat::RGBA8;
		lain::image::ColorSpace colorSpace = lain::image::ColorSpace::Unspecified;
		lain::image::AlphaMode alphaMode = lain::image::AlphaMode::Unspecified;
		FrameRate rate{};

		// An unset spec — what an empty sequence carries. Distinct from "a spec whose frames
		// happen to be 0x0", which cannot occur.
		bool valid() const { return extent.x > 0 && extent.y > 0; }

		std::string toString() const; // "3840x2160 RGB8 sRGB · 29.97 fps"
	};

	bool operator==(const FrameSpec& a, const FrameSpec& b);
	bool operator!=(const FrameSpec& a, const FrameSpec& b);

	// The spec `image` would contribute to a sequence, at `rate`.
	FrameSpec specOf(const lain::image::Image& image, FrameRate rate = {});

	// Whether `image` may be delivered as a frame of `spec` — the homogeneity question asked in
	// ONE place, so a source validating its own frames and a composition validating two specs
	// cannot drift apart. The rate is not an image property and takes no part.
	bool matches(const FrameSpec& spec, const lain::image::Image& image);

	// The one spec covering both, or nullopt when they cannot appear in one sequence.
	//
	// Pixel geometry and the two colour tags must agree exactly. THE RATE IS THE ONE AXIS THAT
	// TOLERATES ABSENCE: an unspecified rate adopts the other side's rather than refusing, which
	// is what makes ADR-0018's promise — a sequence spanning "a video file and a folder of
	// stills" — actually work. Two DIFFERENT specified rates are still refused: reconciling them
	// is a retime, which is a decision no composition should make silently.
	//
	// An invalid spec (an empty sequence's) imposes no constraint and unifies with anything.
	[[nodiscard]] std::optional<FrameSpec> unify(const FrameSpec& a, const FrameSpec& b);
} // namespace lain::media
