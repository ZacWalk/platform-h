// theme.h — the platform-ui colour table.
//
// Phase 2 replaces these constants with an injectable pf::ui::theme; until then they
// carry the exact values rethinkify-app and equity-app both shipped.

#pragma once

#include "platform.h"

namespace pf::ui::colors
{
	constexpr pf::color_t handle_color{0x44, 0x44, 0x44};
	constexpr pf::color_t focus_handle_color{40, 100, 220};
	constexpr pf::color_t main_wnd_clr{0x22, 0x22, 0x22};
	constexpr pf::color_t tool_wnd_clr{0x33, 0x33, 0x33};
	constexpr pf::color_t handle_hover_color{0x66, 0x66, 0x66};
	constexpr pf::color_t handle_tracking_color{0x11, 0x66, 0xCC};
	constexpr pf::color_t text_color{0xFF, 0xFF, 0xFF};
	constexpr pf::color_t folder_text_color{0xEE, 0xCC, 0x22};
	constexpr pf::color_t darker_text_color{0xCC, 0xCC, 0xCC};
	constexpr pf::color_t line_color{0x22, 0x55, 0xAA};
	constexpr pf::color_t window_background{33, 33, 33};
}

