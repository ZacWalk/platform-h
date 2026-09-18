// view_list.h — the list panel: rows, selection, hover, keyboard and the scrollbar.
//
// A row is a `list_item`: some text, an optional dimmer prefix, an indent depth,
// and — for a search hit — a run of the text to highlight. What the row *means* is
// the application's business, so it hangs its own object off `data` and this layer
// never looks inside it.
//
// Rows are laid out once and hit-tested by binary search over their bounds, so a
// list of a hundred thousand files costs a handful of comparisons per click and
// per paint rather than a scan.

#pragma once

#include "ui/theme.h"
#include "ui/view_base.h"
#include "ui/view_text.h"
#include "ui/widgets.h"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace pf::ui
{
	struct list_item
	{
		pf::irect bounds; // where the last layout put it

		std::string text;
		std::string prefix; // drawn dimmer before the text — a line number, say

		int depth = 0;
		bool is_group = false;
		bool expanded = false;

		// A run of `text` to draw highlighted, as a byte offset and a length. When a
		// row has one, long text is windowed around it rather than simply truncated.
		int match_start = -1;
		int match_length = 0;

		// The application's own handle for this row. The list never dereferences it.
		std::shared_ptr<void> data;

		template <typename T>
		[[nodiscard]] std::shared_ptr<T> as() const { return std::static_pointer_cast<T>(data); }

		[[nodiscard]] bool has_match() const { return match_start >= 0 && match_length > 0; }
	};

	using list_item_ptr = std::shared_ptr<list_item>;

	class list_view : public view_base
	{
	protected:
		const theme& _theme;
		view_context* _context = nullptr;

		std::vector<list_item_ptr> _items;
		list_item_ptr _selected_item;
		list_item_ptr _hover_item;
		bool _focused = false;

		// Position of _selected_item; a hint, revalidated by selected_index()
		mutable int _selected_index = -1;

		// Where a shift-extended selection started; -1 when only one row is selected
		int _anchor_index = -1;

		pf::isize _font_extent = {10, 10};
		int _header_height = 0;

	public:
		list_view(const theme& th, view_context* context = nullptr) : _theme(th), _context(context)
		{
		}

		~list_view() override = default;

		[[nodiscard]] list_item_ptr selected_item() const { return _selected_item; }

		// O(1) while the hint holds, falling back to a scan after the list is rebuilt
		[[nodiscard]] int selected_index() const
		{
			if (!_selected_item) return -1;

			if (_selected_index >= 0 && _selected_index < static_cast<int>(_items.size())
				&& _items[_selected_index] == _selected_item)
				return _selected_index;

			for (int i = 0; i < static_cast<int>(_items.size()); i++)
			{
				if (_items[i] == _selected_item)
				{
					_selected_index = i;
					return i;
				}
			}

			_selected_index = -1;
			return -1;
		}

		// The rows a shift-extended selection covers, as an inclusive range
		[[nodiscard]] std::pair<int, int> selected_range() const
		{
			const auto current = selected_index();
			if (current < 0) return {-1, -1};
			if (_anchor_index < 0 || _anchor_index >= static_cast<int>(_items.size())) return {current, current};
			return {std::min(_anchor_index, current), std::max(_anchor_index, current)};
		}

		[[nodiscard]] bool is_selected(const int index) const
		{
			const auto [lo, hi] = selected_range();
			return lo >= 0 && index >= lo && index <= hi;
		}

		void set_selected(const int index)
		{
			_selected_index = index;
			_anchor_index = index;
			_selected_item = index >= 0 && index < static_cast<int>(_items.size()) ? _items[index] : nullptr;
		}

		// --- Copying rows out ---

		[[nodiscard]] bool can_copy_rows() const { return _selected_item != nullptr; }

		// What one row copies as. The default is what it shows; an application whose
		// rows stand for something longer — a path, say — overrides it.
		[[nodiscard]] virtual std::string row_text(const list_item& item) const
		{
			return item.prefix.empty() ? item.text : item.prefix + item.text;
		}

		// Copies the selected rows, one per line. Lists in these applications had no
		// copy at all before this.
		[[nodiscard]] std::string selected_rows_text() const
		{
			const auto [lo, hi] = selected_range();
			if (lo < 0) return {};

			std::string out;

			for (int i = lo; i <= hi; i++)
			{
				if (!out.empty()) out += "\r\n";
				out += row_text(*_items[i]);
			}

			return out;
		}

		bool copy_rows() const
		{
			const auto text = selected_rows_text();
			return !text.empty() && pf::platform_text_to_clipboard(text);
		}

		// --- Inline edit field operations, invoked by the application command table ---

		[[nodiscard]] bool has_active_edit_box() { return active_edit_box() != nullptr; }

		bool edit_select_all()
		{
			auto* e = active_edit_box();
			if (!e) return false;
			e->edit.select_all();
			return true;
		}

		[[nodiscard]] bool edit_can_copy()
		{
			auto* e = active_edit_box();
			return e && e->edit.has_selection();
		}

		bool edit_copy()
		{
			auto* e = active_edit_box();
			return e && e->edit.copy_to_clipboard();
		}

		bool edit_cut()
		{
			auto* e = active_edit_box();
			if (!e || !e->edit.cut_to_clipboard()) return false;
			on_edit_text_changed();
			return true;
		}

		bool edit_paste()
		{
			auto* e = active_edit_box();
			if (!e || !e->edit.paste_from_clipboard()) return false;
			on_edit_text_changed();
			return true;
		}

		bool edit_delete()
		{
			auto* e = active_edit_box();
			if (!e || !e->edit.delete_forward()) return false;
			on_edit_text_changed();
			return true;
		}

		// --- Navigation ---

		void navigate_next(const pf::window_frame_ptr& window, const bool forward,
		                   const bool skip_groups = false, const bool extend = false)
		{
			if (_items.empty()) return;

			const auto count = static_cast<int>(_items.size());
			const auto current = selected_index();
			const auto step = forward ? 1 : -1;

			auto next = current < 0 ? (forward ? 0 : count - 1) : current + step;

			while (next >= 0 && next < count && skip_groups && _items[next]->is_group)
				next += step;

			if (next < 0 || next >= count) return;

			const auto anchor = extend && current >= 0 ? (_anchor_index < 0 ? current : _anchor_index) : next;
			set_selected(next);
			_anchor_index = anchor;

			ensure_visible(window, _selected_item);
			window->invalidate();
			on_item_selected(window, _selected_item, false);
		}

		[[nodiscard]] int scroll_y() const { return _scroll_offset.y; }

		[[nodiscard]] bool can_scroll() const { return _content_extent.cy > _view_extent.cy; }

		// --- frame_reactor interface ---

		uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
		                        const pf::message_params& params) override
		{
			using mt = pf::message_type;

			if (msg == mt::create) return 0;
			if (msg == mt::erase_background) return 1;
			if (msg == mt::timer) return on_timer(window, params.timer_id);
			if (msg == mt::set_focus || msg == mt::kill_focus)
			{
				update_focus(window);
				return 0;
			}

			return 0;
		}

		uint32_t handle_keyboard(pf::window_frame_ptr window, const pf::keyboard_message_type msg,
		                         const pf::keyboard_params& params) override
		{
			using kt = pf::keyboard_message_type;

			if (msg == kt::char_input) return on_char(window, params.ch);
			if (msg == kt::key_down) return on_key_down(window, params.vk);
			return 0;
		}

		uint32_t handle_mouse(const pf::window_frame_ptr window, const pf::mouse_message_type msg,
		                      const pf::mouse_params& params) override
		{
			using mt = pf::mouse_message_type;

			if (msg == mt::set_cursor)
			{
				if (params.hit_test == 1 /*HTCLIENT*/)
				{
					window->set_cursor_shape(pf::cursor_shape::arrow);
					return 1;
				}
				return 0;
			}
			if (msg == mt::left_button_down) return on_left_button_down(window, params.point);
			if (msg == mt::left_button_up) return on_left_button_up(window);
			if (msg == mt::left_button_dbl_clk) return 0; // the single click already handled it
			if (msg == mt::mouse_move) return on_mouse_move(window, params.point);
			if (msg == mt::mouse_leave) return on_mouse_leave(window);
			if (msg == mt::mouse_wheel)
			{
				if (params.control)
				{
					zoom(window, params.wheel_delta > 0 ? 1 : -1);
					return 0;
				}

				scroll_to(window, _scroll_offset.y - params.wheel_delta / 2);
				return 0;
			}

			return 0;
		}

		void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
		                 pf::measure_context& measure) override
		{
			_view_extent = extent;
			_vscroll.set_dpi_scale(_theme.dpi_scale);
			layout_list(measure);
			clamp_scroll_offset();
			window->invalidate();
		}

		void layout_list(const pf::measure_context& measure)
		{
			_font_extent = measure.measure_char(_theme.list_font);
			layout_list();
		}

		void layout_list()
		{
			int y = _header_height + _theme.list_top_pad;
			const auto item_height = _font_extent.cy + _theme.padding_y * 2;

			for (const auto& i : _items)
			{
				i->bounds = pf::irect(0, y, _view_extent.cx, y + item_height);
				y += item_height;
			}

			_content_extent.cy = y + _theme.list_scroll_pad;
		}

		void handle_paint(pf::window_frame_ptr& window, pf::draw_context& dc) override
		{
			const auto r = client_rect();
			dc.fill_solid_rect(r, _theme.tool_background);

			const auto hh = _header_height;

			if (hh > 0)
				draw_header(window, dc, {r.left, r.top, r.right, r.top + hh});

			const auto items_top = r.top + hh;

			for (int idx = first_visible_item(); idx < static_cast<int>(_items.size()); idx++)
			{
				const auto& i = _items[idx];
				auto bounds = i->bounds.offset(0, -_scroll_offset.y);

				if (bounds.top >= r.bottom) break;
				if (bounds.bottom <= items_top) continue;
				if (bounds.top < items_top) bounds.top = items_top;

				const bool selected = is_selected(idx);
				const bool hovered = i == _hover_item;

				if (hovered) dc.fill_solid_rect(bounds, _theme.handle_hover);
				if (selected) dc.fill_solid_rect(bounds, _focused ? _theme.focus_handle : _theme.handle);

				draw_item(dc, i, bounds, selected, hovered);
			}

			auto sb_rect = r;
			sb_rect.top = items_top;
			_vscroll.update(_content_extent.cy, _view_extent.cy - hh, _scroll_offset.y);
			_vscroll.draw(dc, sb_rect);
		}

		// --- Hit testing and selection ---

		[[nodiscard]] list_item_ptr selection_from_point(const pf::ipoint& pt) const
		{
			int lo = 0;
			int hi = static_cast<int>(_items.size()) - 1;

			while (lo <= hi)
			{
				const int mid = lo + (hi - lo) / 2;
				const auto& b = _items[mid]->bounds;

				if (pt.y < b.top) hi = mid - 1;
				else if (pt.y >= b.bottom) lo = mid + 1;
				else return _items[mid];
			}

			return nullptr;
		}

		void set_hover(const pf::window_frame_ptr& window, const list_item_ptr& h)
		{
			if (_hover_item != h)
			{
				_hover_item = h;
				window->invalidate();
			}
		}

		void select_list_item(const pf::window_frame_ptr& window, const list_item_ptr& item,
		                      const bool activated = true)
		{
			for (int i = 0; i < static_cast<int>(_items.size()); i++)
			{
				if (_items[i] == item)
				{
					set_selected(i);
					ensure_visible(window, item);
					window->invalidate();
					on_item_selected(window, item, activated);
					return;
				}
			}
		}

		uint32_t on_left_button_down(const pf::window_frame_ptr& window, const pf::ipoint& point)
		{
			window->set_focus();

			const auto hh = _header_height;
			if (point.y < hh) return 0;

			const auto rc = pf::irect(0, hh, _view_extent.cx, _view_extent.cy);

			if (!_vscroll.handle_mouse(pf::mouse_message_type::left_button_down, point, rc, window,
			                           [this, &window](const int pos) { scroll_to(window, pos); }))
			{
				const auto scroll_pt = pf::ipoint(point.x, point.y + _scroll_offset.y);

				if (const auto i = selection_from_point(scroll_pt))
				{
					set_hover(window, i);

					// Shift keeps the anchor, so the rows between it and this one are
					// the selection — which is what makes copying a run of them useful.
					if (window->is_key_down(pf::platform_key::Shift) && _selected_item)
						extend_selection_to(window, i);
					else
						select_list_item(window, i);
				}

				window->invalidate();
			}

			return 0;
		}

		uint32_t on_mouse_move(const pf::window_frame_ptr& window, const pf::ipoint& point)
		{
			const auto rc = pf::irect(0, _header_height, _view_extent.cx, _view_extent.cy);

			_vscroll.handle_mouse(pf::mouse_message_type::mouse_move, point, rc, window,
			                      [this, &window](const int pos) { scroll_to(window, pos); });

			if (!_vscroll._tracking)
			{
				if (point.y >= _header_height)
					set_hover(window, selection_from_point({point.x, point.y + _scroll_offset.y}));
				else
					set_hover(window, nullptr);
			}

			return 0;
		}

		uint32_t on_left_button_up(const pf::window_frame_ptr& window)
		{
			_vscroll.handle_mouse(pf::mouse_message_type::left_button_up, {}, {}, window,
			                      [this, &window](const int pos) { scroll_to(window, pos); });
			return 0;
		}

		uint32_t on_mouse_leave(const pf::window_frame_ptr& window)
		{
			_hover_item = nullptr;
			_vscroll.handle_mouse(pf::mouse_message_type::mouse_leave, {}, {}, window);
			window->invalidate();
			return 0;
		}

		void scroll_to(const pf::window_frame_ptr& window, int offset)
		{
			offset = std::clamp(offset, 0, std::max(0, _content_extent.cy - _view_extent.cy));

			if (_scroll_offset.y != offset)
			{
				_scroll_offset.y = offset;
				window->invalidate();
			}
		}

		void ensure_visible(const pf::window_frame_ptr& window, const list_item_ptr& item)
		{
			if (!item) return;

			const auto visible_top = _scroll_offset.y + _header_height;
			const auto visible_bottom = _scroll_offset.y + _view_extent.cy;

			if (item->bounds.top < visible_top)
				scroll_to(window, item->bounds.top - _header_height);
			else if (item->bounds.bottom > visible_bottom)
				scroll_to(window, item->bounds.bottom - _view_extent.cy);
		}

	protected:
		// --- What the application decides ---

		virtual void draw_header(pf::window_frame_ptr& window, pf::draw_context& dc, const pf::irect& header_rect)
		{
		}

		virtual void on_item_selected(const pf::window_frame_ptr& window, const list_item_ptr& item, bool activated)
		{
		}

		virtual uint32_t on_timer(pf::window_frame_ptr& window, uint32_t id) { return 0; }

		virtual uint32_t on_char(pf::window_frame_ptr& window, char32_t ch) { return 0; }

		// What Enter means — usually "I am done here, move focus on" — is the
		// application's, not the list's.
		virtual void on_item_activated(const pf::window_frame_ptr& window)
		{
		}

		virtual void zoom(const pf::window_frame_ptr& window, const int delta)
		{
			if (_context) _context->on_zoom(delta);
		}

		// The colour a row's text is drawn in. The default is the theme's; an
		// application that marks rows — a modified file, say — overrides it.
		[[nodiscard]] virtual pf::color_t item_text_color(const list_item& item) const
		{
			return item.is_group ? _theme.group_text : _theme.text;
		}

		// The inline text field currently accepting input, if any (search box, rename box)
		virtual edit_box_widget* active_edit_box() { return nullptr; }

		virtual void on_edit_text_changed()
		{
		}

		virtual uint32_t on_key_down(pf::window_frame_ptr& window, const unsigned int vk)
		{
			namespace pk = pf::platform_key;
			const bool shift = window->is_key_down(pk::Shift);

			if (vk == pk::Down)
			{
				navigate_next(window, true, false, shift);
				return 0;
			}

			if (vk == pk::Up)
			{
				navigate_next(window, false, false, shift);
				return 0;
			}

			if (vk == pk::Return)
			{
				on_item_activated(window);
				return 0;
			}

			if (vk == pk::Escape)
			{
				if (_context) _context->on_escape();
				return 0;
			}

			return 0;
		}

		virtual void update_focus(pf::window_frame_ptr& window)
		{
			const bool focused = window && window->has_focus();

			if (_focused != focused)
			{
				_focused = focused;
				window->invalidate();
			}
		}

		void draw_expand_icon(pf::draw_context& dc, const int cx, const int cy, const bool expanded) const
		{
			constexpr auto size = 4;

			if (expanded)
			{
				const pf::ipoint pts[] = {{cx - size, cy - size / 2}, {cx, cy + size / 2}, {cx + size, cy - size / 2}};
				dc.draw_lines(pts, _theme.line);
			}
			else
			{
				const pf::ipoint pts[] = {{cx - size / 2, cy - size}, {cx + size / 2, cy}, {cx - size / 2, cy + size}};
				dc.draw_lines(pts, _theme.line);
			}
		}

		void draw_item(pf::draw_context& dc, const list_item_ptr& item,
		               const pf::irect& bounds, const bool selected, const bool hovered)
		{
			const auto indent = _theme.padding_x + item->depth * _theme.indent;
			const auto font_spec = _theme.list_font;

			const auto bg = selected
				                ? (_focused ? _theme.focus_handle : _theme.handle)
				                : (hovered ? _theme.handle_hover : _theme.tool_background);

			if (item->is_group)
			{
				draw_expand_icon(dc, bounds.left + indent + 4, (bounds.top + bounds.bottom) / 2, item->expanded);

				auto text_bounds = bounds;
				text_bounds.left += indent + 14;
				text_bounds = text_bounds.inflate(-_theme.padding_x, -_theme.padding_y);
				dc.draw_text(text_bounds.left, text_bounds.top, text_bounds, item->text,
				             font_spec, item_text_color(*item), bg);
				return;
			}

			auto text_bounds = bounds;
			text_bounds.left += indent + 4;
			text_bounds = text_bounds.inflate(-_theme.padding_x, -_theme.padding_y);

			auto content_rect = text_bounds;

			if (!item->prefix.empty())
			{
				const auto prefix_sz = dc.measure_text(item->prefix, font_spec);
				auto prefix_rect = text_bounds;
				prefix_rect.right = prefix_rect.left + prefix_sz.cx;
				dc.draw_text(prefix_rect.left, prefix_rect.top, prefix_rect, item->prefix, font_spec,
				             _theme.dim_text, bg);
				content_rect.left += prefix_sz.cx;
			}

			if (item->has_match())
				draw_matched_text(dc, *item, content_rect, font_spec, bg);
			else
				draw_plain_text(dc, *item, content_rect, font_spec, bg);
		}

	private:
		int first_visible_item() const
		{
			int lo = 0;
			int hi = static_cast<int>(_items.size()) - 1;

			while (lo <= hi)
			{
				const int mid = lo + (hi - lo) / 2;
				if (_items[mid]->bounds.bottom <= _scroll_offset.y) lo = mid + 1;
				else hi = mid - 1;
			}

			return lo;
		}

		void extend_selection_to(const pf::window_frame_ptr& window, const list_item_ptr& item)
		{
			const auto anchor = _anchor_index >= 0 ? _anchor_index : selected_index();

			for (int i = 0; i < static_cast<int>(_items.size()); i++)
			{
				if (_items[i] != item) continue;

				set_selected(i);
				_anchor_index = anchor;
				ensure_visible(window, item);
				window->invalidate();
				on_item_selected(window, item, false);
				return;
			}
		}

		// Text with a match in it: a window around the match rather than a prefix of
		// the line, because the match is the reason the row is on screen at all.
		void draw_matched_text(pf::draw_context& dc, const list_item& item, const pf::irect& content_rect,
		                       const pf::font& font_spec, const pf::color_t bg) const
		{
			const std::string_view name = item.text;
			const auto name_len = static_cast<int>(name.size());
			const auto avail_width = content_rect.width();
			const auto full_width = dc.measure_text(name, font_spec).cx;

			std::string_view display_text = name;
			int display_match_start = item.match_start;
			bool ellipsis_prefix = false;
			bool ellipsis_suffix = false;

			if (full_width > avail_width)
			{
				// Widen a window around the match until it fills the available width.
				// Width grows monotonically with the radius, so binary search it rather
				// than stepping a character at a time — this runs on every paint.
				const auto ellipsis_width = dc.measure_text("...", font_spec).cx;
				const int match_start = std::clamp(item.match_start, 0, name_len);
				const int match_end = std::clamp(match_start + item.match_length, match_start, name_len);

				int vis_start = match_start;
				int vis_end = match_end;

				const auto width_of_radius = [&](const int radius)
				{
					vis_start = std::max(0, match_start - radius);
					while (vis_start > 0 && pf::is_utf8_continuation(name[vis_start])) vis_start--;

					vis_end = std::min(name_len, match_end + radius);
					while (vis_end < name_len && pf::is_utf8_continuation(name[vis_end])) vis_end++;

					auto w = dc.measure_text(name.substr(vis_start, vis_end - vis_start), font_spec).cx;
					if (vis_start > 0) w += ellipsis_width;
					if (vis_end < name_len) w += ellipsis_width;
					return w;
				};

				int lo = 0;
				int hi = name_len;

				while (lo < hi)
				{
					const auto mid = lo + (hi - lo + 1) / 2;
					if (width_of_radius(mid) <= avail_width) lo = mid;
					else hi = mid - 1;
				}

				width_of_radius(lo);

				ellipsis_prefix = vis_start > 0;
				ellipsis_suffix = vis_end < name_len;
				display_text = name.substr(vis_start, vis_end - vis_start);
				display_match_start = match_start - vis_start;
			}

			auto seg_rect = content_rect;
			const auto text_color = item_text_color(item);

			const auto draw_segment = [&](const std::string_view text, const pf::color_t seg_bg)
			{
				if (text.empty()) return;
				seg_rect.right = std::min(seg_rect.left + dc.measure_text(text, font_spec).cx, content_rect.right);
				dc.draw_text(seg_rect.left, seg_rect.top, seg_rect, text, font_spec, text_color, seg_bg);
				seg_rect.left = seg_rect.right;
			};

			if (ellipsis_prefix) draw_segment("...", bg);

			if (display_match_start >= 0 && display_match_start < static_cast<int>(display_text.size()))
			{
				const auto match_end = std::min(display_match_start + item.match_length,
				                                static_cast<int>(display_text.size()));

				draw_segment(display_text.substr(0, display_match_start), bg);
				draw_segment(display_text.substr(display_match_start, match_end - display_match_start),
				             _theme.match_highlight);
				draw_segment(display_text.substr(match_end), bg);
			}
			else
			{
				draw_segment(display_text, bg);
			}

			if (ellipsis_suffix) draw_segment("...", bg);

			if (seg_rect.left < content_rect.right)
				dc.fill_solid_rect(seg_rect.left, content_rect.top,
				                   content_rect.right - seg_rect.left, content_rect.height(), bg);
		}

		void draw_plain_text(pf::draw_context& dc, const list_item& item, const pf::irect& content_rect,
		                     const pf::font& font_spec, const pf::color_t bg) const
		{
			auto display_text = std::string_view(item.text);
			std::string ellipsized;

			if (dc.measure_text(display_text, font_spec).cx > content_rect.width())
			{
				const auto avail = content_rect.width() - dc.measure_text("...", font_spec).cx;

				// Binary search the longest whole-codepoint prefix that fits
				int lo = 0;
				int hi = static_cast<int>(pf::utf8_codepoint_count(display_text));

				while (lo < hi)
				{
					const auto mid = lo + (hi - lo + 1) / 2;
					const auto bytes = pf::utf8_truncate(display_text, mid);
					if (dc.measure_text(display_text.substr(0, bytes), font_spec).cx <= avail) lo = mid;
					else hi = mid - 1;
				}

				ellipsized = std::string(display_text.substr(0, pf::utf8_truncate(display_text, lo)));
				ellipsized += "...";
				display_text = ellipsized;
			}

			dc.draw_text(content_rect.left, content_rect.top, content_rect, display_text,
			             font_spec, item_text_color(item), bg);
		}
	};
}
