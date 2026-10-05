#include "lain/camera/capture/capturegroup.h"

#include <lain/core/sha256.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace lain::camera::capture
{
	// The text a group's identity is the digest of: a version line, then each member's camera
	// identity, source and ordinal, each length-prefixed so no two different member lists can spell
	// the same text. The version line changes if this does, and a group's identity with it.
	static std::string canonicalText(const std::vector<CaptureMember>& members)
	{
		std::string text = "lain capture group 1\n";
		for (const CaptureMember& m : members)
		{
			const std::string source = m.frame.source.toString();
			text += std::to_string(m.camera.value.size()) + ":" + m.camera.value + " ";
			text += std::to_string(source.size()) + ":" + source + " ";
			text += std::to_string(m.frame.ordinal) + "\n";
		}
		return text;
	}

	CaptureGroupResult CaptureGroup::create(std::vector<CaptureMember> members)
	{
		CaptureGroupResult out;
		if (members.empty())
			out.diagnostics.push_back({GroupProblem::NoMembers, "a capture group needs at least one member"});
		for (const CaptureMember& m : members)
		{
			if (m.camera.empty())
				out.diagnostics.push_back({GroupProblem::NoCameraIdentity, "a member has no camera identity"});
			if (!m.frame.valid())
				out.diagnostics.push_back(
					{GroupProblem::NoFrame, "the member from camera \"" + m.camera.value + "\" names no frame"});
		}
		std::stable_sort(members.begin(), members.end(),
						 [](const CaptureMember& a, const CaptureMember& b)
						 { return a.camera < b.camera; });
		for (std::size_t i = 1; i < members.size(); ++i)
		{
			if (members[i].camera == members[i - 1].camera && !members[i].camera.empty())
				out.diagnostics.push_back(
					{GroupProblem::CameraTwice, "camera \"" + members[i].camera.value + "\" appears more than once"});
		}
		// One frame named by two cameras, found by sorting rather than comparing every pair, since a
		// group of a hundred cameras is checked once per capture instant.
		std::vector<const CaptureMember*> byFrame;
		for (const CaptureMember& m : members)
		{
			if (m.frame.valid())
				byFrame.push_back(&m);
		}
		const auto frameKey = [](const CaptureMember* m)
		{ return std::make_pair(std::string_view(m->frame.source.toString()), m->frame.ordinal); };
		std::sort(byFrame.begin(), byFrame.end(),
				  [&](const CaptureMember* a, const CaptureMember* b)
				  { return frameKey(a) < frameKey(b); });
		for (std::size_t i = 1; i < byFrame.size(); ++i)
		{
			if (byFrame[i]->frame == byFrame[i - 1]->frame)
				out.diagnostics.push_back({GroupProblem::FrameTwice, "cameras \"" + byFrame[i - 1]->camera.value +
																		 "\" and \"" + byFrame[i]->camera.value +
																		 "\" name one frame, " + byFrame[i]->frame.toString()});
		}
		if (!out.diagnostics.empty())
			return out;
		std::string identity = core::sha256(canonicalText(members)).toString();
		out.group = CaptureGroup{std::move(identity), std::move(members)};
		return out;
	}

	const CaptureMember* CaptureGroup::member(const CameraIdentity& camera) const
	{
		const auto found = std::lower_bound(m_members.begin(), m_members.end(), camera,
											[](const CaptureMember& m, const CameraIdentity& c)
											{ return m.camera < c; });
		return found != m_members.end() && found->camera == camera ? &*found : nullptr;
	}

	std::string CaptureGroup::toString() const
	{
		std::string out = "capture group " + m_identity.substr(0, 12) + " (";
		for (std::size_t i = 0; i < m_members.size(); ++i)
		{
			if (i > 0)
				out += ", ";
			out += m_members[i].camera.value + ": " + m_members[i].frame.toString();
		}
		return out + ")";
	}

	GroupingResult groupsByPosition(const std::vector<CameraFootage>& cameras)
	{
		GroupingResult out;
		std::size_t longest = 0;
		for (const CameraFootage& c : cameras)
			longest = std::max(longest, c.footage.size());
		for (std::size_t k = 0; k < longest; ++k)
		{
			std::vector<CaptureMember> members;
			for (const CameraFootage& c : cameras)
			{
				if (k < c.footage.size())
					members.push_back({c.camera, c.footage.frame(k)});
			}
			CaptureGroupResult group = CaptureGroup::create(std::move(members));
			if (group.group)
				out.groups.push_back(std::move(*group.group));
			for (GroupDiagnostic& d : group.diagnostics)
			{
				d.detail = "position " + std::to_string(k) + ": " + d.detail;
				out.diagnostics.push_back(std::move(d));
			}
		}
		return out;
	}
} // namespace lain::camera::capture
