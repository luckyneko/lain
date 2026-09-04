#pragma once

#include <lain/flow/portvalue.h>
#include <lain/math/types.h>

#include <functional>
#include <map>
#include <memory>
#include <typeindex>

namespace lain::gui
{
	class Context;
	class Texture;
} // namespace lain::gui

namespace flowview
{
	// The adapter's type-keyed VIEW registry — CONTEXT.md's deferred "GUI view" seam, and the sibling
	// of ParamEditors: that one is how a type is WRITTEN, this is how a type is SHOWN. flow stays
	// UI-free, so both live here.
	//
	// It has two halves, because a value is shown in two places with two different lifetimes:
	//
	//   a POSTER — the still image that stands for the value in a LIST (the Inspector's and
	//     Interface's thumbnails). It goes into the PreviewCache, which is rebuilt only on an edit
	//     because acm::Texture::upload is a stalling synchronous submit.
	//   a VIEW — the full Preview-pane rendering, which owns VIEW STATE: an image's zoom and pan, a
	//     player's position and its own decoded-frame texture. That state changes with no edit at
	//     all, so it cannot live in the edit-refreshed cache.
	//
	// Before this, every one of those decisions was a `type() == typeid(image::Image)` written out
	// in five places: the cache chose what to upload, two panes chose what drew a thumbnail, and the
	// Preview pane refused everything else outright. A type that can be seen registers here instead,
	// and a type that cannot is text through Evaluation::describe(), exactly as it always was.
	class ValueView
	{
	public:
		virtual ~ValueView() = default;

		// The pane body, inside `area`. `poster` is this pin's cached thumbnail texture (null when
		// there is none) and `gui` is for a view that uploads images of its own — a player decodes a
		// new frame as it plays, which no cache keyed by an edit could hold.
		//
		// ImGui is single-threaded: a view draws only on the main/render thread.
		virtual void draw(const lain::flow::PortValue& value, const lain::gui::Texture* poster,
						  lain::math::Vec2f area, lain::gui::Context& gui) = 0;
	};

	class ValueViews
	{
	public:
		// The image that stands for a value in a list. It returns a PortValue rather than an Image so
		// the Image case can ALIAS its own payload (PortValue::alias) — the thumbnail of an image IS
		// that image, and handing one back by value would charge every image port a deep pixel copy on
		// every edit, which is the cost M5 slice 1 took off the edge path. A type with no still form
		// returns an empty value.
		using Poster = std::function<lain::flow::PortValue(const lain::flow::PortValue&)>;
		// A fresh view instance, made per previewed target: the state belongs to what is on screen.
		using Maker = std::function<std::unique_ptr<ValueView>()>;

		void add(std::type_index type, Poster poster, Maker maker);

		// The poster for `value`, or an empty PortValue when its type has no view (or the slot is
		// empty). The caller checks holds<image::Image>() — a poster is not guaranteed to succeed
		// either, since producing one may mean decoding.
		lain::flow::PortValue poster(const lain::flow::PortValue& value) const;

		// A view for `type`, or null when none is registered — which is what the Preview pane treats
		// as "this cannot be previewed".
		std::unique_ptr<ValueView> make(std::type_index type) const;

		bool has(std::type_index type) const;

	private:
		struct Entry
		{
			Poster poster;
			Maker maker;
		};
		std::map<std::type_index, Entry> m_views;
	};

	// Register the built-in views (image::Image, media::FrameSequence). Called once by the MainWindow.
	void registerBuiltinValueViews(ValueViews& views);
} // namespace flowview
