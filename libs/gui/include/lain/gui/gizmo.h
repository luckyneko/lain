#pragma once

// Re-expose CedricGuillemet/ImGuizmo under a lain face: the 3D manipulation gizmo, translate and
// rotate handles drawn over a pane and dragged to edit a transform. Its functions (Manipulate,
// SetRect, IsUsing, ...) live in namespace ImGuizmo; here that is aliased to lain::gui::gizmo, so a
// consumer writes lain::gui::gizmo::Manipulate(...). A sub-namespace, as nodes.h's is, since
// ImGuizmo declares names that ImGui also has (PushID, GetID, ...).
//
// Its conventions are OpenGL's, not lain's: Manipulate takes a column-major float[16] view and
// projection in OpenGL's camera axes (x right, y up, looking down -z) and clip space (y up, depth -1
// to 1). A consumer converts at the boundary.
//
// It keeps ONE global context, on whichever ImGui context is current, so lain::gui::Context does not
// own one as it does imnodes'. Nor does Context call BeginFrame each frame: BeginFrame opens a
// full-viewport invisible window of its own, which an app without a gizmo has no use for. A pane that
// shows one calls BeginFrame once a frame, then SetDrawlist inside its own window, SetRect over the
// area the view covers, and Manipulate.

#include <imgui.h> // ImGuizmo.h uses ImVec2 / ImDrawList without including it
#include <ImGuizmo.h>

namespace lain::gui
{
	namespace gizmo = ImGuizmo;
}
