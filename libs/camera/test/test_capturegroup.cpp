// Capture groups: what makes one, what refuses one, the identity that does not depend on the order
// its members were listed in, and by-position grouping of frame-locked footage.

#include <lain/camera/capture/capturegroup.h>
#include <lain/media/framesource.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace lain;
using namespace lain::camera::capture;

namespace
{
	class BlankSource : public media::FrameSource
	{
	public:
		BlankSource(std::string uri, std::size_t frames)
			: FrameSource{core::Uri{uri}, spec(), frames}
		{
		}
		static media::FrameSpec spec()
		{
			media::FrameSpec s;
			s.extent = {32, 24};
			s.pixelFormat = image::PixelFormat::Gray8;
			return s;
		}

	protected:
		image::Image decodeFrame(std::size_t) const override { return image::Image{32, 24, image::PixelFormat::Gray8}; }
	};

	media::FrameSequence footage(const std::string& uri, std::size_t frames)
	{
		return media::FrameSequence::over(std::make_shared<BlankSource>(uri, frames));
	}

	media::FrameRef frame(const std::string& source, std::size_t ordinal)
	{
		media::FrameRef f;
		f.source = core::Uri{source};
		f.ordinal = ordinal;
		return f;
	}

	CaptureMember member(const std::string& camera, const std::string& source, std::size_t ordinal)
	{
		return {CameraIdentity{camera}, frame(source, ordinal)};
	}

	bool hasProblem(const CaptureGroupResult& result, GroupProblem problem)
	{
		for (const GroupDiagnostic& d : result.diagnostics)
		{
			if (d.problem == problem)
				return true;
		}
		return false;
	}
} // namespace

TEST_CASE("a capture group holds its members in camera order", "[camera][capture]")
{
	const CaptureGroupResult result =
		CaptureGroup::create({member("right", "/rig/right.mp4", 7), member("left", "/rig/left.mp4", 7)});
	REQUIRE(result.group.has_value());
	CHECK(result.diagnostics.empty());
	const CaptureGroup& group = *result.group;
	REQUIRE(group.members().size() == 2);
	CHECK(group.members()[0].camera.value == "left");
	CHECK(group.members()[1].camera.value == "right");

	const CaptureMember* right = group.member(CameraIdentity{"right"});
	REQUIRE(right != nullptr);
	CHECK(right->frame == frame("/rig/right.mp4", 7));
	CHECK(group.member(CameraIdentity{"middle"}) == nullptr);
	CHECK(group.toString().find("left: frame 7 of /rig/left.mp4") != std::string::npos);
}

TEST_CASE("a capture group's identity is canonical", "[camera][capture]")
{
	const CaptureGroup a =
		*CaptureGroup::create({member("left", "/rig/left.mp4", 7), member("right", "/rig/right.mp4", 7)}).group;
	const CaptureGroup b =
		*CaptureGroup::create({member("right", "/rig/right.mp4", 7), member("left", "/rig/left.mp4", 7)}).group;
	// Pinned, and computed independently of lain (Python's hashlib over the documented text):
	// changing the canonical text is a format change, which must bump its version line.
	CHECK(a.identity() == "d0ad90195adace6741b1a3d1e8b86fad42c8338c5e1f29ba48857def1519854c");
	CHECK(a.identity() == b.identity());

	SECTION("another frame is another group")
	{
		const CaptureGroup c =
			*CaptureGroup::create({member("left", "/rig/left.mp4", 8), member("right", "/rig/right.mp4", 7)}).group;
		CHECK(c.identity() != a.identity());
	}
	SECTION("another camera seeing the same frames is another group")
	{
		const CaptureGroup c =
			*CaptureGroup::create({member("left", "/rig/left.mp4", 7), member("centre", "/rig/right.mp4", 7)}).group;
		CHECK(c.identity() != a.identity());
	}
	SECTION("a name cannot run into its neighbour")
	{
		// Without the length prefixes, these two spell the same text: "a b /c 1".
		const CaptureGroup c = *CaptureGroup::create({member("a b", "/c", 1)}).group;
		const CaptureGroup d = *CaptureGroup::create({member("a", "b /c", 1)}).group;
		CHECK(c.identity() != d.identity());
	}
}

