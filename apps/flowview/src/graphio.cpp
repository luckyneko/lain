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

namespace flowview
{
	using namespace lain;

	flow::serialize::ValueCodecs sceneCodecs()
	{
		flow::serialize::ValueCodecs codecs;
		codecs.registerType<int>("int");
		codecs.registerType<float>("float");
		codecs.registerType<std::filesystem::path>("path");
		codecs.registerType<image::ColorRGBf>("color");
		return codecs;
	}

	void registerSceneSerialization()
	{
		flow::registerPortType<image::Image>("Image"); // boundary pins (image::Image) replay through this
		io::data::registerDataCodecs();				   // json (Value <-> bytes)
	}

	bool saveGraph(const std::string& uri, const flow::Graph& graph,
				   const core::Factory<flow::Node>& factory, const flow::serialize::EditorData& editor)
	{
		const data::Value document = flow::serialize::toValue(graph, factory, sceneCodecs(), editor);
		return io::data::save(uri, document);
	}

	flow::serialize::LoadResult loadGraph(const std::string& uri, const core::Factory<flow::Node>& factory)
	{
		const auto document = io::data::load(uri);
		if (!document)
		{
			flow::serialize::LoadResult result; // io::data::load logged the read failure
			result.issues.push_back({flow::serialize::Severity::Error, "could not read graph file: " + uri});
			return result;
		}
		return flow::serialize::fromValue(*document, factory, sceneCodecs());
	}
} // namespace flowview
