#pragma once

#include <lain/media/frameref.h>
#include <lain/media/framesequence.h>

#include <optional>
#include <string>
#include <vector>

// Which frames were captured together (ADR-0016: lain::camera::capture owns capture association,
// registration only consumes it).
namespace lain::camera::capture
{
	// Which camera a frame came from (CONTEXT.md, "Camera identity"): durable and opaque, assigned by
	// whatever knows the rig, and preserved through grouping, registration and reports. Not a
	// position in some list, which changes when the list does.
	struct CameraIdentity
	{
		std::string value;

		bool empty() const { return value.empty(); }
		std::string toString() const { return value; }

		bool operator==(const CameraIdentity& rhs) const { return value == rhs.value; }
		bool operator!=(const CameraIdentity& rhs) const { return value != rhs.value; }
		bool operator<(const CameraIdentity& rhs) const { return value < rhs.value; }
	};

	// One camera's frame in a capture group.
	struct CaptureMember
	{
		CameraIdentity camera;
		media::FrameRef frame;
	};

	enum class GroupProblem
	{
		NoMembers,		  // a group of nothing
		NoCameraIdentity, // a member with an empty camera identity
		NoFrame,		  // a member whose frame reference names no source
		CameraTwice,	  // two members from one camera: a group holds at most one frame per camera
		FrameTwice,		  // two members naming one frame: a frame comes from one camera
	};

	struct GroupDiagnostic
	{
		GroupProblem problem;
		std::string detail;
	};

	struct CaptureGroupResult;

	// At most one frame from each camera, associated with one capture instant (CONTEXT.md, "Capture
	// group"). Explicit: built by whoever knows how the rig was triggered, never inferred here.
	//
	// IMMUTABLE and CANONICAL: create() sorts the members by camera identity and derives the group's
	// identity from them, so one set of frames is one group with one identity however its members
	// were listed.
	class CaptureGroup
	{
	public:
		static CaptureGroupResult create(std::vector<CaptureMember> members);

		// A digest of the members' camera identities and frame identities (SHA-256, hex), stable
		// across runs and input orders.
		const std::string& identity() const { return m_identity; }
		// Ascending by camera identity.
		const std::vector<CaptureMember>& members() const { return m_members; }
		// The member from `camera`, or nullptr when the camera is absent from this group.
		const CaptureMember* member(const CameraIdentity& camera) const;

		// One line for a person: the identity's start and the members.
		std::string toString() const;

	private:
		CaptureGroup(std::string identity, std::vector<CaptureMember> members)
			: m_identity(std::move(identity))
			, m_members(std::move(members))
		{
		}

		std::string m_identity;
		std::vector<CaptureMember> m_members;
	};

	struct CaptureGroupResult
	{
		std::optional<CaptureGroup> group;
		std::vector<GroupDiagnostic> diagnostics; // empty exactly when `group` is set
	};

	// A camera and its footage, for by-position grouping.
	struct CameraFootage
	{
		CameraIdentity camera;
		media::FrameSequence footage;
	};

	struct GroupingResult
	{
		std::vector<CaptureGroup> groups;
		std::vector<GroupDiagnostic> diagnostics; // why a position made no group
	};

	// By-position grouping (CONTEXT.md): frame k of every camera is group k, and a camera with fewer
	// frames is absent from the groups past its end. It reads no timestamp, so it is right only when
	// the cameras were triggered together, as a frame-locked rig is. Two cameras with one identity
	// make every position fail, with the reason.
	GroupingResult groupsByPosition(const std::vector<CameraFootage>& cameras);
} // namespace lain::camera::capture
