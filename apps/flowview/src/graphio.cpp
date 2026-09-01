#include "graphio.h"

#include <lain/data/data.h>
#include <lain/flow/graph.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/serialize/serialize.h>
#include <lain/image/color.h>
#include <lain/image/image.h>
#include <lain/io/data/codecs.h> // registerDataCodecs
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/io/uri.h>
#include <lain/media/frameposition.h>
#include <lain/media/frameref.h>
#include <lain/media/framesequence.h>

#include <filesystem>

// image::ColorRGBf's serialize() — defined in lain::image so ADL finds it. flowview owns this
// because it names both the type (image) and the archive (data); lain::image itself stays data-free,
// so this bridge can't live there. Its .r/.g/.b are GLM lvalue members, so member() binds both ways.
namespace lain::image
{
	void serialize(lain::data::Archive& archive, ColorRGBf& color)
	{
		archive.member("r", color.r).member("g", color.g).member("b", color.b);
	}
} // namespace lain::image

// media::FramePosition's serialize() — here for the same reason as the one above: it names both
// the type (media) and the archive (data), and lain::media stays data-free so a graph's serializer
// is not a dependency of the sequence model. A FramePosition IS serialized, because FrameAtNode
// declares it as an input default and a default is a param underneath.
//
// FrameSpec's and FrameRate's bridges are NOT here — they live in lain::media::serialize, the
// target that already names both sides, beside the manifest that needs them.
namespace lain::media
{
	LAIN_SERIALIZE(FramePosition, value)
} // namespace lain::media

namespace flowview
{
	using namespace lain;

	flow::serialize::ValueCodecs sceneCodecs()
	{
		flow::serialize::ValueCodecs codecs;
		codecs.registerType<bool>("bool"); // ConstantNode<bool> (a Gate's enable source)
		codecs.registerType<int>("int");   // ConstantNode<int> (a Select's selector source)
		codecs.registerType<float>("float");
		codecs.registerType<std::string>("string"); // ConstantNode<std::string> (a ListDir extension)
		codecs.registerType<std::filesystem::path>("path");
		codecs.registerType<image::ColorRGBf>("color");
		codecs.registerType<media::FramePosition>("framePosition"); // FrameAt's position default
		return codecs;
	}

	void registerSceneSerialization()
	{
		// Addable port types = what a dynamic pin (a boundary pin, a Merge/Select branch) can be. The
		// scene payload plus the scalars, so a graph's boundary interface can carry numbers/flags/text
		// (bindable from the cli via BoundaryBinders), not just images.
		flow::registerPortType<image::Image>("Image");
		flow::registerPortType<int>("Int");
		flow::registerPortType<float>("Float");
		flow::registerPortType<bool>("Bool");
		flow::registerPortType<std::string>("String");
		flow::registerPortType<std::filesystem::path>("Path");

		// The frame-sequence vocabulary (M10). All three are registered because an unregistered
		// type's boundary pin cannot be NAMED on save — it is skipped, taking its wiring with it —
		// and a sweep binds a FrameSequence and a FramePosition at exactly that boundary.
		flow::registerPortType<media::FrameSequence>("FrameSequence");
		flow::registerPortType<media::FrameRef>("FrameRef");
		flow::registerPortType<media::FramePosition>("FramePosition");

		// COLLECTION types (M8). Registering a list form is what makes its element MAPPABLE — a
		// PortType knows its element type but nothing can walk that backwards, so a map lifts an
		// inner pin through this registry (ADR-0014). It is also what lets a collection pin name its
		// type on disk, which a map's stored interface needs.
		flow::registerPortType<std::vector<image::Image>>("ListOfImage");
		flow::registerPortType<std::vector<std::filesystem::path>>("ListOfPath");
		io::data::registerDataCodecs(); // json (Value <-> bytes)
	}

	bool saveGraph(const std::string& uri, const flow::Graph& graph,
				   const core::Factory<flow::Node>& factory, const flow::serialize::EditorTree& editor)
	{
		const data::Value document = flow::serialize::toValue(graph, factory, sceneCodecs(), editor);
		return io::data::save(uri, document);
	}

	std::string templateKey(const std::filesystem::path& path)
	{
		// ONE canonicalisation in the tree. io::canonicalUri owns the rule (weakly_canonical, so a
		// template that does not exist yet still has a stable key, which is what lets a broken link
		// heal when the file appears); this names the CONCEPT — a template's cache key — and is what
		// the four call sites read. A key computed two ways eventually disagrees with itself, and
		// the failure is silent: an invalidation that misses simply keeps serving the definition it
		// was told to drop.
		return lain::io::canonicalUri(path.string());
	}

	flow::serialize::TemplateResolver templateResolver(const std::filesystem::path& documentDir)
	{
		// A linked group's `source` is stored RELATIVE to the document that names it, so a project
		// folder can be moved or shared whole. Resolution is the app's job — flow core does no file
		// I/O — and the CANONICAL key it returns is what the recursion guard compares, so two
		// spellings of one file (./sub.json vs sub.json) must collapse to the same key.
		return [documentDir](const std::string& source) -> std::optional<flow::serialize::ResolvedTemplate>
		{
			const std::filesystem::path full = documentDir.empty() ? std::filesystem::path(source) : documentDir / source;
			const std::string key = templateKey(full);

			const auto document = io::data::load(key);
			if (!document)
				return std::nullopt; // io::data::load logged it; serialize turns this into an issue
			return flow::serialize::ResolvedTemplate{key, *document};
		};
	}

	flow::serialize::LoadResult loadGraph(const std::string& uri, const core::Factory<flow::Node>& factory,
										  flow::serialize::TemplateCache* cache)
	{
		const auto document = io::data::load(uri);
		if (!document)
		{
			flow::serialize::LoadResult result; // io::data::load logged the read failure
			result.issues.push_back({flow::serialize::Severity::Error, "could not read graph file: " + uri});
			return result;
		}
		// Linked groups resolve against the folder holding THIS document.
		return flow::serialize::fromValue(*document, factory, sceneCodecs(),
										  templateResolver(std::filesystem::path(uri).parent_path()), cache);
	}

	data::Value snapshotGraph(const flow::Graph& graph, const core::Factory<flow::Node>& factory,
							  const flow::serialize::EditorTree& editor)
	{
		return flow::serialize::toValue(graph, factory, sceneCodecs(), editor);
	}

	flow::serialize::LoadResult restoreGraph(const data::Value& document, const core::Factory<flow::Node>& factory,
											 const std::filesystem::path& documentDir, flow::serialize::TemplateCache* cache)
	{
		// Same resolver a file load uses (see restoreGraph's header note): a snapshot stores a linked
		// group as source + interface cache, exactly as the file does, so restoring one has to follow
		// the link the same way or the group comes back as an empty placeholder.
		return flow::serialize::fromValue(document, factory, sceneCodecs(), templateResolver(documentDir), cache);
	}
} // namespace flowview