TEST_CASE("a malformed capture group is refused with every reason", "[camera][capture]")
{
	SECTION("no members")
	{
		const CaptureGroupResult result = CaptureGroup::create({});
		CHECK_FALSE(result.group.has_value());
		CHECK(hasProblem(result, GroupProblem::NoMembers));
	}
	SECTION("a member with no camera and a member with no frame")
	{
		const CaptureGroupResult result =
			CaptureGroup::create({member("", "/rig/left.mp4", 0), {CameraIdentity{"right"}, media::FrameRef{}}});
		CHECK_FALSE(result.group.has_value());
		CHECK(hasProblem(result, GroupProblem::NoCameraIdentity));
		CHECK(hasProblem(result, GroupProblem::NoFrame));
	}
	SECTION("one frame from two cameras")
	{
		const CaptureGroupResult result =
			CaptureGroup::create({member("left", "/rig/both.mp4", 3), member("right", "/rig/both.mp4", 3)});
		CHECK_FALSE(result.group.has_value());
		CHECK(hasProblem(result, GroupProblem::FrameTwice));
	}
	SECTION("one camera twice")
	{
		const CaptureGroupResult result =
			CaptureGroup::create({member("left", "/rig/left.mp4", 0), member("left", "/rig/left.mp4", 1)});
		CHECK_FALSE(result.group.has_value());
		CHECK(hasProblem(result, GroupProblem::CameraTwice));
	}
}

TEST_CASE("by-position grouping makes frame k of every camera group k", "[camera][capture]")
{
	const GroupingResult result = groupsByPosition({{CameraIdentity{"b"}, footage("/rig/b", 3)},
													{CameraIdentity{"a"}, footage("/rig/a", 5)},
													{CameraIdentity{"c"}, footage("/rig/c", 4)}});
	CHECK(result.diagnostics.empty());
	REQUIRE(result.groups.size() == 5);
	CHECK(result.groups[0].members().size() == 3);
	CHECK(result.groups[2].members().size() == 3);
	CHECK(result.groups[3].members().size() == 2); // b has ended
	REQUIRE(result.groups[4].members().size() == 1);
	CHECK(result.groups[4].members()[0].camera.value == "a");
	for (std::size_t k = 0; k < result.groups.size(); ++k)
	{
		for (const CaptureMember& m : result.groups[k].members())
			CHECK(m.frame.ordinal == k);
	}
	// The same footage listed in another order makes the same groups.
	const GroupingResult shuffled = groupsByPosition({{CameraIdentity{"c"}, footage("/rig/c", 4)},
													  {CameraIdentity{"a"}, footage("/rig/a", 5)},
													  {CameraIdentity{"b"}, footage("/rig/b", 3)}});
	REQUIRE(shuffled.groups.size() == result.groups.size());
	for (std::size_t k = 0; k < result.groups.size(); ++k)
		CHECK(shuffled.groups[k].identity() == result.groups[k].identity());
}

TEST_CASE("by-position grouping says why a position made no group", "[camera][capture]")
{
	const GroupingResult result =
		groupsByPosition({{CameraIdentity{"a"}, footage("/rig/a", 2)}, {CameraIdentity{"a"}, footage("/rig/b", 2)}});
	CHECK(result.groups.empty());
	REQUIRE(result.diagnostics.size() == 2);
	CHECK(result.diagnostics[0].problem == GroupProblem::CameraTwice);
	CHECK(result.diagnostics[0].detail.find("position 0") == 0);
	CHECK(result.diagnostics[1].detail.find("position 1") == 0);
}
