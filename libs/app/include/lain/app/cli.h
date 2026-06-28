#pragma once

// Re-expose CLI11 under a lain face — like lain::math over glm, or lain::gui over
// ImGui — rather than hand-wrapping its rich API (which would only grow). So
// lain::app::cli *is* CLI11: a delegate registers options with CLI11's own API
// (add_flag / add_option / ...) on the cli::App handed to onInit. CLI11's header is
// pulled in here and therefore reaches a delegate's translation unit — accepted, the
// same trade as ImGui reaching a gui consumer.

#include <CLI/CLI.hpp>

namespace lain::app
{
	namespace cli = ::CLI;
}
