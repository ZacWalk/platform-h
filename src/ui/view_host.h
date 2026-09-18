// view_host.h — what a view needs from whatever is hosting it.
//
// A view never repaints directly and never names an application command. It
// raises a request on view_host and lets the host coalesce the work, and it
// describes menu actions to a context_menu_builder rather than referring to a
// command table it cannot see.

#pragma once

#include "platform.h"
#include "ui/text_types.h"

namespace pf::ui
{
	class view_host
	{
	public:
		virtual ~view_host() = default;

		// Repaint only — the text of these lines is unchanged.
		virtual void invalidate_lines(int start, int end) = 0;

		// The text of these lines changed, so layout and highlighting must be redone.
		virtual void lines_changed(int start, int end) = 0;

		// Lines were inserted after 'at' (delta > 0) or erased from 'at' + 1
		// (delta < 0). The text of line 'at' changed too.
		virtual void line_count_changed(int at, int delta) = 0;

		// Scroll so this position is on screen.
		virtual void ensure_visible(const text_location& pt) = 0;

		// Layout, caret and scrollbar all need recomputing.
		virtual void invalidate_view() = 0;

		// The caret moved; nothing else changed. Kept separate from invalidate_view
		// so moving the cursor does not force a relayout of the document.
		virtual void invalidate_caret() = 0;

		// The content extent changed, so the scrollbar needs recomputing.
		virtual void invalidate_scrollbar() = 0;

		// The status text or the focus band changed. Separate again because it
		// repaints chrome around the text rather than the text itself.
		virtual void invalidate_status() = 0;
	};

	// The view adds what it can do; the host decides how it appears and what sits
	// beside it. Keeps command identity on the application side of the boundary.
	class context_menu_builder
	{
	public:
		virtual ~context_menu_builder() = default;

		virtual void add_item(std::string_view text, std::function<void()> action,
		                      bool enabled = true, bool checked = false) = 0;

		virtual void add_separator() = 0;
	};
}
