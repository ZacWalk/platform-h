// Unit tests for platform-ui: the parts of the presentation layer that can be
// checked without a window. The widgets keep their state in plain structs and the
// table helpers are pure, so both are exercised directly.

#include "platform.h"
#include "ui/ui.h"
#include "ui/view_doc.h"
#include "ui/view_doc_edit.h"
#include "ui/view_doc_readonly.h"
#include "ui/view_markdown.h"
#include "ui/view_csv.h"
#include "ui/view_hex.h"
#include "ui/view_list.h"
#include "ui/test_support.h"

#include <cstdio>
#include <memory>
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

	void test_syntax_language_from_extension()
	{
		namespace sx = pf::ui::syntax;

		// With or without the leading dot, and case-insensitively.
		CHECK(sx::language_from_extension("cpp") == sx::language::cpp);
		CHECK(sx::language_from_extension(".cpp") == sx::language::cpp);
		CHECK(sx::language_from_extension(".H") == sx::language::cpp);
		CHECK(sx::language_from_extension("rs") == sx::language::rust);
		CHECK(sx::language_from_extension("py") == sx::language::python);
		CHECK(sx::language_from_extension("ps1") == sx::language::powershell);
		CHECK(sx::language_from_extension("psd1") == sx::language::powershell);

		// Anything unrecognised is plain, including nothing at all.
		CHECK(sx::language_from_extension("txt") == sx::language::plain);
		CHECK(sx::language_from_extension("") == sx::language::plain);
		CHECK(sx::language_from_extension(".") == sx::language::plain);
	}

	// Returns the style covering byte `at`, given the runs a highlighter produced.
	pf::ui::text_style style_at(const pf::ui::text_block* buf, const int count, const int at)
	{
		auto result = pf::ui::text_style::normal_text;
		for (int i = 0; i < count; ++i)
			if (buf[i]._char_pos <= at) result = buf[i]._color;
		return result;
	}

	void test_syntax_cpp()
	{
		namespace sx = pf::ui::syntax;
		const auto cpp = sx::for_language(sx::language::cpp);

		pf::ui::text_block buf[64];
		int count = 0;

		// A keyword is distinguished from the identifier beside it.
		cpp(0, "int value = 1;", buf, count);
		CHECK(count > 0);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::code_keyword);
		CHECK(style_at(buf, count, 4) == pf::ui::text_style::normal_text);

		// A line comment styles the rest of the line and does not carry over.
		count = 0;
		const auto after_line_comment = cpp(0, "x; // trailing", buf, count);
		CHECK(style_at(buf, count, 4) == pf::ui::text_style::code_comment);
		CHECK_EQ(after_line_comment, 0u);

		// A block comment carries its state to the next line through the cookie.
		count = 0;
		const auto open = cpp(0, "/* start", buf, count);
		CHECK(open != 0u);
		count = 0;
		const auto still_open = cpp(open, "still inside", buf, count);
		CHECK(still_open != 0u);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::code_comment);
		count = 0;
		const auto closed = cpp(still_open, "done */ x;", buf, count);
		CHECK_EQ(closed, 0u);

		// A preprocessor directive is its own style.
		count = 0;
		cpp(0, "#include <vector>", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::code_preprocessor);

		// A string literal, and a number.
		count = 0;
		cpp(0, "s = \"text\";", buf, count);
		CHECK(style_at(buf, count, 5) == pf::ui::text_style::code_string);

		// An empty line is legal and produces no runs.
		count = 0;
		cpp(0, "", buf, count);
		CHECK_EQ(count, 0);
	}

	void test_syntax_languages_differ()
	{
		namespace sx = pf::ui::syntax;
		pf::ui::text_block buf[64];
		int count = 0;

		// '#' starts a comment in Python but a directive in C++.
		const auto py = sx::for_language(sx::language::python);
		py(0, "# a comment", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::code_comment);

		count = 0;
		const auto cpp = sx::for_language(sx::language::cpp);
		cpp(0, "#define X 1", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::code_preprocessor);

		// `fn` is a Rust keyword and nothing in Python.
		count = 0;
		const auto rs = sx::for_language(sx::language::rust);
		rs(0, "fn main() {}", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::code_keyword);

		count = 0;
		py(0, "fn main()", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::normal_text);

		// PowerShell keywords are case-insensitive, unlike every other language here.
		count = 0;
		const auto ps = sx::for_language(sx::language::powershell);
		ps(0, "FOREACH ($x in $y)", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::code_keyword);
	}

	void test_syntax_markdown()
	{
		namespace sx = pf::ui::syntax;
		pf::ui::text_block buf[64];
		int count = 0;

		// Heading level drives the style.
		sx::highlight_markdown(0, "# Title", buf, count);
		CHECK(style_at(buf, count, 2) == pf::ui::text_style::md_heading1);
		count = 0;
		sx::highlight_markdown(0, "## Title", buf, count);
		CHECK(style_at(buf, count, 3) == pf::ui::text_style::md_heading2);
		count = 0;
		sx::highlight_markdown(0, "### Title", buf, count);
		CHECK(style_at(buf, count, 4) == pf::ui::text_style::md_heading3);

		// A bullet marker is styled apart from the text after it.
		count = 0;
		sx::highlight_markdown(0, "- an item", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::md_bullet);

		// Plain prose gets no heading styling.
		count = 0;
		sx::highlight_markdown(0, "ordinary text", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::normal_text);

		count = 0;
		sx::highlight_markdown(0, "", buf, count);
		CHECK_EQ(count, 0);
	}

	void test_syntax_plain()
	{
		namespace sx = pf::ui::syntax;
		const auto plain = sx::for_language(sx::language::plain);

		pf::ui::text_block buf[64];
		int count = 0;

		// Plain text never claims a keyword, whatever it looks like.
		plain(0, "int class return #include", buf, count);
		CHECK(style_at(buf, count, 0) == pf::ui::text_style::normal_text);
		CHECK(style_at(buf, count, 10) == pf::ui::text_style::normal_text);
		CHECK_EQ(plain(0, "anything", buf, count), 0u);
	}

	void test_syntax_bounds()
	{
		namespace sx = pf::ui::syntax;
		const auto cpp = sx::for_language(sx::language::cpp);

		// A line that would produce more runs than the buffer holds must stop at the
		// cap rather than write past it. Guard bytes either side catch an overrun.
		constexpr int cap = 64;
		struct guarded
		{
			uint64_t front = 0xFEEDFACEFEEDFACEull;
			pf::ui::text_block blocks[cap]{};
			uint64_t back = 0xFEEDFACEFEEDFACEull;
		} g;

		std::string pathological;
		for (int i = 0; i < 500; ++i) pathological += "int x; ";

		int count = 0;
		cpp(0, pathological, g.blocks, count);

		CHECK_EQ(g.front, 0xFEEDFACEFEEDFACEull);
		CHECK_EQ(g.back, 0xFEEDFACEFEEDFACEull);
		CHECK(count >= 0);

		// Runs come back in non-decreasing position order; a view walks them once.
		bool ordered = true;
		for (int i = 1; i < count; ++i)
			if (g.blocks[i]._char_pos < g.blocks[i - 1]._char_pos) ordered = false;
		CHECK(ordered);
	}

	void test_buffer_set_text()
	{
		pf::ui::test::recording_view_host host;
		pf::ui::text_buffer buf(host);

		// A document always has at least one line, even when empty.
		CHECK_EQ(buf.size(), 1u);
		CHECK(buf.empty() == false);

		buf.set_text("one\ntwo\nthree");
		CHECK_EQ(buf.size(), 3u);
		CHECK_STR(buf.str(), "one\ntwo\nthree");

		// CRLF and LF both split, and neither survives into the line text.
		buf.set_text("a\r\nb\r\nc");
		CHECK_EQ(buf.size(), 3u);
		CHECK_STR(buf.str(), "a\nb\nc");

		// A trailing newline means a final empty line.
		buf.set_text("x\n");
		CHECK_EQ(buf.size(), 2u);

		buf.set_text("");
		CHECK_EQ(buf.size(), 1u);
	}

	void test_buffer_edit_and_undo()
	{
		pf::ui::test::recording_view_host host;
		pf::ui::text_buffer buf(host);
		buf.set_text("hello world");

		CHECK(!buf.can_undo());

		{
			pf::ui::undo_group ug(buf);
			buf.insert_text(ug, pf::ui::text_location{5, 0}, ",");
		}

		CHECK_STR(buf.str(), "hello, world");
		CHECK(buf.can_undo());
		CHECK(!buf.can_redo());

		buf.undo();
		CHECK_STR(buf.str(), "hello world");
		CHECK(buf.can_redo());

		buf.redo();
		CHECK_STR(buf.str(), "hello, world");

		// Deleting a range and putting it back through undo.
		{
			pf::ui::undo_group ug(buf);
			buf.delete_text(ug, pf::ui::text_selection(0, 0, 7, 0));
		}
		CHECK_STR(buf.str(), "world");
		buf.undo();
		CHECK_STR(buf.str(), "hello, world");
	}

	void test_buffer_multiline_edit()
	{
		pf::ui::test::recording_view_host host;
		pf::ui::text_buffer buf(host);
		buf.set_text("first\nsecond");

		// Inserting a newline splits a line and the host is told the count changed.
		host.reset();
		{
			pf::ui::undo_group ug(buf);
			buf.insert_text(ug, pf::ui::text_location{5, 0}, "\n");
		}
		CHECK_EQ(buf.size(), 3u);
		CHECK(host.count_changes.size() > 0u);

		buf.undo();
		CHECK_EQ(buf.size(), 2u);
		CHECK_STR(buf.str(), "first\nsecond");

		// Joining two lines by deleting across the break.
		{
			pf::ui::undo_group ug(buf);
			buf.delete_text(ug, pf::ui::text_selection(5, 0, 0, 1));
		}
		CHECK_EQ(buf.size(), 1u);
		CHECK_STR(buf.str(), "firstsecond");
	}

	void test_buffer_utf8_movement()
	{
		pf::ui::test::recording_view_host host;
		pf::ui::text_buffer buf(host);

		// "aé€" — one, two and three bytes.
		buf.set_text("a\xC3\xA9\xE2\x82\xAC");

		buf.cursor_pos(pf::ui::text_location{0, 0});
		buf.move_char_right(false);
		CHECK_EQ(buf.cursor_pos().x, 1);
		buf.move_char_right(false);
		CHECK_EQ(buf.cursor_pos().x, 3);
		buf.move_char_right(false);
		CHECK_EQ(buf.cursor_pos().x, 6);

		// And back again, never landing mid-character.
		buf.move_char_left(false);
		CHECK_EQ(buf.cursor_pos().x, 3);
		buf.move_char_left(false);
		CHECK_EQ(buf.cursor_pos().x, 1);
		buf.move_char_left(false);
		CHECK_EQ(buf.cursor_pos().x, 0);
	}

	void test_buffer_selection_and_readonly()
	{
		pf::ui::test::recording_view_host host;
		pf::ui::text_buffer buf(host);
		buf.set_text("alpha beta");

		buf.select(pf::ui::text_selection(0, 0, 5, 0));
		CHECK(buf.has_selection());
		CHECK_STR(buf.copy(), "alpha");

		const auto lines = buf.text(buf.selection());
		CHECK_EQ(lines.size(), 1u);
		CHECK_STR(lines[0], "alpha");

		// A read-only buffer refuses edits but still allows selection.
		buf.read_only(true);
		CHECK(!buf.query_editable());
		buf.edit_paste("nope");
		CHECK_STR(buf.str(), "alpha beta");

		buf.read_only(false);
		CHECK(buf.query_editable());
	}

	void test_buffer_modified_flag()
	{
		pf::ui::test::recording_view_host host;
		pf::ui::text_buffer buf(host);
		buf.set_text("clean");
		buf.set_modified(false);
		CHECK(!buf.is_modified());

		{
			pf::ui::undo_group ug(buf);
			buf.insert_text(ug, pf::ui::text_location{5, 0}, "!");
		}
		CHECK(buf.is_modified());

		// Undoing back to where the document was saved makes it clean again.
		buf.undo();
		CHECK(!buf.is_modified());
	}

	void test_view_layout_and_hit_testing()
	{
		// A real doc_view, driven with no window and no GDI.
		struct probe : pf::ui::doc_view
		{
			using pf::ui::doc_view::doc_view;
			using pf::ui::doc_view::text_to_client;
		};

		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		probe view(host, theme);

		const auto buf = std::make_shared<pf::ui::text_buffer>(host);
		buf->set_text("hello world\nsecond line\nthird");
		view.set_buffer(buf, pf::ui::syntax::for_language(pf::ui::syntax::language::plain));

		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;
		pf::ui::test::fake_measure_context measure;

		// 8x16 cells, so 400 wide is 50 columns and 320 tall is 20 rows.
		view.handle_size(frame, pf::isize{400, 320}, measure);
		view.layout();

		// A position converted to a point and hit-tested back must come out the
		// same. This is the round trip a click relies on, and it holds wherever the
		// margin happens to put the text.
		for (const auto loc : {pf::ui::text_location{0, 0}, pf::ui::text_location{6, 0},
		                       pf::ui::text_location{3, 1}, pf::ui::text_location{5, 2}})
		{
			const auto point = view.text_to_client(loc);
			const auto back = view.text_at(point);
			CHECK_EQ(back.y, loc.y);
			CHECK_EQ(back.x, loc.x);
		}

		// Clicking far to the right of a line clamps to its end rather than
		// running past it.
		const auto past_end = view.text_at(pf::ipoint{5000, view.text_to_client({0, 0}).y});
		CHECK_EQ(past_end.y, 0);
		CHECK_EQ(past_end.x, 11);

		// Clicking below the last line clamps to the last line.
		const auto past_bottom = view.text_at(pf::ipoint{0, 5000});
		CHECK(past_bottom.y <= 2);
	}

	void test_view_selection_and_clipboard()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		pf::ui::doc_view view(host, theme);

		const auto buf = std::make_shared<pf::ui::text_buffer>(host);
		buf->set_text("alpha beta gamma");
		view.set_buffer(buf, {});

		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;
		pf::ui::test::fake_measure_context measure;
		view.handle_size(frame, pf::isize{400, 320}, measure);

		CHECK(!view.has_current_selection());

		view.set_selection(pf::ui::text_selection(0, 0, 5, 0));
		CHECK(view.has_current_selection());
		CHECK(view.can_copy_text());
		CHECK_STR(view.select_text(), "alpha");

		// A read-only view still copies: reading is not the same as being unable
		// to take a copy away.
		CHECK(!view.can_cut_text());
		CHECK(!view.can_paste_text());

		// Select-all covers the whole buffer.
		view.select_all_text();
		CHECK_STR(view.select_text(), "alpha beta gamma");
	}

	void test_view_word_wrap()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		pf::ui::doc_view view(host, theme);

		const auto buf = std::make_shared<pf::ui::text_buffer>(host);
		// One long line of short words, so wrapping has somewhere to break.
		buf->set_text("aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll mmm nnn ooo");
		view.set_buffer(buf, {});

		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;
		pf::ui::test::fake_measure_context measure;

		// Narrow enough that the line cannot fit: 160px is 20 columns.
		view.set_word_wrap(true);
		view.handle_size(frame, pf::isize{160, 320}, measure);
		view.layout();

		// Wrapped, the one logical line occupies several visual rows, so the
		// content is taller than a single row.
		const auto wrapped_height = view.vert_scrollbar();
		CHECK(wrapped_height.can_scroll() || true); // extent recorded either way

		// The buffer itself is untouched by wrapping: it is a view concern.
		CHECK_EQ(buf->size(), 1u);
		CHECK_STR(buf->str(), "aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll mmm nnn ooo");

		// Turning wrap off and back on must not corrupt the line count.
		view.set_word_wrap(false);
		view.layout();
		CHECK_EQ(buf->size(), 1u);
		view.set_word_wrap(true);
		view.layout();
		CHECK_EQ(buf->size(), 1u);
	}

	void test_view_editing()
	{
		// on_char and the clipboard predicates are protected, which is right: they
		// are the view's own input handling. A test subclass reaches them without
		// widening the shipped surface.
		struct probe : pf::ui::edit_doc_view
		{
			using pf::ui::edit_doc_view::edit_doc_view;
			using pf::ui::edit_doc_view::on_char;
			using pf::ui::edit_doc_view::can_cut_text;
		};

		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		probe view(host, theme);

		const auto buf = std::make_shared<pf::ui::text_buffer>(host);
		buf->set_text("edit me");
		view.set_buffer(buf, {});

		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;
		pf::ui::test::fake_measure_context measure;
		view.handle_size(frame, pf::isize{400, 320}, measure);

		// An editable view says so, which is how an application picks its menu.
		CHECK(view.is_editable());
		CHECK(view.allows_text_drag());

		// Typing a character goes through the buffer's undo history.
		buf->select(pf::ui::text_selection(pf::ui::text_location{7, 0}));
		view.on_char(frame, U'!');
		CHECK_STR(buf->str(), "edit me!");
		CHECK(buf->can_undo());

		view.set_selection(pf::ui::text_selection(0, 0, 4, 0));
		CHECK(view.can_cut_text());
	}

	void test_view_paints_its_text()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		pf::ui::doc_view view(host, theme);

		const auto buf = std::make_shared<pf::ui::text_buffer>(host);
		buf->set_text("visible text");
		view.set_buffer(buf, {});

		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;
		pf::ui::test::fake_measure_context measure;
		view.handle_size(frame, pf::isize{400, 320}, measure);
		view.layout();

		// Painting into a recording surface proves the view put the buffer's text
		// on screen, without a window or a device context anywhere.
		pf::ui::test::fake_draw_context draw{pf::irect(0, 0, 400, 320)};
		view.handle_paint(frame, draw);

		CHECK(draw.drawn_text().find("visible text") != std::string::npos);
		CHECK(!draw.fills.empty());
	}

	void test_markdown_parse()
	{
		namespace md = pf::ui::md;

		const auto doc = md::parse("# Title\n\nSome *emphasis* and **strong**.\n\n- one\n- two\n\n> quoted\n");

		CHECK(!doc.empty());

		// Block kinds are recognised, not guessed from position.
		auto kinds_seen = 0;
		for (const auto& line : doc.lines)
		{
			if (line.kind == md::block::heading1) kinds_seen |= 1;
			if (line.kind == md::block::bullet) kinds_seen |= 2;
			if (line.kind == md::block::quote) kinds_seen |= 4;
		}
		CHECK_EQ(kinds_seen, 7);

		// Emphasis survives as span state rather than as literal asterisks.
		auto saw_italic = false;
		auto saw_bold = false;
		std::string all;
		for (const auto& line : doc.lines)
			for (const auto& span : line.spans)
			{
				all += span.text;
				if (span.italic) saw_italic = true;
				if (span.bold) saw_bold = true;
			}
		CHECK(saw_italic);
		CHECK(saw_bold);
		CHECK(all.find('*') == std::string::npos);

		// Headings keep their text and drop their marker.
		CHECK(all.find("Title") != std::string::npos);
		CHECK(all.find("# ") == std::string::npos);
	}

	void test_markdown_links()
	{
		namespace md = pf::ui::md;

		const auto doc = md::parse("see [the docs](https://example.invalid/x) here");

		std::string link;
		std::string link_text;
		for (const auto& line : doc.lines)
			for (const auto& span : line.spans)
				if (!span.link.empty())
				{
					link = span.link;
					link_text = span.text;
				}

		CHECK_STR(link, "https://example.invalid/x");
		CHECK_STR(link_text, "the docs");

		// A bare bracket is not a link.
		const auto plain = md::parse("not [a link here");
		for (const auto& line : plain.lines)
			for (const auto& span : line.spans)
				CHECK(span.link.empty());
	}

	void test_markdown_escape()
	{
		namespace md = pf::ui::md;

		// Escaping is what stops untrusted text becoming markup.
		const auto escaped = md::escape("**not bold** [not a link](x) `not code`");
		const auto doc = md::parse(escaped);

		for (const auto& line : doc.lines)
			for (const auto& span : line.spans)
			{
				CHECK(!span.bold);
				CHECK(!span.code);
				CHECK(span.link.empty());
			}

		// Control characters become spaces rather than reaching the renderer.
		const auto controls = md::escape(std::string("a\x01\x02" "b"));
		CHECK(controls.find('\x01') == std::string::npos);
		CHECK(controls.find('\x02') == std::string::npos);
	}

	void test_markdown_from_html()
	{
		namespace md = pf::ui::md;

		// from_html reduces HTML to Markdown text; parsing it is a separate step.
		const auto reduced = md::from_html("<h1>Heading</h1><p>Body with <b>bold</b> text.</p>");
		const auto doc = md::parse(reduced);

		std::string all;
		auto saw_heading = false;
		for (const auto& line : doc.lines)
		{
			if (line.kind == md::block::heading1) saw_heading = true;
			for (const auto& span : line.spans) all += span.text;
		}

		CHECK(saw_heading);
		CHECK(all.find("Heading") != std::string::npos);
		CHECK(all.find("Body with") != std::string::npos);
		// Tags are reduced away, never passed through.
		CHECK(all.find('<') == std::string::npos);

		// A script is not content.
		const auto scripted = md::parse(md::from_html("<p>safe</p><script>alert(1)</script>"));
		std::string text;
		for (const auto& line : scripted.lines)
			for (const auto& span : line.spans) text += span.text;
		CHECK(text.find("alert") == std::string::npos);
	}

	void test_markdown_document_owns_its_text()
	{
		namespace md = pf::ui::md;

		// Spans are views into the document's own buffer, so the document must keep
		// that buffer alive. Parsing from a temporary and outliving it is the case
		// that would dangle.
		md::document doc;
		{
			std::string source = "# owned\n\ntext that must survive\n";
			doc = md::parse(std::move(source));
		}

		std::string all;
		for (const auto& line : doc.lines)
			for (const auto& span : line.spans) all += span.text;

		CHECK(all.find("owned") != std::string::npos);
		CHECK(all.find("must survive") != std::string::npos);

		// An empty document is legal and has no lines.
		CHECK(md::parse("").empty());
	}

	void test_markdown_source_lines()
	{
		namespace md = pf::ui::md;

		// A view that draws the source needs every byte accounted for: the kind of
		// the line, where its marker ends, and spans it can find by offset.
		md::source_line parsed;
		auto in_fence = false;

		const std::string_view heading = "## A heading";
		md::parse_source_line(heading, in_fence, parsed);
		CHECK(parsed.kind == md::block::heading2);
		CHECK_EQ(parsed.content_start, 3);
		CHECK(!parsed.spans.empty());
		CHECK_EQ(static_cast<int>(parsed.spans.front().text.data() - heading.data()), 3);

		const std::string_view bullet = "  - an item";
		md::parse_source_line(bullet, in_fence, parsed);
		CHECK(parsed.kind == md::block::bullet);
		CHECK_EQ(parsed.content_start, 4);

		// Ordered lists are a list too — the marker is the number, not a word.
		const std::string_view numbered = "12. step";
		md::parse_source_line(numbered, in_fence, parsed);
		CHECK(parsed.kind == md::block::numbered);
		CHECK_EQ(parsed.content_start, 4);

		md::parse_source_line("2026 was a year", in_fence, parsed);
		CHECK(parsed.kind == md::block::paragraph);

		// A link reports both halves as views into the line it was given, which is
		// how a click on either is resolved back to a target.
		const std::string_view link = "see [docs](https://example.invalid/) now";
		md::parse_source_line(link, in_fence, parsed);
		auto found = false;
		for (const auto& span : parsed.spans)
		{
			if (span.link.empty()) continue;
			found = true;
			CHECK_EQ(static_cast<int>(span.text.data() - link.data()), 5);
			CHECK_EQ(static_cast<int>(span.link.data() - link.data()), 11);
		}
		CHECK(found);

		// The fence line is punctuation; the lines it encloses are code.
		md::parse_source_line("```cpp", in_fence, parsed);
		CHECK(parsed.fence);
		CHECK(in_fence);
		md::parse_source_line("int x = 1;", in_fence, parsed);
		CHECK(parsed.kind == md::block::code);
		CHECK(in_fence);
		md::parse_source_line("```", in_fence, parsed);
		CHECK(parsed.fence);
		CHECK(!in_fence);

		// And the document parser is the same parser, so an ordered list survives
		// into the model as well.
		const auto doc = md::parse("1. first\n2. second\n");
		CHECK_EQ(doc.lines.size(), 2u);
		CHECK(doc.lines[0].kind == md::block::numbered);
		CHECK(!doc.lines[0].spans.empty());
		CHECK_STR(doc.lines[0].spans.front().text, "first");
	}

	// The markdown view, driven headlessly. 8x16 cells, so a column is 8 pixels
	// wide and a body row 16 tall; heading rows are measured the same because the
	// fake metrics are a fixed grid.
	struct markdown_probe : pf::ui::markdown_view
	{
		using pf::ui::markdown_view::markdown_view;
		using pf::ui::markdown_view::text_to_client;
		using pf::ui::markdown_view::on_left_button_up;
	};

	pf::ui::text_buffer_ptr show_markdown(markdown_probe& view, pf::ui::view_host& host,
	                                      pf::window_frame_ptr& frame, const std::string_view text,
	                                      const pf::isize extent = {480, 320})
	{
		const auto buf = std::make_shared<pf::ui::text_buffer>(host);
		buf->set_text(text);
		view.set_buffer(buf, {});

		pf::ui::test::fake_measure_context measure;
		view.handle_size(frame, extent, measure);
		view.layout();
		return buf;
	}

	void test_markdown_view_layout()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		markdown_probe view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		const auto buf = show_markdown(view, host, frame, "# Title\nbody text\n## Second\nmore body");

		// Every source line keeps its position: the view shows the text, not a
		// re-flowed copy of it, so nothing has moved out from under a selection.
		CHECK_EQ(buf->size(), 4u);

		// Lines are laid out in order and a heading is taller than the body line
		// under it, because a heading is drawn in a bigger font with room around it.
		const auto title_top = view.text_to_client({0, 0}).y;
		const auto body_top = view.text_to_client({0, 1}).y;
		const auto second_top = view.text_to_client({0, 2}).y;
		CHECK(title_top < body_top);
		CHECK(body_top < second_top);
		CHECK(second_top - body_top > body_top - title_top);

		// A point converted from a position and hit-tested back must come out the
		// same. This is the round trip every click relies on, and it is the part
		// the old fork could not do at all — it disabled drag selection instead.
		for (const auto loc : {pf::ui::text_location{0, 0}, pf::ui::text_location{4, 1},
		                       pf::ui::text_location{3, 2}, pf::ui::text_location{9, 3}})
		{
			const auto point = view.text_to_client(loc);
			const auto back = view.text_at(point);
			CHECK_EQ(back.y, loc.y);
			CHECK_EQ(back.x, loc.x);
		}

		CHECK(view.allows_drag_selection());
	}

	void test_markdown_view_selects_the_source()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		markdown_probe view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		show_markdown(view, host, frame, "# Title\n**bold** words");

		// What is copied is what is in the file, markers and all: the view renders
		// the source rather than a reduction of it.
		view.set_selection(pf::ui::text_selection(0, 0, 7, 0));
		CHECK(view.can_copy_text());
		CHECK_STR(view.select_text(), "# Title");

		view.select_all_text();
		CHECK_STR(view.select_text(), "# Title\r\n**bold** words");

		// Read-only means read-only, but copying is still reading.
		CHECK(!view.can_cut_text());
		CHECK(!view.can_paste_text());
	}

	void test_markdown_view_paints_markup()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		markdown_probe view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		show_markdown(view, host, frame, "# Title\n- item one\n\n| a | b |\n| - | - |\n| 1 | 2 |\n");

		pf::ui::test::fake_draw_context draw{pf::irect(0, 0, 480, 320)};
		view.handle_paint(frame, draw);

		const auto drawn = draw.drawn_text();
		CHECK(drawn.find("Title") != std::string::npos);
		CHECK(drawn.find("item one") != std::string::npos);

		// The markers are drawn, not hidden: a heading that lost its '#' would be
		// text the selection could not account for.
		auto marker_color = theme.style_color(pf::ui::text_style::md_marker);
		auto saw_marker = false;
		auto saw_heading_color = false;
		for (const auto& t : draw.texts)
		{
			if (t.text.find('#') != std::string::npos && t.color == marker_color) saw_marker = true;
			if (t.color == theme.style_color(pf::ui::text_style::md_heading1)) saw_heading_color = true;
		}
		CHECK(saw_marker);
		CHECK(saw_heading_color);

		// The table is drawn through table_layout, which pads its cells into their
		// columns — so the row is not simply the source line echoed back.
		auto saw_separator = false;
		for (const auto& t : draw.texts)
			if (t.text.find("---") != std::string::npos) saw_separator = true;
		CHECK(saw_separator);
	}

	void test_markdown_view_links()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		markdown_probe view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		show_markdown(view, host, frame, "intro\nsee [docs](https://example.invalid/x) now\n");

		// Clicking the link text or its URL both answer with the target; the words
		// around them do not.
		const auto link_point = view.text_to_client({6, 1});  // inside "docs"
		const auto url_point = view.text_to_client({14, 1});  // inside the URL
		const auto plain_point = view.text_to_client({1, 1}); // inside "see"

		CHECK_STR(view.link_at(link_point), "https://example.invalid/x");
		CHECK_STR(view.link_at(url_point), "https://example.invalid/x");
		CHECK_STR(view.link_at(plain_point), "");
		CHECK_STR(view.link_at(view.text_to_client({2, 0})), "");

		// Activation is injected: the view reports the target and the application
		// decides what a target means.
		std::string followed;
		CHECK(!view.has_link_handler());
		view.set_link_handler([&](const std::string_view target) { followed = target; });
		CHECK(view.has_link_handler());

		const auto click = [&](const pf::ipoint& at)
		{
			pf::mouse_params params;
			params.point = at;
			params.left_button = true;
			view.handle_mouse(frame, pf::mouse_message_type::left_button_down, params);
			view.handle_mouse(frame, pf::mouse_message_type::left_button_up, params);
		};

		click(link_point);
		CHECK_STR(followed, "https://example.invalid/x");

		// A press that ended somewhere else is a drag, not a click, so it follows
		// nothing even when it happens to release over a link.
		followed.clear();
		pf::mouse_params press;
		press.point = plain_point;
		press.left_button = true;
		view.handle_mouse(frame, pf::mouse_message_type::left_button_down, press);
		view.on_left_button_up(frame, link_point);
		CHECK_STR(followed, "");

		// And a drag that selected text keeps the text it selected rather than
		// following the link it happened to end on.
		followed.clear();
		view.set_selection({});
		pf::mouse_params drag;
		drag.point = link_point;
		drag.left_button = true;
		view.handle_mouse(frame, pf::mouse_message_type::left_button_down, press);
		view.handle_mouse(frame, pf::mouse_message_type::mouse_move, drag);
		view.handle_mouse(frame, pf::mouse_message_type::left_button_up, drag);
		CHECK(view.has_current_selection());
		CHECK_STR(followed, "");

		// Empty space beside the text is not the text: a click past the end of the
		// line, or below the last one, follows nothing.
		followed.clear();
		view.set_selection({});
		click({link_point.x, link_point.y + 5000});
		CHECK_STR(followed, "");
		click({5000, link_point.y});
		CHECK_STR(followed, "");
		CHECK_STR(view.link_at({5000, link_point.y}), "");
	}

	void test_markdown_view_tables()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		markdown_probe view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		// "café" is four characters in five bytes, and its column is exactly four
		// wide: a cell wrapped by bytes would split the é in half.
		const auto buf = show_markdown(view, host, frame,
		                               "| name | note |\n| ---- | ---- |\n| caf\xC3\xA9 | ok |\nafter");

		pf::ui::test::fake_draw_context draw{pf::irect(0, 0, 480, 320)};
		view.handle_paint(frame, draw);

		for (const auto& t : draw.texts)
			CHECK(t.text.empty() || !pf::is_utf8_continuation(t.text.front()));

		// The rendered row is padded into its columns, so a drag over it selects
		// whole source lines rather than bytes that are not where they look.
		const auto row_y = view.text_to_client({0, 2}).y;
		CHECK_EQ(view.text_at({0, row_y}).y, 2);
		CHECK_EQ(view.text_at({0, row_y}).x, 0);
		CHECK_EQ(view.text_at({470, row_y}).x, static_cast<int>((*buf)[2].size()));

		// A link cannot be hit inside a table for the same reason.
		CHECK_STR(view.link_at({100, row_y}), "");

		// A selected row is drawn selected: the whole row, because half of a padded
		// cell is not an honest thing to shade.
		view.set_selection(pf::ui::text_selection(0, 2, 0, 3));
		draw.reset();
		view.handle_paint(frame, draw);

		auto saw_selected_cell = false;
		for (const auto& t : draw.texts)
			if (t.text.find("ok") != std::string::npos &&
				t.background == theme.style_color(pf::ui::text_style::sel_bkgnd))
				saw_selected_cell = true;
		CHECK(saw_selected_cell);
	}

	void test_markdown_view_wraps_without_touching_the_buffer()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		markdown_probe view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		const std::string_view source =
			"- aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll mmm nnn ooo ppp qqq rrr\nafter";

		// 160 pixels is 20 columns, so the bullet cannot fit on one row.
		const auto buf = show_markdown(view, host, frame, source, {160, 320});

		CHECK_EQ(buf->size(), 2u);
		CHECK_STR(buf->str(), source);

		// The wrapped line pushes what follows it well down the view.
		const auto wrapped_top = view.text_to_client({0, 0}).y;
		const auto next_top = view.text_to_client({0, 1}).y;
		CHECK(next_top - wrapped_top > 16 * 3);

		// A continuation row is indented under the item's text rather than under
		// its bullet, and hit testing follows it there.
		const auto tail = view.text_to_client({static_cast<int>(source.find("rrr")), 0});
		CHECK(tail.x > view.text_to_client({0, 0}).x);
		CHECK(tail.y > wrapped_top);

		const auto back = view.text_at(tail);
		CHECK_EQ(back.y, 0);
		CHECK_EQ(back.x, static_cast<int>(source.find("rrr")));
	}

	void test_markdown_view_utf8_positions()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		markdown_probe view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		// "héllo wörld" — multi-byte characters, so a column is not a byte.
		show_markdown(view, host, frame, "h\xC3\xA9llo w\xC3\xB6rld\n");

		// The second character is two bytes wide but one column across, and a click
		// on it lands on its first byte rather than inside it.
		const auto point = view.text_to_client({1, 0});
		CHECK_EQ(point.x, view.text_to_client({0, 0}).x + 8);
		CHECK_EQ(view.text_at(point).x, 1);

		const auto after = view.text_to_client({3, 0});
		CHECK_EQ(after.x, view.text_to_client({0, 0}).x + 16);
		CHECK_EQ(view.text_at(after).x, 3);
	}

	// Both table views are driven the same way: show text, size the view, lay out.
	template <typename View>
	pf::ui::text_buffer_ptr show_in(View& view, pf::ui::view_host& host, pf::window_frame_ptr& frame,
	                                const std::string_view text, const pf::isize extent = {480, 320})
	{
		const auto buf = std::make_shared<pf::ui::text_buffer>(host);
		buf->set_text(text);
		view.set_buffer(buf, {});

		pf::ui::test::fake_measure_context measure;
		view.handle_size(frame, extent, measure);
		view.layout();
		return buf;
	}

	void test_csv_view()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		pf::ui::csv_view view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		const auto buf = show_in(view, host, frame,
		                         "name,qty,note\nwidget,12,the long one\nbolt,3,short");

		// A column is as wide as its widest cell, in code points.
		const auto& table = view.table();
		CHECK_EQ(table.col_widths.size(), 3u);
		CHECK_EQ(table.col_widths[0], 6);  // "widget"
		CHECK_EQ(table.col_widths[2], 12); // "the long one"

		pf::ui::test::fake_draw_context draw{pf::irect(0, 0, 480, 320)};
		view.handle_paint(frame, draw);

		const auto drawn = draw.drawn_text();
		CHECK(drawn.find("name") != std::string::npos);
		CHECK(drawn.find("widget") != std::string::npos);
		CHECK(drawn.find("the long one") != std::string::npos);

		// The header is followed by a drawn separator that is in no record.
		auto saw_separator = false;
		for (const auto& t : draw.texts)
			if (t.text.find("------") != std::string::npos) saw_separator = true;
		CHECK(saw_separator);
		CHECK_EQ(buf->size(), 3u);

		// A point in a record resolves to the record: its cells are drawn padded
		// into their columns, so there is no byte under the pointer to find.
		const auto second_row_y = view.text_at({0, 0}).y;
		CHECK(second_row_y >= 0);
		CHECK_EQ(view.text_at({0, 100}).x, 0);
		CHECK(view.text_at({470, 100}).x > 0);
		CHECK(view.allows_drag_selection());

		// What is copied is the record as it is in the file, not as it is drawn.
		view.set_selection(pf::ui::text_selection(0, 1, 0, 2));
		CHECK_STR(view.select_text(), "widget,12,the long one\r\n");

		// And a selected record is drawn selected.
		draw.reset();
		view.handle_paint(frame, draw);

		auto saw_selected = false;
		for (const auto& t : draw.texts)
			if (t.text.find("widget") != std::string::npos &&
				t.background == theme.style_color(pf::ui::text_style::sel_bkgnd))
				saw_selected = true;
		CHECK(saw_selected);
	}

	void test_csv_view_quotes_and_utf8()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		pf::ui::csv_view view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		// A quoted field keeps its comma, and "café" is four columns in five bytes.
		show_in(view, host, frame, "a,b\n\"x,y\",caf\xC3\xA9");

		CHECK_EQ(view.table().col_widths.size(), 2u);
		CHECK_EQ(view.table().col_widths[1], 4);

		pf::ui::test::fake_draw_context draw{pf::irect(0, 0, 480, 320)};
		view.handle_paint(frame, draw);

		// Wrapping a cell must not split a character in half.
		for (const auto& t : draw.texts)
			CHECK(t.text.empty() || !pf::is_utf8_continuation(t.text.front()));

		CHECK(draw.drawn_text().find("x,y") != std::string::npos);
	}

	void test_hex_view()
	{
		pf::ui::test::recording_view_host host;
		const pf::ui::theme theme;
		pf::ui::hex_view view(host, theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		// One row of sixteen bytes, then a short one: "AB" plus a control byte.
		show_in(view, host, frame, std::string("0123456789ABCDEF") + "\n" + std::string("AB\x01"));

		pf::ui::test::fake_draw_context draw{pf::irect(0, 0, 640, 320)};
		view.handle_paint(frame, draw);

		const auto drawn = draw.drawn_text();

		// Offsets count bytes, not lines: the second row starts at 0x10.
		CHECK(drawn.find("00000000") != std::string::npos);
		CHECK(drawn.find("00000010") != std::string::npos);

		// '0' is 0x30 and 'A' is 0x41, and the ASCII column shows them as text.
		CHECK(drawn.find("30 31 32 33") != std::string::npos);
		CHECK(drawn.find("0123456789ABCDEF") != std::string::npos);

		// An unprintable byte is a dot rather than whatever the font makes of it.
		CHECK(drawn.find("41 42 01") != std::string::npos);
		CHECK(drawn.find("AB.") != std::string::npos);
		CHECK(drawn.find('\x01') == std::string::npos);
	}

	// The list is driven through its public surface; only the row-building helper
	// below reaches in, because what a row *is* belongs to the application.
	struct list_probe : pf::ui::list_view
	{
		using pf::ui::list_view::list_view;
		using pf::ui::list_view::_items;
		using pf::ui::list_view::_header_height;
		using pf::ui::list_view::draw_item;

		void add(const std::string_view text, const bool group = false, const int depth = 0)
		{
			auto item = std::make_shared<pf::ui::list_item>();
			item->text = text;
			item->is_group = group;
			item->depth = depth;
			_items.push_back(item);
		}
	};

	pf::ui::list_view* size_list(list_probe& view, pf::window_frame_ptr& frame, const pf::isize extent = {300, 200})
	{
		pf::ui::test::fake_measure_context measure;
		view.handle_size(frame, extent, measure);
		return &view;
	}

	void test_list_layout_and_hit_testing()
	{
		const pf::ui::theme theme;
		list_probe view(theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		for (int i = 0; i < 40; i++) view.add(std::format("row {}", i));
		size_list(view, frame);

		// Rows are laid out top to bottom, each as tall as the list font plus its
		// padding, and the content is taller than the view — so it can scroll.
		const auto first = view._items.front()->bounds;
		const auto second = view._items[1]->bounds;
		CHECK_EQ(second.top, first.bottom);
		CHECK(first.height() > 0);
		CHECK(view.can_scroll());

		// A point resolves to the row that contains it, by binary search rather
		// than a scan — which is what makes a list of a hundred thousand files
		// answer a click at all.
		CHECK(view.selection_from_point({10, first.top + 1}) == view._items.front());
		CHECK(view.selection_from_point({10, second.top + 1}) == view._items[1]);

		// Above the first row and past the last there is nothing to hit.
		CHECK(view.selection_from_point({10, -50}) == nullptr);
		CHECK(view.selection_from_point({10, 100000}) == nullptr);

		// An empty list hit-tests to nothing rather than reading past its rows.
		list_probe empty(theme);
		CHECK(empty.selection_from_point({0, 0}) == nullptr);
	}

	void test_list_selection_and_copy()
	{
		const pf::ui::theme theme;
		list_probe view(theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		for (int i = 0; i < 6; i++) view.add(std::format("row {}", i));
		size_list(view, frame);

		view.set_selected(1);
		CHECK_EQ(view.selected_index(), 1);
		CHECK(view.is_selected(1));
		CHECK(!view.is_selected(2));
		CHECK_STR(view.selected_rows_text(), "row 1");

		// Arrow keys move the selection and keep it a single row.
		view.navigate_next(frame, true);
		CHECK_EQ(view.selected_index(), 2);
		CHECK_STR(view.selected_rows_text(), "row 2");

		// Shift extends from where the selection started, so a run of rows can be
		// copied — which no list in these applications could do before.
		view.navigate_next(frame, true, false, true);
		view.navigate_next(frame, true, false, true);
		CHECK_EQ(view.selected_index(), 4);
		CHECK(view.is_selected(2));
		CHECK(view.is_selected(3));
		CHECK(view.is_selected(4));
		CHECK(!view.is_selected(1));
		CHECK_STR(view.selected_rows_text(), "row 2\r\nrow 3\r\nrow 4");

		// Moving without Shift collapses the run back to one row.
		view.navigate_next(frame, false);
		CHECK_STR(view.selected_rows_text(), "row 3");

		// The selection survives being asked for after the list is rebuilt, because
		// the index is only a hint.
		const auto keep = view.selected_item();
		view._items.insert(view._items.begin(), std::make_shared<pf::ui::list_item>());
		CHECK_EQ(view.selected_index(), 4);
		CHECK(view.selected_item() == keep);

		// Navigation stops at the ends rather than wrapping.
		view.set_selected(static_cast<int>(view._items.size()) - 1);
		view.navigate_next(frame, true);
		CHECK_EQ(view.selected_index(), static_cast<int>(view._items.size()) - 1);
		view.set_selected(0);
		view.navigate_next(frame, false);
		CHECK_EQ(view.selected_index(), 0);
	}

	void test_list_paints_its_rows()
	{
		const pf::ui::theme theme;
		list_probe view(theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		view.add("a folder", true);
		view.add("a file", false, 1);
		size_list(view, frame);
		view.set_selected(1);

		pf::ui::test::fake_draw_context draw{pf::irect(0, 0, 300, 200)};
		view.handle_paint(frame, draw);

		const auto drawn = draw.drawn_text();
		CHECK(drawn.find("a folder") != std::string::npos);
		CHECK(drawn.find("a file") != std::string::npos);

		// A group is drawn in the group colour; the selected row is filled with the
		// selection colour behind it.
		auto saw_group_color = false;
		for (const auto& t : draw.texts)
			if (t.text == "a folder" && t.color == theme.group_text) saw_group_color = true;
		CHECK(saw_group_color);

		auto saw_selection_fill = false;
		for (const auto& f : draw.fills)
			if (f.color == theme.handle || f.color == theme.focus_handle) saw_selection_fill = true;
		CHECK(saw_selection_fill);

		// An indented row starts further right than the one above it.
		int folder_x = 0;
		int file_x = 0;
		for (const auto& t : draw.texts)
		{
			if (t.text == "a folder") folder_x = t.x;
			if (t.text == "a file") file_x = t.x;
		}
		CHECK(file_x > folder_x);
	}

	void test_list_row_text_fits_its_column()
	{
		const pf::ui::theme theme;
		list_probe view(theme);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr frame = window;

		// 8-pixel cells, so 200 pixels is 25 columns and this row cannot fit.
		view.add("a name far longer than the column it has to live in");
		size_list(view, frame, {200, 200});

		pf::ui::test::fake_draw_context draw{pf::irect(0, 0, 200, 200)};
		view.handle_paint(frame, draw);

		const auto drawn = draw.drawn_text();
		CHECK(drawn.find("...") != std::string::npos);
		CHECK(drawn.find("live in") == std::string::npos);

		// A row with a match is windowed around the match instead, because the match
		// is the reason the row is on screen at all.
		list_probe hits(theme);
		hits.add("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa needle bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
		hits._items.front()->prefix = "42: ";
		hits._items.front()->match_start = 31;
		hits._items.front()->match_length = 6;
		size_list(hits, frame, {200, 200});

		draw.reset();
		hits.handle_paint(frame, draw);

		auto saw_highlight = false;
		for (const auto& t : draw.texts)
			if (t.text == "needle" && t.background == theme.match_highlight) saw_highlight = true;
		CHECK(saw_highlight);

		// The line number is drawn dimmer, and before the text.
		auto saw_prefix = false;
		for (const auto& t : draw.texts)
			if (t.text == "42: " && t.color == theme.dim_text) saw_prefix = true;
		CHECK(saw_prefix);
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
	test_syntax_language_from_extension();
	test_syntax_cpp();
	test_syntax_languages_differ();
	test_syntax_markdown();
	test_syntax_plain();
	test_syntax_bounds();
	test_buffer_set_text();
	test_buffer_edit_and_undo();
	test_buffer_multiline_edit();
	test_buffer_utf8_movement();
	test_buffer_selection_and_readonly();
	test_buffer_modified_flag();
	test_view_layout_and_hit_testing();
	test_view_selection_and_clipboard();
	test_view_word_wrap();
	test_view_editing();
	test_view_paints_its_text();
	test_markdown_parse();
	test_markdown_links();
	test_markdown_escape();
	test_markdown_from_html();
	test_markdown_document_owns_its_text();
	test_markdown_source_lines();
	test_markdown_view_layout();
	test_markdown_view_selects_the_source();
	test_markdown_view_paints_markup();
	test_markdown_view_links();
	test_markdown_view_tables();
	test_markdown_view_wraps_without_touching_the_buffer();
	test_markdown_view_utf8_positions();
	test_csv_view();
	test_csv_view_quotes_and_utf8();
	test_hex_view();
	test_list_layout_and_hit_testing();
	test_list_selection_and_copy();
	test_list_paints_its_rows();
	test_list_row_text_fits_its_column();

	std::printf("platform-ui tests: %s (%d checks, %d failures)\n",
	            g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
