#include "lain/gui/frametexture.h"

#include <cstdint>
#include <utility>

namespace lain::gui
{
	FrameTexture::FrameTexture(std::shared_ptr<detail::FrameTextureSource> source)
		: m_source(std::move(source))
	{
	}

	FrameTexture::operator ImTextureRef() const
	{
		return detail::standInOf(m_source.get());
	}

	namespace detail
	{
		ImTextureID standInOf(const FrameTextureSource* source)
		{
			return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(source)); // null is ImTextureID_Invalid
		}

		std::size_t resolveStandIns(ImDrawData& data, const std::function<std::optional<ImTextureID>(ImTextureID)>& resolve)
		{
			std::size_t replaced = 0;
			for (ImDrawList* list : data.CmdLists)
			{
				for (ImDrawCmd& cmd : list->CmdBuffer)
				{
					// A callback draws nothing of its own, and a texture ImGui manages is the backend's to
					// resolve: its id lives in the ImTextureData, never in the command.
					if (cmd.UserCallback != nullptr || cmd.TexRef._TexData != nullptr)
						continue;
					if (const std::optional<ImTextureID> id = resolve(cmd.TexRef._TexID))
					{
						cmd.TexRef._TexID = *id;
						++replaced;
					}
				}
			}
			return replaced;
		}
	} // namespace detail
} // namespace lain::gui
