#include "graphio.h"

#include <lain/data/data.h>
#include <lain/flow/example/comparenode.h> // Comparison — CompareNode's operator param
#include <lain/flow/graph.h>
#include <lain/flow/porttyperegistry.h>
#include <lain/flow/serialize/serialize.h>
#include <lain/image/color.h>
#include <lain/image/colorspace.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/io/data/codecs.h> // registerDataCodecs
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/io/uri.h>
#include <lain/media/frameposition.h>
#include <lain/media/frameref.h>
#include <lain/media/framesequence.h>
#include <lain/string/format.h> // format — a float's text form

#include <exception>
#include <filesystem>
#include <optional>
#include <string>

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
		// Convert's three enum params. data reflects an enum as its NAME through meta::enums, so
		// these are ordinary codec registrations rather than a new mechanism — and a name survives
		// an enumerator being inserted, which an ordinal would not.
		codecs.registerType<image::PixelFormat>("pixelFormat");
		codecs.registerType<image::ColorSpace>("colorSpace");
		// Compare's operator (M11) — the same enum-as-name mechanism, so a document says
		// "Greater" rather than an ordinal that an inserted enumerator would silently re-point.
		codecs.registerType<flow::example::Comparison>("comparison");
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

		registerSceneConversions();
		io::data::registerDataCodecs(); // json (Value <-> bytes)
	}

	// Parse a whole `text` as T, or nothing. The STRICT parse clibinders.cpp already applies to a
	// cli value: trailing junk ("12x") is not a number, because a graph quietly reading 12 out of a
	// typo is worse than a Cast that produces nothing and says so. One policy, two callers.
	template <typename T, typename Parse>
	static std::optional<T> parseWhole(const std::string& text, Parse parse)
	{
		try
		{
			std::size_t used = 0;
			const T value = parse(text, &used);
			if (used != text.size())
				return std::nullopt;
			return value;
		}
		catch (const std::exception&)
		{
			return std::nullopt; // not a number at all, or out of range
		}
	}

	void registerSceneConversions()
	{
		// What a CAST node may do (ADR-0022). Each entry is a POLICY, which is why they are spelled
		// out per pair rather than offered for every pair the language happens to convert: `float ->
		// int` truncates toward zero, and that is a decision, not a fact about the types.
		//
		// Nothing else reads this registry. `connect` type-checks exactly — a conversion is
		// something a graph asks for by containing a node that does it, never something an edge
		// performs on its behalf (ADR-0020's "no silent lossy conversion", arriving from the other
		// side).

		// The arithmetic pair, and the one that unblocked a loop: an `index` is an Int and every
		// numeric setting downstream is a Float.
		flow::registerConversion<int, float>();
		// TRUNCATES toward zero (2.9 -> 2), which is C++'s own conversion and what a node called
		// Cast should mean. Rounding is a different operation and belongs to a numeric node that
		// says so, not to a param here whose meaning would exist for only some pairs.
		flow::registerConversion<float, int>();

		// To text: total, and useful for a label or a filename.
		flow::registerConversion<int, std::string>(+[](const int& v) -> std::string
												   { return std::to_string(v); });
		flow::registerConversion<float, std::string>(+[](const float& v) -> std::string
													 { return lain::string::format("{}", v); });
		flow::registerConversion<bool, std::string>(+[](const bool& v) -> std::string
													{ return v ? "true" : "false"; });

		// From text: FALLIBLE — an unparseable string converts to nothing, which suppresses the Cast
		// exactly as any node that produced no value does (ADR-0007).
		flow::registerConversion<std::string, int>(+[](const std::string& v) -> std::optional<int>
												   { return parseWhole<int>(v, [](const std::string& t, std::size_t* used)
																			{ return std::stoi(t, used); }); });
		flow::registerConversion<std::string, float>(+[](const std::string& v) -> std::optional<float>
													 { return parseWhole<float>(v, [](const std::string& t, std::size_t* used)
																				{ return std::stof(t, used); }); });

		// A path IS text, in both directions and without loss — the one pair here that cannot fail.
		flow::registerConversion<std::filesystem::path, std::string>(
			+[](const std::filesystem::path& v) -> std::string
			{ return v.string(); });
		flow::registerConversion<std::string, std::filesystem::path>(
			+[](const std::string& v) -> std::filesystem::path
			{ return std::filesystem::path{v}; });

		// Deliberately absent: bool <-> int. What `2 -> true` should mean is a decision nothing is
		// asking for, and an absent conversion is a menu entry that never appears rather than a
		// wrong answer nobody notices.
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
