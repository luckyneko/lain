// The type-keyed VALUE VIEW registry (M10 slice 7): what a type looks like when it is shown.
//
// Driver-free — no window and no device. The half tested here is the POSTER, which is pure: it turns
// a port's value into the still image that stands for it. Drawing needs a live gui::Context and is
// the repo owner's live pass; what can be pinned here is the contract every pane depends on — an
// image posters itself WITHOUT COPYING, a sequence posters a decoded frame, and a type with no view
// posters nothing at all rather than something empty-but-present.

#include "graphio.h" // registerSceneSerialization — the port types the scene declares
#include "valueviews.h"

#include <lain/flow/portvalue.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/io/image/codecs.h>
#include <lain/io/image/save.h>
#include <lain/io/sequence/open.h>
#include <lain/io/sequence/openers.h>
#include <lain/io/video/codecs.h>
#include <lain/media/framesequence.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

using namespace lain;
namespace fs = std::filesystem;

namespace
{
	void ensureCodecs()
	{
		static const bool once = []
		{
			io::image::registerImageCodecs();
			io::video::registerVideoCodecs();
			io::sequence::registerSequenceOpeners();
			flowview::registerSceneSerialization();
			return true;
		}();
		(void)once;
	}

	fs::path scratchDir(const std::string& name)
	{
		const fs::path dir = fs::temp_directory_path() / ("lain_views_" + name);
		fs::remove_all(dir);
		fs::create_directories(dir);
		return dir;
	}

	image::Image grey(std::uint8_t level)
	{
		image::Image made{2, 2, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		for (std::size_t i = 0; i < made.byteSize(); ++i)
			made.data()[i] = level;
		return made;
	}

	// A folder of stills, opened through the production opener seam — the same path the gui takes.
	media::FrameSequence stillsIn(const fs::path& dir, int frames)
	{
		ensureCodecs();
		for (int i = 0; i < frames; ++i)
			REQUIRE(io::image::save((dir / ("f" + std::to_string(i) + ".png")).string(), grey(static_cast<std::uint8_t>(10 + i * 40))));
		auto sequence = io::sequence::open(dir.string());
		REQUIRE(sequence);
		return *sequence;
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

TEST_CASE("a frame sequence posters its first frame", "[views]")
{
	const flowview::ValueViews views = builtins();
	const fs::path dir = scratchDir("poster");

	flow::PortValue value;
	value.set<media::FrameSequence>(stillsIn(dir, 3));

	const flow::PortValue poster = views.poster(value);
	REQUIRE(poster.holds<image::Image>());
	const image::Image& shown = poster.get<image::Image>();
	REQUIRE(shown.valid());

	// Frame 0 specifically — the sequence is sorted, so this is the first still and not merely some
	// still. Each fixture frame carries its own grey level, which is what makes that checkable.
	const image::Image first = value.get<media::FrameSequence>().image(0);
	REQUIRE(first.valid());
	REQUIRE(shown.byteSize() == first.byteSize());
	CHECK(shown.data()[0] == first.data()[0]);
	CHECK(shown.data()[0] != value.get<media::FrameSequence>().image(1).data()[0]);

	fs::remove_all(dir);
}

TEST_CASE("an empty sequence posters nothing", "[views]")
{
	const flowview::ValueViews views = builtins();
	const fs::path dir = scratchDir("empty");
	ensureCodecs();

	// A folder that exists and holds no images: a VALUE (zero frames), not a failure — so it has a
	// view but no poster, and the pin still describes itself in text.
	auto sequence = io::sequence::open(dir.string());
	REQUIRE(sequence);
	REQUIRE(sequence->empty());

	flow::PortValue value;
	value.set<media::FrameSequence>(*sequence);
	CHECK(views.poster(value).empty());
	CHECK(views.has(typeid(media::FrameSequence)));

	fs::remove_all(dir);
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

	// A view owns state — zoom, a playback position — that belongs to what is on screen, so two
	// makes must not hand back one shared object.
	const auto first = views.make(typeid(image::Image));
	const auto second = views.make(typeid(image::Image));
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	CHECK(first.get() != second.get());
	CHECK(views.make(typeid(media::FrameSequence)) != nullptr);
}
