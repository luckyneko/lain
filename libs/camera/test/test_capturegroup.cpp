// Capture groups: what makes one, what refuses one, the identity that does not depend on the order
// its members were listed in, and by-position grouping of frame-locked footage. And where footage lies
// (location.h): the names and identities that do not depend on where it sits on disk.

#include <lain/camera/capture/capturegroup.h>
#include <lain/camera/capture/location.h>
#include <lain/media/framesource.h>
#include <lain/media/operations.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

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

	// A folder of stills: one single-frame source per frame, `folder`/0000.png onwards.
	media::FrameSequence stills(const std::string& folder, std::size_t frames)
	{
		std::optional<media::FrameSequence> out;
		for (std::size_t i = 0; i < frames; ++i)
		{
			const media::FrameSequence one = footage(folder + "/000" + std::to_string(i) + ".png", 1);
			out = out ? media::concat(*out, one) : one;
			REQUIRE(out.has_value());
		}
		return *out;
	}

	std::vector<std::string> names(const std::vector<media::FrameSequence>& footage)
	{
		std::vector<std::string> out;
		for (const CameraIdentity& c : identitiesByLocation(footage))
			out.push_back(c.value);
		return out;
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

TEST_CASE("a camera is named by where its footage lies within the folder its rig lies in", "[camera][capture][location]")
{
	SECTION("video files")
	{
		const std::vector<media::FrameSequence> rig{footage("/data/rig/cam0.mp4", 3), footage("/data/rig/cam1.mp4", 3)};
		CHECK(footageRoot(rig) == "/data/rig");
		CHECK(names(rig) == std::vector<std::string>{"cam0.mp4", "cam1.mp4"});
	}
	SECTION("folders of stills")
	{
		const std::vector<media::FrameSequence> rig{stills("/data/rig/cam0", 3), stills("/data/rig/cam1", 3)};
		CHECK(footageRoot(rig) == "/data/rig");
		CHECK(names(rig) == std::vector<std::string>{"cam0", "cam1"});
	}
	SECTION("cameras at different depths keep their paths below the root")
	{
		const std::vector<media::FrameSequence> rig{stills("/data/rig/left/cam0", 2), footage("/data/rig/cam1.mp4", 2)};
		CHECK(footageRoot(rig) == "/data/rig");
		CHECK(names(rig) == std::vector<std::string>{"left/cam0", "cam1.mp4"});
	}
	SECTION("one camera is named within the folder holding it")
	{
		CHECK(names({stills("/data/rig/cam0", 3)}) == std::vector<std::string>{"cam0"});
		CHECK(names({footage("/data/clip.mp4", 3)}) == std::vector<std::string>{"clip.mp4"});
		CHECK(footageRoot({footage("/data/clip.mp4", 3)}) == "/data");
	}
	SECTION("two cameras sharing a folder are told apart by their first frame")
	{
		// Two numbered patterns in one folder: both cameras' location is the folder.
		std::optional<media::FrameSequence> a = media::concat(footage("/shots/a.0001.png", 1), footage("/shots/a.0002.png", 1));
		std::optional<media::FrameSequence> b = media::concat(footage("/shots/b.0001.png", 1), footage("/shots/b.0002.png", 1));
		REQUIRE(a.has_value());
		REQUIRE(b.has_value());
		CHECK(names({*a, *b}) == std::vector<std::string>{"shots/a.0001.png", "shots/b.0001.png"});
	}
	SECTION("empty footage names nobody, and the others are still named")
	{
		CHECK(names({footage("/data/rig/cam0.mp4", 2), media::FrameSequence{}, footage("/data/rig/cam1.mp4", 2)}) ==
			  std::vector<std::string>{"cam0.mp4", "", "cam1.mp4"});
		CHECK(footageRoot({}).empty());
	}
}

TEST_CASE("the same footage somewhere else has the same names and the same groups", "[camera][capture][location]")
{
	const std::vector<media::FrameSequence> here{stills("/home/a/rig/cam0", 3), stills("/home/a/rig/cam1", 3)};
	const std::vector<media::FrameSequence> there{stills("/mnt/backup/b/c/rig/cam0", 3), stills("/mnt/backup/b/c/rig/cam1", 3)};
	CHECK(names(here) == names(there));

	const auto grouped = [](const std::vector<media::FrameSequence>& footage)
	{
		const std::vector<CameraIdentity> cameras = identitiesByLocation(footage);
		return groupsByPosition({{cameras[0], footage[0]}, {cameras[1], footage[1]}});
	};
	const GroupingResult a = grouped(here);
	const GroupingResult b = grouped(there);
	REQUIRE(a.groups.size() == 3);
	REQUIRE(b.groups.size() == 3);
	for (std::size_t k = 0; k < 3; ++k)
	{
		CHECK(a.groups[k].identity() == b.groups[k].identity());
		// Its members still name the frames themselves, where they are.
		CHECK(a.groups[k].members()[0].frame.source.toString() == "/home/a/rig/cam0/000" + std::to_string(k) + ".png");
	}

	// A group made with no root spells its frames whole, so it does depend on where they are: the
	// identity by-position grouping gave up.
	const std::vector<CameraIdentity> cameras = identitiesByLocation(here);
	const CaptureGroupResult whole =
		CaptureGroup::create({{cameras[0], here[0].frame(0)}, {cameras[1], here[1].frame(0)}});
	REQUIRE(whole.group.has_value());
	CHECK(whole.group->identity() != a.groups[0].identity());
	const CaptureGroupResult rooted =
		CaptureGroup::create({{cameras[0], here[0].frame(0)}, {cameras[1], here[1].frame(0)}}, footageRoot(here));
	REQUIRE(rooted.group.has_value());
	CHECK(rooted.group->identity() == a.groups[0].identity());
}

TEST_CASE("a source is spelled relative to a root it lies within, and whole otherwise", "[camera][capture][location]")
{
	CHECK(relativeTo(core::Uri{"/data/rig/cam0/0001.png"}, "/data/rig") == "cam0/0001.png");
	CHECK(relativeTo(core::Uri{"/data/rig/cam0/0001.png"}, "/data/rig/") == "cam0/0001.png");
	CHECK(relativeTo(core::Uri{"/elsewhere/cam0.mp4"}, "/data/rig") == "/elsewhere/cam0.mp4");
	CHECK(relativeTo(core::Uri{"/data/rigs/cam0.mp4"}, "/data/rig") == "/data/rigs/cam0.mp4"); // a component, not a prefix
	CHECK(relativeTo(core::Uri{"/data/rig"}, "/data/rig") == "/data/rig");					   // strictly within
	CHECK(relativeTo(core::Uri{"/data/rig/cam0.mp4"}, "") == "/data/rig/cam0.mp4");
	CHECK(relativeTo(core::Uri{"/cam0.mp4"}, "/") == "cam0.mp4");
	CHECK(relativeTo(core::Uri{"C:\\data\\rig\\cam0.mp4"}, "C:/data/rig") == "cam0.mp4");
	CHECK(relativeTo(core::Uri{"s3://bucket/rig/cam0.mp4"}, "s3://bucket/rig") == "cam0.mp4");
}
