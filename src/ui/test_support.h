// test_support.h — headless fakes for the drawing surface.
//
// Layout, word wrap and hit testing are the parts of a view most worth testing and
// the parts hardest to reach through a window. Both pf::measure_context and
// pf::draw_context are interfaces, so a test can supply its own and drive a view
// with no window, no message loop and no GDI.
//
// The metrics here are deliberately a fixed grid rather than a real font: a test
// asserting "this wraps after 40 columns" should not change meaning because a font
// was hinted differently. Width is measured in code points, so multi-byte text
// measures the same as ASCII of the same length — which is what a monospaced text
// view assumes anyway.
//
// This header is part of platform-ui so applications can use it for their own view
// tests, not only platform-h's.

#pragma once

#include "platform.h"

#include <vector>

namespace pf::ui::test
{
	// A fixed character cell, so every measurement is exact and reproducible.
	struct grid_metrics
	{
		int char_width = 8;
		int char_height = 16;

		[[nodiscard]] isize measure(const std::string_view text) const
		{
			return {static_cast<int>(pf::utf8_codepoint_count(text)) * char_width, char_height};
		}
	};

	class fake_measure_context : public pf::measure_context
	{
	public:
		grid_metrics metrics;

		fake_measure_context() = default;

		explicit fake_measure_context(const grid_metrics m) : metrics(m)
		{
		}

		isize measure_text(const std::string_view text, const pf::font&) const override
		{
			return metrics.measure(text);
		}

		isize measure_char(const pf::font&) const override
		{
			return {metrics.char_width, metrics.char_height};
		}
	};

	// Records what was drawn instead of drawing it, so a test can assert on the
	// output of a paint rather than only on the state that produced it.
	class fake_draw_context : public pf::draw_context
	{
	public:
		struct text_draw
		{
			int x = 0;
			int y = 0;
			std::string text;
			pf::color_t color;
			pf::color_t background;
		};

		struct rect_fill
		{
			pf::irect rect;
			pf::color_t color;
		};

		grid_metrics metrics;
		std::vector<text_draw> texts;
		std::vector<rect_fill> fills;

		fake_draw_context() : _clip(0, 0, 1024, 768)
		{
		}

		explicit fake_draw_context(const pf::irect& clip) : _clip(clip)
		{
		}

		void reset()
		{
			texts.clear();
			fills.clear();
		}

		// Everything drawn, concatenated in call order. Convenient for asserting
		// that a view put the right words on screen without caring where.
		[[nodiscard]] std::string drawn_text() const
		{
			std::string all;
			for (const auto& t : texts) all += t.text;
			return all;
		}

		irect clip_rect() const override { return _clip; }

		void fill_solid_rect(const irect& rc, const color_t color) override
		{
			fills.push_back({rc, color});
		}

		void fill_solid_rect(const int x, const int y, const int cx, const int cy, const color_t color) override
		{
			fills.push_back({pf::irect(x, y, x + cx, y + cy), color});
		}

		void draw_text(const int x, const int y, const irect&, const std::string_view text,
		               const pf::font&, const color_t text_color, const color_t bg_color) override
		{
			texts.push_back({x, y, std::string(text), text_color, bg_color});
		}

		isize measure_text(const std::string_view text, const pf::font&) const override
		{
			return metrics.measure(text);
		}

		void draw_text_h(const int x, const int y, const std::string_view text,
		                 const pf::font_handle, const color_t text_color) override
		{
			texts.push_back({x, y, std::string(text), text_color, {}});
		}

		isize measure_text_h(const std::string_view text, const pf::font_handle) const override
		{
			return metrics.measure(text);
		}

		void draw_lines(std::span<const ipoint>, color_t) override
		{
		}

		void draw_solid_line(ipoint, ipoint, color_t, int) override
		{
		}

		void draw_ellipse(int, int, int, int, color_t, int) override
		{
		}

		void fill_ellipse(int, int, int, int, color_t) override
		{
		}

		void set_clip_rect(const irect&) override
		{
		}

		void clear_clip_rect() override
		{
		}

		void draw_bitmap(int, int, const struct bitmap&) override
		{
		}

		void draw_bitmap(const irect&, const struct bitmap&) override
		{
		}

	private:
		pf::irect _clip;
	};

	// Records the requests a view makes of its host, so a test can assert that an
	// edit asked for the right repaint without owning a window.
	class recording_view_host : public view_host
	{
	public:
		struct line_range
		{
			int start = 0;
			int end = 0;
		};

		std::vector<line_range> invalidated;
		std::vector<line_range> changed;
		std::vector<std::pair<int, int>> count_changes;
		std::vector<text_location> ensured;
		int invalidate_view_count = 0;
		int invalidate_layout_count = 0;
		int invalidate_caret_count = 0;
		int invalidate_scrollbar_count = 0;
		int invalidate_status_count = 0;

		void reset()
		{
			invalidated.clear();
			changed.clear();
			count_changes.clear();
			ensured.clear();
			invalidate_view_count = 0;
			invalidate_layout_count = 0;
			invalidate_caret_count = 0;
			invalidate_scrollbar_count = 0;
			invalidate_status_count = 0;
		}

		void invalidate_lines(const int start, const int end) override
		{
			invalidated.push_back({start, end});
		}

		void lines_changed(const int start, const int end) override
		{
			changed.push_back({start, end});
		}

		void line_count_changed(const int at, const int delta) override
		{
			count_changes.emplace_back(at, delta);
		}

		void ensure_visible(const text_location& pt) override
		{
			ensured.push_back(pt);
		}

		void invalidate_view() override
		{
			++invalidate_view_count;
		}

		void invalidate_layout() override
		{
			++invalidate_layout_count;
		}

		void invalidate_caret() override
		{
			++invalidate_caret_count;
		}

		void invalidate_scrollbar() override
		{
			++invalidate_scrollbar_count;
		}

		void invalidate_status() override
		{
			++invalidate_status_count;
		}
	};
}
