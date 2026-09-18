// table_layout.cpp — the pure cell-parsing helpers behind the table views.
//
// These are out of line because every view that renders a table includes the header,
// and because being pure makes them testable with no window.

#include "platform.h"
#include "ui/table_layout.h"

namespace pf::ui::table_layout
{
	std::string_view trim_cell(const std::string_view s)
	{
		const auto start = s.find_first_not_of(u8' ');
		if (start == std::string_view::npos) return {};
		const auto end = s.find_last_not_of(u8' ');
		return s.substr(start, end - start + 1);
	}

	std::vector<std::string_view>& split_pipe_cells(std::vector<std::string_view>& cells,
	                                                       const std::string_view line)
	{
		cells.clear();
		const auto len = static_cast<int>(line.size());
		int pos = 0;

		// skip leading whitespace
		while (pos < len && line[pos] == u8' ') pos++;
		// skip leading pipe
		if (pos < len && line[pos] == u8'|') pos++;

		while (pos < len)
		{
			const auto next = line.find(u8'|', pos);
			if (next == std::string_view::npos)
			{
				const auto tail = trim_cell(line.substr(pos));
				if (!tail.empty()) cells.push_back(line.substr(pos, len - pos));
				break;
			}
			cells.push_back(line.substr(pos, next - pos));
			pos = static_cast<int>(next) + 1;
		}
		return cells;
	}

	std::vector<std::string_view>& split_csv_cells(std::vector<std::string_view>& cells,
	                                                      const std::string_view line)
	{
		cells.clear();
		const auto len = static_cast<int>(line.size());
		int pos = 0;

		while (pos < len)
		{
			if (line[pos] == u8'"')
			{
				// Quoted field: find matching close quote
				const auto quote_start = pos + 1;
				int end = quote_start;
				while (end < len)
				{
					if (line[end] == u8'"')
					{
						if (end + 1 < len && line[end + 1] == u8'"')
						{
							end += 2; // escaped quote
							continue;
						}
						break;
					}
					end++;
				}
				cells.push_back(line.substr(quote_start, end - quote_start));
				pos = (end < len) ? end + 1 : end; // skip closing quote
				if (pos < len && line[pos] == u8',') pos++; // skip delimiter
			}
			else
			{
				const auto next = line.find(u8',', pos);
				if (next == std::string_view::npos)
				{
					cells.push_back(line.substr(pos));
					break;
				}
				cells.push_back(line.substr(pos, next - pos));
				pos = static_cast<int>(next) + 1;
			}
		}
		return cells;
	}

	bool is_right_align_cell(const std::string_view text)
	{
		const auto trimmed = trim_cell(text);
		if (trimmed.empty()) return false;

		const auto len = static_cast<int>(trimmed.size());
		int pos = 0;

		// optional leading sign
		if (pos < len && (trimmed[pos] == u8'+' || trimmed[pos] == u8'-')) pos++;

		// optional currency symbol
		if (pos < len && trimmed[pos] == u8'$') pos++;
		else if (pos + 2 < len && static_cast<uint8_t>(trimmed[pos]) == 0xE2
			&& static_cast<uint8_t>(trimmed[pos + 1]) == 0x82
			&& static_cast<uint8_t>(trimmed[pos + 2]) == 0xAC)
			pos += 3; // EUR
		else if (pos + 1 < len && static_cast<uint8_t>(trimmed[pos]) == 0xC2
			&& static_cast<uint8_t>(trimmed[pos + 1]) == 0xA3)
			pos += 2; // GBP

		bool has_digit = false;
		while (pos < len)
		{
			const auto ch = trimmed[pos];
			if (ch >= u8'0' && ch <= u8'9')
			{
				has_digit = true;
				pos++;
			}
			else if (ch == u8',' || ch == u8'.' || ch == u8' ') pos++;
			else if (ch == u8'%' && pos == len - 1) pos++;
			else return false;
		}
		return has_digit;
	}

	void cap_col_widths(std::vector<int>& col_widths, const int avail_cols)
	{
		if (col_widths.empty()) return;

		const auto num_cols = static_cast<int>(col_widths.size());
		const auto overhead = 1 + num_cols * 3; // leading pipe + per-col: space+space+pipe
		const auto budget = std::max(num_cols, avail_cols - overhead);

		int total_natural = 0;
		for (const auto w : col_widths) total_natural += w;

		if (total_natural > budget && total_natural > 0)
		{
			for (auto& w : col_widths)
				w = std::max(1, w * budget / total_natural);
		}
	}

}
