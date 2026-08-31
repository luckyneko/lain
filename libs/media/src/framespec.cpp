#include "lain/media/framespec.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

namespace lain::media
{
	double FrameRate::hz() const
	{
		return specified() ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
	}

	std::string FrameRate::toString() const
	{
		if (!specified())
			return "unspecified";
		// {:g} drops the trailing zeros a fixed precision would leave, so 25/1 reads "25 fps"
		// rather than "25.00 fps", while 30000/1001 keeps the digits that distinguish it.
		return lain::string::format("{:g} fps", hz());
	}

	bool operator==(const FrameRate& a, const FrameRate& b)
	{
		// Value equality, not equivalence: 30000/1001 and 60000/2002 are the same rate but not
		// the same declaration, and nothing here needs them to compare equal. Reducing on
		// construction would be the fix if a caller ever does.
		return a.numerator == b.numerator && a.denominator == b.denominator;
	}

	bool operator!=(const FrameRate& a, const FrameRate& b) { return !(a == b); }

	std::string FrameSpec::toString() const
	{
		if (!valid())
			return "unspecified";
		return lain::string::format("{}x{} {} {} · {}", extent.x, extent.y,
									lain::meta::enums::name(pixelFormat),
									lain::meta::enums::name(colorSpace), rate.toString());
	}

	bool operator==(const FrameSpec& a, const FrameSpec& b)
	{
		return a.extent == b.extent && a.pixelFormat == b.pixelFormat && a.colorSpace == b.colorSpace && a.alphaMode == b.alphaMode && a.rate == b.rate;
	}

	bool operator!=(const FrameSpec& a, const FrameSpec& b) { return !(a == b); }

	FrameSpec specOf(const lain::image::Image& image, FrameRate rate)
	{
		FrameSpec spec;
		spec.extent = image.extent();
		spec.pixelFormat = image.pixelFormat();
		spec.colorSpace = image.colorSpace();
		spec.alphaMode = image.alphaMode();
		spec.rate = rate;
		return spec;
	}

	bool matches(const FrameSpec& spec, const lain::image::Image& image)
	{
		// The rate is a property of the sequence, not of any one image, so it takes no part —
		// asking an Image about it would have no answer.
		return image.valid() && image.extent() == spec.extent && image.pixelFormat() == spec.pixelFormat && image.colorSpace() == spec.colorSpace && image.alphaMode() == spec.alphaMode;
	}

	std::optional<FrameSpec> unify(const FrameSpec& a, const FrameSpec& b)
	{
		// An invalid spec is an empty sequence's, and empty imposes no constraint on what it is
		// concatenated with. Without this, appending to an empty sequence would always refuse.
		if (!a.valid())
			return b;
		if (!b.valid())
			return a;

		if (a.extent != b.extent || a.pixelFormat != b.pixelFormat || a.colorSpace != b.colorSpace || a.alphaMode != b.alphaMode)
		{
			return std::nullopt;
		}

		FrameSpec unified = a;
		if (a.rate.specified() && b.rate.specified())
		{
			// Two different declared rates would need a retime to reconcile — a decision no
			// composition should make silently.
			if (a.rate != b.rate)
				return std::nullopt;
		}
		else
		{
			// At most one side declares a rate, so adopting it cannot contradict anything. This
			// is what lets a folder of stills join a video file.
			unified.rate = a.rate.specified() ? a.rate : b.rate;
		}
		return unified;
	}
} // namespace lain::media
