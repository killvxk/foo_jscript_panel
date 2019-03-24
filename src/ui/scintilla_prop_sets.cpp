#include "stdafx.h"
#include "helpers.h"
#include "scintilla_prop_sets.h"

const t_prop_set_init_table prop_sets_init_table[] =
{
	{"style.default", "font:Courier New,size:10"},
	{"style.comment", "fore:#008000"},
	{"style.keyword", "bold,fore:#0000ff"},
	{"style.indentifier", "$(style.default)"},
	{"style.string", "fore:#ff0000"},
	{"style.number", "fore:#ff0000"},
	{"style.operator", "$(style.default)"},
	{"style.linenumber", "font:Courier New,size:8,fore:#2b91af"},
	{"style.bracelight", "bold,fore:#000000,back:#ffee62"},
	{"style.bracebad", "bold,fore:#ff0000"},
	{"style.selection.fore", ""},
	{"style.selection.back", ""},
	{"style.selection.alpha", "256"},
	{"style.caret.fore", ""},
	{"style.caret.width", "1"},
	{"style.caret.line.back", ""},
	{"style.caret.line.back.alpha", "256"},
	{0, 0},
};

cfg_sci_prop_sets g_sci_prop_sets(jsp_guids::prop_sets, prop_sets_init_table);

cfg_sci_prop_sets::cfg_sci_prop_sets(const GUID& p_guid, const t_prop_set_init_table* p_default) : cfg_var(p_guid)
{
	init_data(p_default);
}

void cfg_sci_prop_sets::export_to_file(const char* filename)
{
	pfc::string8_fast content = "# Generated by " JSP_NAME "\r\n";

	for (t_size i = 0; i < m_data.get_count(); ++i)
	{
		content << m_data[i].key << "=" << m_data[i].val << "\r\n";
	}

	helpers::write_file(filename, content);
}

void cfg_sci_prop_sets::get_data_raw(stream_writer* p_stream, abort_callback& p_abort)
{
	try
	{
		p_stream->write_lendian_t(m_data.get_count(), p_abort);
		for (t_size i = 0; i < m_data.get_count(); ++i)
		{
			p_stream->write_string(m_data[i].key, p_abort);
			p_stream->write_string(m_data[i].val, p_abort);
		}
	}
	catch (...) {}
}

void cfg_sci_prop_sets::import(const char* content)
{
	using t_string_chain_list = pfc::chain_list_v2_t<pfc::string8_fast>;
	t_string_chain_list lines;
	pfc::splitStringByLines(lines, content);

	pfc::string8_fast key;
	t_str_to_str_map data_map;

	for (t_string_chain_list::iterator iter = lines.first(); iter != lines.last(); ++iter)
	{
		const int len = iter->get_length();
		if (len > 0)
		{
			const char* ptr = iter->get_ptr();

			if (*ptr == '#' || len <= 3)
			{
				continue;
			}

			const char* delim = strchr(ptr, '=');

			if ((!delim) || (delim - ptr + 1 > len))
			{
				continue;
			}

			if (delim > ptr)
			{
				key.set_string(ptr, delim - ptr);
				data_map[key].set_string(delim + 1);
			}
		}
	}

	merge_data(data_map);
}

void cfg_sci_prop_sets::init_data(const t_prop_set_init_table* p_default)
{
	t_sci_prop_set temp;

	m_data.remove_all();

	for (t_size i = 0; p_default[i].key; ++i)
	{
		temp.key = p_default[i].key;
		temp.defaultval = p_default[i].defaultval;
		temp.val = temp.defaultval;
		m_data.add_item(temp);
	}
}

void cfg_sci_prop_sets::merge_data(const t_str_to_str_map& data_map)
{
	pfc::string8_fast val;

	for (t_size i = 0; i < m_data.get_count(); ++i)
	{
		if (data_map.query(m_data[i].key, val))
		{
			m_data[i].val = val;
		}
	}
}

void cfg_sci_prop_sets::reset()
{
	for (t_size i = 0; i < m_data.get_count(); ++i)
	{
		m_data[i].val = m_data[i].defaultval;
	}
}

void cfg_sci_prop_sets::set_data_raw(stream_reader* p_stream, t_size p_sizehint, abort_callback& p_abort)
{
	t_str_to_str_map data_map;
	pfc::string8_fast key, val;
	t_size count;

	try
	{
		p_stream->read_lendian_t(count, p_abort);
		for (t_size i = 0; i < count; ++i)
		{
			p_stream->read_string(key, p_abort);
			p_stream->read_string(val, p_abort);
			data_map[key] = val;
		}
	}
	catch (...)
	{
		init_data(prop_sets_init_table);
		return;
	}
	merge_data(data_map);
}
