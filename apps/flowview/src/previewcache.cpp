#include "previewcache.h"

#include "valueviews.h"

#include <lain/flow/evaluation.h>
#include <lain/flow/graph.h>
#include <lain/flow/node.h>
#include <lain/flow/port.h>
#include <lain/image/image.h>

#include <set>

namespace flowview
{
	using namespace lain;

	void PreviewCache::refreshIfDirty(const GraphPath& path, const flow::Graph& graph,
									  const flow::Evaluation& evaluation, const ValueViews& views,
									  gui::Context& ctx)
	{
		if (!m_dirty)
			return;
		m_dirty = false;

		std::set<PinKey> live;
		const auto refresh = [&](flow::NodeId id, const flow::Port& p)
		{
			// The registry decides what a value looks like as a still — this loop no longer knows
			// that an image is the thing worth uploading. An unviewable type, an empty slot, or a
			// poster that could not be produced (a frame that would not decode) all yield nothing.
			const flow::PortValue poster = views.poster(evaluation.value(flow::PortAddress{id, p.id()}));
			if (!poster.holds<image::Image>())
				return;
			const image::Image& img = poster.get<image::Image>();
			if (!img.valid())
				return;
			const PinKey key{path, flow::PortAddress{id, p.id()}};
			live.insert(key);
			gui::Texture& tex = m_textures[key]; // default-empty on first sight
			if (!tex.upload(img))				 // re-upload in place when size/format fits...
				tex = ctx.createTexture(img);	 // ...else first-time or resized -> reallocate
		};
		for (const flow::NodeId id : graph.topoOrder())
		{
			const flow::Node& node = graph.node(id);
			for (std::size_t i = 0; i < node.inputCount(); ++i)
				refresh(id, node.input(i));
			for (std::size_t o = 0; o < node.outputCount(); ++o)
				refresh(id, node.output(o));
		}
		// Drop previews whose pin is gone (or no longer posters anything); the erased
		// gui::Texture reclaims its descriptor.
		for (auto it = m_textures.begin(); it != m_textures.end();)
		{
			if (live.count(it->first) == 0)
				it = m_textures.erase(it);
			else
				++it;
		}
	}

	const gui::Texture* PreviewCache::find(const PinKey& key) const
	{
		const auto it = m_textures.find(key);
		if (it == m_textures.end() || !it->second.valid())
			return nullptr;
		return &it->second;
	}
} // namespace flowview
