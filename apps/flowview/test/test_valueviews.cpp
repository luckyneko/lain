// The type-keyed VALUE VIEW registry: what a type looks like when it is shown.
//
// Driver-free — no window and no device. The half tested here is the POSTER, which is pure: it turns
// a port's value into the still image that stands for it. Drawing needs a live gui::Context and is
// the repo owner's live pass; what can be pinned here is the contract every pane depends on — an
// image posters itself WITHOUT COPYING, and a type with no view posters nothing at all rather than
// something empty-but-present.

#include "valueviews.h"

#include <lain/flow/portvalue.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace lain;

namespace
{
	image::Image grey(std::uint8_t level)
	{
		image::Image made{2, 2, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		for (std::size_t i = 0; i < made.byteSize(); ++i)
			made.data()[i] = level;
		return made;
	}

	flowview::ValueViews builtins()
	{
		flowview::ValueViews views;
		registerBuiltinValueViews(views);
		return views;
	}
} // namespace

TEST_CASE("an image posters itself without copying", "[views]")
{
	const flowview::ValueViews views = builtins();

	flow::PortValue value;
	value.set<image::Image>(grey(42));
	const image::Image& original = value.get<image::Image>();

	const flow::PortValue poster = views.poster(value);
	REQUIRE(poster.holds<image::Image>());

	// THE address, not an equal one: the poster ALIASES the value's payload (PortValue::alias), so a
	// thumbnail costs a refcount bump rather than a deep pixel copy per image port per edit. Make
	// posterOfImage `set` a copy instead and this is the assertion that fails.
	CHECK(&poster.get<image::Image>() == &original);

	// And it OUTLIVES the slot it came from — the aliasing constructor shares the control block, so
	// clearing the source does not free what the cache is about to upload.
	flow::PortValue owner = value;
	const flow::PortValue aliased = views.poster(owner);
	owner.clear();
	REQUIRE(aliased.holds<image::Image>());
	CHECK(aliased.get<image::Image>().valid());
	CHECK(aliased.get<image::Image>().data()[0] == 42);
}

TEST_CASE("a type with no view posters nothing and makes no view", "[views]")
{
	const flowview::ValueViews views = builtins();

	flow::PortValue number;
	number.set<int>(7);
	CHECK_FALSE(views.has(typeid(int)));
	CHECK(views.poster(number).empty());
	CHECK(views.make(typeid(int)) == nullptr);

	// An empty slot posters nothing whatever its type would have been — the panes rely on this to
	// drop a thumbnail for a suppressed output rather than showing the last run's.
	CHECK(views.poster(flow::PortValue{}).empty());
}

TEST_CASE("each previewed target gets its own view instance", "[views]")
{
	const flowview::ValueViews views = builtins();

	// A view owns state — an image's zoom and pan — that belongs to what is on screen, so two makes
	// must not hand back one shared object.
	const auto first = views.make(typeid(image::Image));
	const auto second = views.make(typeid(image::Image));
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	CHECK(first.get() != second.get());
}
