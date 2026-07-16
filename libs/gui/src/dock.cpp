#include "lain/gui/dock.h"

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* — kept here so clients don't pull the internal header

namespace lain::gui
{
	static ImGuiDir toImGuiDir(DockDir dir)
	{
		switch (dir)
		{
			case DockDir::Left:
				return ImGuiDir_Left;
			case DockDir::Right:
				return ImGuiDir_Right;
			case DockDir::Up:
				return ImGuiDir_Up;
			case DockDir::Down:
				return ImGuiDir_Down;
		}
		return ImGuiDir_Right;
	}

	DockNode dockSpaceOverViewport()
	{
		return ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
	}

	void dockReset(DockNode dockspace)
	{
		ImGui::DockBuilderRemoveNode(dockspace); // clear any existing layout for this id
		ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);
	}

	DockSplit dockSplit(DockNode node, DockDir dir, float fraction)
	{
		ImGuiID first = 0;
		ImGuiID second = 0;
		ImGui::DockBuilderSplitNode(node, toImGuiDir(dir), fraction, &first, &second);
		return {first, second};
	}

	void dockWindow(DockNode node, const char* windowName)
	{
		ImGui::DockBuilderDockWindow(windowName, node);
	}

	void dockFinish(DockNode dockspace)
	{
		ImGui::DockBuilderFinish(dockspace);
	}

	void activateWindowTab(const char* windowName)
	{
		ImGuiWindow* window = ImGui::FindWindowByName(windowName);
		if (window == nullptr)
			return;
		// Docked in a tab bar: ask the bar to select this window's tab next draw (SelectedTabId holds
		// window->TabId values). Not docked: fall back to plain focus.
		if (window->DockNode != nullptr && window->DockNode->TabBar != nullptr)
			window->DockNode->TabBar->NextSelectedTabId = window->TabId;
		else
			ImGui::FocusWindow(window);
	}
} // namespace lain::gui
