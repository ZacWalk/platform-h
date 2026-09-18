// Unit tests for platform-ui: the parts of the presentation layer that can be
// checked without a window. The widgets keep their state in plain structs and the
// table helpers are pure, so both are exercised directly.

#include "platform.h"
#include "ui/ui.h"
#include "ui/test_support.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	int g_checks = 0;
	int g_failures = 0;

	void check(const bool ok, const char* expr, const int line)
	{
		++g_checks;
		if (!ok)
		{
			++g_failures;
			std::printf("FAIL line %d: %s\n", line, expr);
		}
	}

	template <typename T, typename U>
	void check_eq(const T& actual, const U& expected, const char* expr, const int line)
	{
		++g_checks;
		if (!(actual == expected))
		{
			++g_failures;
			std::printf("FAIL line %d: %s\n", line, expr);
		}
	}

	void check_eq_str(const std::string_view actual, const std::string_view expected,
	                  const char* expr, const int line)
	{
		++g_checks;
		if (actual != expected)
		{
			++g_failures;
			std::printf("FAIL line %d: %s\n  expected: '%.*s'\n  actual:   '%.*s'\n",
			            line, expr,
			            static_cast<int>(expected.size()), expected.data(),
			            static_cast<int>(actual.size()), actual.data());
		}
	}

#define CHECK(expr)     check((expr), #expr, __LINE__)
#define CHECK_EQ(a, b)  check_eq((a), (b), #a " == " #b, __LINE__)
#define CHECK_STR(a, b) check_eq_str((a), (b), #a " == " #b, __LINE__)

	// "aé€" — one byte, two bytes, three bytes. A byte-oriented caret corrupts this.
	constexpr std::string_view mixed = "a\xC3\xA9\xE2\x82\xAC";

	void test_edit_box_utf8()
	{
		pf::ui::edit_box box;
		box.text = std::string(mixed);

		// Stepping back from the end must cross whole code points, not bytes.
		box.cursor_pos = 6;
		box.sel_anchor = 6;
		CHECK(box.delete_back());
		CHECK_STR(box.text, "a\xC3\xA9");
		CHECK_EQ(box.cursor_pos, 3);

		CHECK(box.delete_back());
		CHECK_STR(box.text, "a");
		CHECK_EQ(box.cursor_pos, 1);

		// Forward deletion from the front removes exactly one code point.
		box.text = std::string(mixed);
		box.cursor_pos = 0;
		box.sel_anchor = 0;
		CHECK(box.delete_forward());
		CHECK_STR(box.text, "\xC3\xA9\xE2\x82\xAC");
		CHECK(box.delete_forward());
		CHECK_STR(box.text, "\xE2\x82\xAC");

		// A non-ASCII character inserts all of its bytes and leaves the caret after them.
		box.text.clear();
		box.cursor_pos = 0;
		box.sel_anchor = 0;
		CHECK(box.on_char(U'\u20ac'));
		CHECK_STR(box.text, "\xE2\x82\xAC");
		CHECK_EQ(box.cursor_pos, 3);

		// Control characters are not text.
		CHECK(!box.on_char(U'\t'));
		CHECK_STR(box.text, "\xE2\x82\xAC");
	}

	void test_edit_box_selection()
	{
		pf::ui::edit_box box;
		box.text = "hello world";

		box.select_all();
		CHECK(box.has_selection());
		CHECK_EQ(box.sel_start(), 0);
		CHECK_EQ(box.sel_end(), 11);
		CHECK_STR(box.selected_text(), "hello world");

		// A selection is replaced by what is typed over it.
		box.cursor_pos = 0;
		box.sel_anchor = 5;
		CHECK_STR(box.selected_text(), "hello");
		box.insert_at_cursor("goodbye");
		CHECK_STR(box.text, "goodbye world");
		CHECK(!box.has_selection());

		// Backspace with a selection removes the selection, not a character.
		box.cursor_pos = 0;
		box.sel_anchor = 7;
		CHECK(box.delete_back());
		CHECK_STR(box.text, " world");
		CHECK_EQ(box.cursor_pos, 0);

		// An empty selection reports nothing selected.
		box.sel_anchor = box.cursor_pos;
		CHECK(!box.has_selection());
		CHECK_STR(box.selected_text(), "");
	}

	void test_edit_box_clipboard()
	{
		pf::ui::edit_box box;
		box.text = "copy me";
		box.select_all();

		// The clipboard is a shared machine resource; if it cannot be opened the
		// rest of the round trip proves nothing, so report and move on.
		if (!box.copy_to_clipboard())
		{
			std::printf("clipboard unavailable: skipping round-trip check\n");
			return;
		}

		pf::ui::edit_box dest;
		CHECK(dest.paste_from_clipboard());
		CHECK_STR(dest.text, "copy me");

		// A single-line field cannot represent newlines, so paste strips them.
		CHECK(pf::platform_text_to_clipboard("one\r\ntwo"));
		pf::ui::edit_box lines;
		CHECK(lines.paste_from_clipboard());
		CHECK_STR(lines.text, "onetwo");

		// Cut leaves the field empty and the text on the clipboard.
		box.text = "cut me";
		box.select_all();
		CHECK(box.cut_to_clipboard());
		CHECK_STR(box.text, "");
		pf::ui::edit_box after_cut;
		CHECK(after_cut.paste_from_clipboard());
		CHECK_STR(after_cut.text, "cut me");
	}

	void test_table_cells()
	{
		namespace tl = pf::ui::table_layout;

		CHECK_STR(tl::trim_cell("  abc  "), "abc");
		CHECK_STR(tl::trim_cell("abc"), "abc");
		CHECK_STR(tl::trim_cell("   "), "");
		CHECK_STR(tl::trim_cell(""), "");

		std::vector<std::string_view> cells;

		tl::split_pipe_cells(cells, "| a | b |");
		CHECK_EQ(cells.size(), 2u);
		CHECK_STR(tl::trim_cell(cells[0]), "a");
		CHECK_STR(tl::trim_cell(cells[1]), "b");

		tl::split_pipe_cells(cells, "a | b | c");
		CHECK_EQ(cells.size(), 3u);
		CHECK_STR(tl::trim_cell(cells[2]), "c");

		tl::split_csv_cells(cells, "a,b,c");
		CHECK_EQ(cells.size(), 3u);
		CHECK_STR(cells[1], "b");

		// A quoted field keeps its comma.
		tl::split_csv_cells(cells, "\"x,y\",z");
		CHECK_EQ(cells.size(), 2u);
		CHECK_STR(cells[0], "x,y");
		CHECK_STR(cells[1], "z");

		tl::split_csv_cells(cells, "");
		CHECK_EQ(cells.size(), 0u);
	}

	void test_table_alignment()
	{
		namespace tl = pf::ui::table_layout;

		CHECK(tl::is_right_align_cell("42"));
		CHECK(tl::is_right_align_cell("1,234.56"));
		CHECK(tl::is_right_align_cell("-5"));
		CHECK(tl::is_right_align_cell("+5"));
		CHECK(tl::is_right_align_cell("$42"));
		CHECK(tl::is_right_align_cell("12%"));
		CHECK(tl::is_right_align_cell("\xE2\x82\xAC""99"));   // EUR
		CHECK(tl::is_right_align_cell("\xC2\xA3""99"));       // GBP

		CHECK(!tl::is_right_align_cell("abc"));
		CHECK(!tl::is_right_align_cell(""));
		CHECK(!tl::is_right_align_cell("   "));
		CHECK(!tl::is_right_align_cell("12a"));
		// A percent sign only counts at the end.
		CHECK(!tl::is_right_align_cell("1%2"));
	}

	void test_table_widths()
	{
		namespace tl = pf::ui::table_layout;

		// Two columns needing 30 cells fit inside 40 with 7 of overhead: untouched.
		std::vector<int> widths{10, 20};
		tl::cap_col_widths(widths, 40);
		CHECK_EQ(widths[0], 10);
		CHECK_EQ(widths[1], 20);

		// The same columns in 20 cells shrink proportionally.
		widths = {10, 20};
		tl::cap_col_widths(widths, 20);
		CHECK(widths[0] < 10);
		CHECK(widths[1] < 20);
		CHECK(widths[0] >= 1);
		CHECK(widths[1] >= 1);
		CHECK(widths[0] < widths[1]);

		// No column is ever capped out of existence.
		widths = {50, 50, 50};
		tl::cap_col_widths(widths, 4);
		for (const auto w : widths)
			CHECK(w >= 1);

		// An empty table is not a special case at the call site.
		widths.clear();
		tl::cap_col_widths(widths, 40);
		CHECK(widths.empty());
	}

	void test_splitter_geometry()
	{
		pf::ui::splitter s(pf::ui::splitter::orientation::vertical, 0.5);
		const pf::irect bounds{0, 0, 200, 100};

		CHECK_EQ(s.split_pos(bounds), 100);
		CHECK(s.hit_test(bounds, pf::ipoint{100, 50}));
		CHECK(!s.hit_test(bounds, pf::ipoint{10, 50}));

		// Dragging past the edge clamps rather than collapsing the pane.
		s.update_ratio(bounds, pf::ipoint{-500, 50});
		CHECK(s._ratio >= pf::ui::splitter::min_ratio);
		s.update_ratio(bounds, pf::ipoint{5000, 50});
		CHECK(s._ratio <= pf::ui::splitter::max_ratio);

		// DPI scaling widens the grab handle.
		const auto narrow = s.bar_width();
		s.set_dpi_scale(2.0);
		CHECK(s.bar_width() > narrow);
	}

	void test_scrollbar_thumb()
	{
		pf::ui::custom_scrollbar bar(pf::ui::custom_scrollbar::orientation::vertical);

		// Nothing to scroll when the content fits.
		bar.update(100, 100, 0);
		CHECK(!bar.can_scroll());

		bar.update(1000, 100, 0);
		CHECK(bar.can_scroll());

		const auto top = bar.thumb(0, 200);
		CHECK_EQ(top.start, 0);
		CHECK(top.length > 0);

		// The thumb has a minimum length, so the last page must still not run past
		// the end of the track — this is the clamp the header calls out.
		bar.update(100000, 100, 100000 - 100);
		const auto bottom = bar.thumb(0, 200);
		CHECK(bottom.start + bottom.length <= 200);
		CHECK(bottom.length >= bar.thumb_thickness());
	}

	void test_text_location()
	{
		using pf::ui::text_location;
		using pf::ui::text_selection;

		// Ordering is by line first, then by byte offset within the line.
		CHECK(text_location(0, 1) < text_location(5, 2));
		CHECK(text_location(2, 3) < text_location(7, 3));
		CHECK(text_location(4, 4) == text_location(4, 4));
		CHECK(!(text_location(9, 1) < text_location(2, 1)));

		// A backwards drag is still a valid selection; normalize orders it.
		const text_selection backwards({8, 5}, {2, 1});
		CHECK(!backwards.empty());
		const auto forwards = backwards.normalize();
		CHECK(forwards._start == text_location(2, 1));
		CHECK(forwards._end == text_location(8, 5));

		// Normalizing an ordered selection leaves it alone.
		const text_selection ordered({1, 1}, {3, 3});
		CHECK(ordered.normalize() == ordered);

		// A caret is an empty selection at one point.
		const text_selection caret(text_location{4, 2});
		CHECK(caret.empty());
		CHECK(caret.is_valid());

		// Negative coordinates mark "no selection" rather than a position.
		CHECK(!text_selection(-1, -1, -1, -1).is_valid());
	}

	void test_theme()
	{
		const pf::ui::theme t;
		using ts = pf::ui::text_style;

		// Styles are distinguished, not collapsed onto one foreground colour.
		CHECK(!(t.style_color(ts::code_keyword) == t.style_color(ts::code_comment)));
		CHECK(!(t.style_color(ts::md_heading1) == t.style_color(ts::md_heading2)));

		// The chrome defaults are the palette both applications shipped.
		CHECK(t.window_background == pf::ui::colors::window_background);
		CHECK(t.handle == pf::ui::colors::handle_color);
		CHECK(t.style_color(ts::main_wnd_clr) == pf::ui::colors::main_wnd_clr);

		// An application repalettes by overriding one function.
		struct light_theme : pf::ui::theme
		{
			pf::color_t style_color(const ts s) const override
			{
				return s == ts::normal_text ? pf::color_t(0, 0, 0) : pf::ui::theme::style_color(s);
			}
		};

		const light_theme light;
		CHECK(light.style_color(ts::normal_text) == pf::color_t(0, 0, 0));
		// Everything it does not override still comes from the base.
		CHECK(light.style_color(ts::code_keyword) == t.style_color(ts::code_keyword));
	}

	void test_headless_measure()
	{
		const pf::ui::test::fake_measure_context measure;
		const pf::font any{12, pf::font_name::consolas};

		CHECK_EQ(measure.measure_char(any).cx, 8);
		CHECK_EQ(measure.measure_text("abcd", any).cx, 32);

		// Measurement counts code points, not bytes, so "aé€" is three cells wide
		// rather than six. A wrap test must not change meaning with the encoding.
		CHECK_EQ(measure.measure_text("a\xC3\xA9\xE2\x82\xAC", any).cx, 24);
		CHECK_EQ(measure.measure_text("", any).cx, 0);
	}

	void test_headless_draw()
	{
		pf::ui::test::fake_draw_context draw;
		const pf::font any{12, pf::font_name::consolas};

		draw.fill_solid_rect(pf::irect(0, 0, 10, 10), pf::color_t(1, 2, 3));
		draw.draw_text(4, 8, pf::irect(4, 8, 36, 24), "hello", any,
		               pf::color_t(255, 255, 255), pf::color_t(0, 0, 0));
		draw.draw_text(4, 24, pf::irect(4, 24, 36, 40), " world", any,
		               pf::color_t(255, 255, 255), pf::color_t(0, 0, 0));

		CHECK_EQ(draw.fills.size(), 1u);
		CHECK_EQ(draw.texts.size(), 2u);
		CHECK_EQ(draw.texts[0].x, 4);
		CHECK_EQ(draw.texts[1].y, 24);
		CHECK_STR(draw.drawn_text(), "hello world");

		// The x/y/cx/cy overload records the same rectangle as the irect one.
		draw.reset();
		draw.fill_solid_rect(2, 3, 5, 7, pf::color_t(9, 9, 9));
		CHECK_EQ(draw.fills[0].rect.left, 2);
		CHECK_EQ(draw.fills[0].rect.right, 7);
		CHECK_EQ(draw.fills[0].rect.bottom, 10);
		CHECK_EQ(draw.drawn_text().size(), 0u);
	}

	void test_recording_host()
	{
		pf::ui::test::recording_view_host host;

		host.lines_changed(3, 5);
		host.invalidate_lines(1, 2);
		host.line_count_changed(7, -2);
		host.ensure_visible(pf::ui::text_location{4, 9});
		host.invalidate_view();

		CHECK_EQ(host.changed.size(), 1u);
		CHECK_EQ(host.changed[0].start, 3);
		CHECK_EQ(host.invalidated.size(), 1u);
		CHECK_EQ(host.count_changes.size(), 1u);
		CHECK_EQ(host.count_changes[0].second, -2);
		CHECK_EQ(host.ensured.size(), 1u);
		CHECK(host.ensured[0] == pf::ui::text_location(4, 9));
		CHECK_EQ(host.invalidate_view_count, 1);

		host.reset();
		CHECK_EQ(host.changed.size(), 0u);
		CHECK_EQ(host.invalidate_view_count, 0);
	}
}

// The backend's WinMain references these; a console test never calls them.
app_init_result app_init(const pf::window_frame_ptr&, std::span<const std::string_view>)
{
	return {false, 0};
}

void app_idle()
{
}

void app_destroy()
{
}

int main()
{
	test_edit_box_utf8();
	test_edit_box_selection();
	test_edit_box_clipboard();
	test_table_cells();
	test_table_alignment();
	test_table_widths();
	test_splitter_geometry();
	test_scrollbar_thumb();
	test_text_location();
	test_theme();
	test_headless_measure();
	test_headless_draw();
	test_recording_host();

	std::printf("platform-ui tests: %s (%d checks, %d failures)\n",
	            g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
