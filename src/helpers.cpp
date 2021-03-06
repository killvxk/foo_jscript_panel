#include "stdafx.h"
#include "helpers.h"

#include <MLang.h>

namespace helpers
{
	COLORREF convert_argb_to_colorref(DWORD argb)
	{
		return RGB(argb >> RED_SHIFT, argb >> GREEN_SHIFT, argb >> BLUE_SHIFT);
	}

	DWORD convert_colorref_to_argb(COLORREF color)
	{
		return GetRValue(color) << RED_SHIFT | GetGValue(color) << GREEN_SHIFT | GetBValue(color) << BLUE_SHIFT | 0xff000000;
	}

	IGdiBitmap* get_album_art(const metadb_handle_ptr& handle, t_size art_id, bool need_stub, pfc::string_base& image_path, bool no_load)
	{
		const GUID what = convert_artid_to_guid(art_id);
		abort_callback_dummy abort;
		auto api = album_art_manager_v2::get();
		album_art_extractor_instance_v2::ptr ptr;
		album_art_data_ptr data;
		Gdiplus::Bitmap* bitmap = nullptr;
		IGdiBitmap* ret = nullptr;

		try
		{
			ptr = api->open(pfc::list_single_ref_t<metadb_handle_ptr>(handle), pfc::list_single_ref_t<GUID>(what), abort);
			data = ptr->query(what, abort);
		}
		catch (...)
		{
			if (need_stub)
			{
				try
				{
					ptr = api->open_stub(abort);
					data = ptr->query(what, abort);
				}
				catch (...) {}
			}
		}

		if (data.is_valid())
		{
			if (!no_load && read_album_art_into_bitmap(data, &bitmap))
			{
				ret = new com_object_impl_t<GdiBitmap>(bitmap);
			}
			album_art_path_list::ptr pathlist = ptr->query_paths(what, abort);
			if (pathlist->get_count() > 0)
			{
				image_path.set_string(file_path_display(pathlist->get_path(0)));
			}
		}
		return ret;
	}

	IGdiBitmap* get_album_art_embedded(const pfc::string8_fast& rawpath, t_size art_id)
	{
		IGdiBitmap* ret = nullptr;

		album_art_extractor::ptr ptr;
		if (album_art_extractor::g_get_interface(ptr, rawpath))
		{
			album_art_extractor_instance_ptr aaep;
			const GUID what = convert_artid_to_guid(art_id);
			abort_callback_dummy abort;

			try
			{
				aaep = ptr->open(nullptr, rawpath, abort);

				album_art_data_ptr data = aaep->query(what, abort);
				Gdiplus::Bitmap* bitmap = nullptr;

				if (read_album_art_into_bitmap(data, &bitmap))
				{
					ret = new com_object_impl_t<GdiBitmap>(bitmap);
				}
			}
			catch (...) {}
		}
		return ret;
	}

	IGdiBitmap* load_image(BSTR path)
	{
		IGdiBitmap* ret = nullptr;
		IStreamPtr pStream;
		if (SUCCEEDED(SHCreateStreamOnFileEx(path, STGM_READ | STGM_SHARE_DENY_WRITE, GENERIC_READ, FALSE, nullptr, &pStream)))
		{
			auto img = new Gdiplus::Bitmap(pStream, TRUE);
			if (helpers::ensure_gdiplus_object(img))
			{
				ret = new com_object_impl_t<GdiBitmap>(img);
			}
			else
			{
				if (img) delete img;
				img = nullptr;
			}
		}
		return ret;
	}

	bool execute_context_command_by_name(const char* p_command, metadb_handle_list_cref p_handles, t_size flags)
	{
		contextmenu_manager::ptr cm;
		contextmenu_manager::g_create(cm);
		pfc::string8_fast path;

		if (p_handles.get_count())
		{
			cm->init_context(p_handles, flags);
		}
		else
		{
			if (!cm->init_context_now_playing(flags))
			{
				return false;
			}
		}

		return execute_context_command_recur(p_command, path, cm->get_root());
	}

	bool execute_context_command_recur(const char* p_command, pfc::string_base& p_path, contextmenu_node* p_parent)
	{
		for (t_size i = 0; i < p_parent->get_num_children(); ++i)
		{
			contextmenu_node* child = p_parent->get_child(i);
			pfc::string8_fast path = p_path;
			path << child->get_name();

			switch (child->get_type())
			{
			case contextmenu_item_node::type_group:
				path.add_char('/');
				if (execute_context_command_recur(p_command, path, child))
				{
					return true;
				}
				break;
			case contextmenu_item_node::type_command:
				if (_stricmp(p_command, path) == 0)
				{
					child->execute();
					return true;
				}
				break;
			}
		}
		return false;
	}

	bool execute_mainmenu_command_by_name(const char* p_command)
	{
		pfc::map_t<GUID, mainmenu_group::ptr> group_guid_map;

		{
			service_enum_t<mainmenu_group> e;
			mainmenu_group::ptr ptr;

			while (e.next(ptr))
			{
				GUID guid = ptr->get_guid();
				group_guid_map.find_or_add(guid) = ptr;
			}
		}

		service_enum_t<mainmenu_commands> e;
		mainmenu_commands::ptr ptr;

		while (e.next(ptr))
		{
			for (t_size i = 0; i < ptr->get_command_count(); ++i)
			{
				pfc::string8_fast path;

				GUID parent = ptr->get_parent();
				while (parent != pfc::guid_null)
				{
					mainmenu_group::ptr group_ptr = group_guid_map[parent];
					mainmenu_group_popup::ptr group_popup_ptr;

					if (group_ptr->cast(group_popup_ptr))
					{
						pfc::string8_fast temp;
						group_popup_ptr->get_display_string(temp);
						if (temp.get_length())
						{
							temp.add_char('/');
							temp.add_string(path);
							path = temp;
						}
					}
					parent = group_ptr->get_parent();
				}

				mainmenu_commands_v2::ptr v2_ptr;
				if (ptr->cast(v2_ptr) && v2_ptr->is_command_dynamic(i))
				{
					mainmenu_node::ptr node = v2_ptr->dynamic_instantiate(i);
					if (execute_mainmenu_command_recur(p_command, path, node))
					{
						return true;
					}
				}
				else
				{
					pfc::string8_fast command;
					ptr->get_name(i, command);
					path.add_string(command);
					if (_stricmp(p_command, path) == 0)
					{
						ptr->execute(i, nullptr);
						return true;
					}
				}
			}
		}
		return false;
	}

	bool execute_mainmenu_command_recur(const char* p_command, pfc::string8_fast path, mainmenu_node::ptr node)
	{
		pfc::string8_fast text;
		t_size flags;
		node->get_display(text, flags);
		path += text;

		switch (node->get_type())
		{
		case mainmenu_node::type_group:
			if (text.get_length())
			{
				path.add_char('/');
			}
			for (t_size i = 0; i < node->get_children_count(); ++i)
			{
				mainmenu_node::ptr child = node->get_child(i);
				if (execute_mainmenu_command_recur(p_command, path, child))
				{
					return true;
				}
			}
			break;
		case mainmenu_node::type_command:
			if (_stricmp(p_command, path) == 0)
			{
				node->execute(nullptr);
				return true;
			}
			break;
		}
		return false;
	}

	bool read_album_art_into_bitmap(const album_art_data_ptr& data, Gdiplus::Bitmap** bitmap)
	{
		*bitmap = nullptr;
		bool ret = false;

		if (!data.is_valid())
			return ret;

		IStreamPtr is;
		if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &is)) && is)
		{
			ULONG bytes_written = 0;
			if (SUCCEEDED(is->Write(data->get_ptr(), data->get_size(), &bytes_written)) && bytes_written == data->get_size())
			{
				auto bmp = new Gdiplus::Bitmap(is, TRUE);

				if (ensure_gdiplus_object(bmp))
				{
					*bitmap = bmp;
					ret = true;
				}
				else
				{
					if (bmp) delete bmp;
					bmp = nullptr;
				}
			}
		}
		return ret;
	}

	bool read_file_wide(t_size codepage, const wchar_t* path, pfc::array_t<wchar_t>& content)
	{
		HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		HANDLE hFileMapping = CreateFileMapping(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);

		if (hFileMapping == nullptr)
		{
			CloseHandle(hFile);
			return false;
		}

		DWORD dwFileSize = GetFileSize(hFile, nullptr);
		LPCBYTE pAddr = (LPCBYTE)MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);

		if (pAddr == nullptr)
		{
			CloseHandle(hFileMapping);
			CloseHandle(hFile);
			return false;
		}

		if (dwFileSize == INVALID_FILE_SIZE)
		{
			UnmapViewOfFile(pAddr);
			CloseHandle(hFileMapping);
			CloseHandle(hFile);
			return false;
		}

		if (dwFileSize >=2 && pAddr[0] == 0xFF && pAddr[1] == 0xFE) // UTF16 LE?
		{
			const wchar_t* pSource = (const wchar_t*)(pAddr + 2);
			t_size len = (dwFileSize - 2) >> 1;

			content.set_size(len + 1);
			pfc::__unsafe__memcpy_t(content.get_ptr(), pSource, len);
			content[len] = 0;
		}
		else if (dwFileSize >= 3 && pAddr[0] == 0xEF && pAddr[1] == 0xBB && pAddr[2] == 0xBF) // UTF8-BOM?
		{
			const char* pSource = (const char*)(pAddr + 3);
			t_size pSourceSize = dwFileSize - 3;

			const t_size size = estimate_utf8_to_wide_quick(pSource, pSourceSize);
			content.set_size(size);
			convert_utf8_to_wide(content.get_ptr(), size, pSource, pSourceSize);
		}
		else
		{
			const char* pSource = (const char*)(pAddr);
			t_size pSourceSize = dwFileSize;

			if (pfc::is_valid_utf8(pSource))
			{
				const t_size size = estimate_utf8_to_wide_quick(pSource, pSourceSize);
				content.set_size(size);
				convert_utf8_to_wide(content.get_ptr(), size, pSource, pSourceSize);
			}
			else
			{
				const t_size size = estimate_codepage_to_wide(codepage, pSource, pSourceSize);
				content.set_size(size);
				convert_codepage_to_wide(codepage, content.get_ptr(), size, pSource, pSourceSize);
			}
		}

		UnmapViewOfFile(pAddr);
		CloseHandle(hFileMapping);
		CloseHandle(hFile);
		return true;
	}

	bool supports_chakra()
	{
		HKEY hKey;
		return RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{16d51579-a30b-4c8b-a276-0ff4dc41e755}", 0, KEY_READ, &hKey) == ERROR_SUCCESS;
	}

	bool write_file(const pfc::string8_fast& path, const pfc::string8_fast& content, bool write_bom)
	{
		t_size offset = write_bom ? 3 : 0;
		HANDLE hFile = uCreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		HANDLE hFileMapping = uCreateFileMapping(hFile, nullptr, PAGE_READWRITE, 0, content.get_length() + offset, nullptr);

		if (hFileMapping == nullptr)
		{
			CloseHandle(hFile);
			if (content.get_length() + offset == 0 && uFileExists(path)) return true; // suppress errors for empty files w/o BOM
			return false;
		}

		PBYTE pAddr = (PBYTE)MapViewOfFile(hFileMapping, FILE_MAP_WRITE, 0, 0, 0);

		if (pAddr == nullptr)
		{
			CloseHandle(hFileMapping);
			CloseHandle(hFile);
			return false;
		}

		if (write_bom)
		{
			const BYTE utf8_bom[] = { 0xef, 0xbb, 0xbf };
			memcpy(pAddr, utf8_bom, 3);
		}
		memcpy(pAddr + offset, content, content.get_length());

		UnmapViewOfFile(pAddr);
		CloseHandle(hFileMapping);
		CloseHandle(hFile);
		return true;
	}

	const GUID convert_artid_to_guid(t_size art_id)
	{
		const GUID* guids[] = {
			&album_art_ids::cover_front,
			&album_art_ids::cover_back,
			&album_art_ids::disc,
			&album_art_ids::icon,
			&album_art_ids::artist,
		};

		if (art_id < _countof(guids))
		{
			return *guids[art_id];
		}
		return *guids[0];
	}

	int get_encoder_clsid(const wchar_t* format, CLSID* pClsid)
	{
		int ret = -1;
		t_size num = 0;
		t_size size = 0;

		Gdiplus::GetImageEncodersSize(&num, &size);
		if (size == 0) return ret;

		auto pImageCodecInfo = new Gdiplus::ImageCodecInfo[size];
		if (pImageCodecInfo == nullptr) return ret;

		Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

		for (t_size i = 0; i < num; ++i)
		{
			if (wcscmp(pImageCodecInfo[i].MimeType, format) == 0)
			{
				*pClsid = pImageCodecInfo[i].Clsid;
				ret = i;
				break;
			}
		}

		delete[] pImageCodecInfo;
		return ret;
	}

	int get_text_height(HDC hdc, const wchar_t* text, int len)
	{
		SIZE size;
		GetTextExtentPoint32(hdc, text, len, &size);
		return size.cy;
	}

	int get_text_width(HDC hdc, const wchar_t* text, int len)
	{
		SIZE size;
		GetTextExtentPoint32(hdc, text, len, &size);
		return size.cx;
	}

	int is_wrap_char(wchar_t current, wchar_t next)
	{
		if (iswpunct(current))
			return false;

		if (next == '\0')
			return true;

		if (iswspace(current))
			return true;

		int currentAlphaNum = iswalnum(current);

		if (currentAlphaNum)
		{
			if (iswpunct(next))
				return false;
		}

		return currentAlphaNum == 0 || iswalnum(next) == 0;
	}

	pfc::string8_fast get_fb2k_component_path()
	{
		pfc::string8_fast path;
		uGetModuleFileName(core_api::get_my_instance(), path);
		path = pfc::string_directory(path);
		path.add_char('\\');
		return path;
	}

	pfc::string8_fast get_fb2k_path()
	{
		pfc::string8_fast path;
		uGetModuleFileName(nullptr, path);
		path = pfc::string_directory(path);
		path.add_char('\\');
		return path;
	}

	pfc::string8_fast get_profile_path()
	{
		pfc::string8_fast path = file_path_display(core_api::get_profile_path()).get_ptr();
		path.add_char('\\');
		return path;
	}

	pfc::string8_fast iterator_to_string(json::iterator j)
	{
		std::string value = j.value().type() == json::value_t::string ? j.value().get<std::string>() : j.value().dump();
		return value.c_str();
	}

	pfc::string8_fast read_file(const char* path)
	{
		pfc::string8_fast content;
		HANDLE hFile = uCreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			return content;
		}

		HANDLE hFileMapping = uCreateFileMapping(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);

		if (hFileMapping == nullptr)
		{
			CloseHandle(hFile);
			return content;
		}

		DWORD dwFileSize = GetFileSize(hFile, nullptr);
		LPCBYTE pAddr = (LPCBYTE)MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);

		if (pAddr == nullptr)
		{
			CloseHandle(hFileMapping);
			CloseHandle(hFile);
			return content;
		}

		if (dwFileSize == INVALID_FILE_SIZE)
		{
			UnmapViewOfFile(pAddr);
			CloseHandle(hFileMapping);
			CloseHandle(hFile);
			return content;
		}

		t_size offset = dwFileSize >= 3 && pAddr[0] == 0xEF && pAddr[1] == 0xBB && pAddr[2] == 0xBF ? 3 : 0;
		const char* pSource = (const char*)(pAddr + offset);
		content.set_string(pSource);

		UnmapViewOfFile(pAddr);
		CloseHandle(hFileMapping);
		CloseHandle(hFile);
		return content;
	}

	t_size detect_charset(const char* fileName)
	{
		pfc::string8_fast text;
		int textSize = 0;

		try
		{
			file_ptr io;
			abort_callback_dummy abort;
			filesystem::g_open_read(io, fileName, abort);
			io->read_string_raw(text, abort);
			textSize = text.get_length();
			if (pfc::is_valid_utf8(text)) return 65001;
		}
		catch (...)
		{
			return 0;
		}

		const int maxEncodings = 2;
		int encodingCount = maxEncodings;
		DetectEncodingInfo encodings[maxEncodings];

		_COM_SMARTPTR_TYPEDEF(IMultiLanguage2, IID_IMultiLanguage2);
		IMultiLanguage2Ptr lang;
		if (FAILED(lang.CreateInstance(CLSID_CMultiLanguage, nullptr, CLSCTX_INPROC_SERVER))) return 0;
		if (FAILED(lang->DetectInputCodepage(MLDETECTCP_NONE, 0, const_cast<char*>(text.get_ptr()), &textSize, encodings, &encodingCount))) return 0;

		t_size codepage = encodings[0].nCodePage;

		if (encodingCount == 2 && encodings[0].nCodePage == 1252)
		{
			switch (encodings[1].nCodePage)
			{
			case 932: // shift-jis
			case 936: // gbk
			case 949: // korean
			case 950: // big5
				codepage = encodings[1].nCodePage;
				break;
			}
		}

		// ASCII?
		if (codepage == 20127)
		{
			codepage = 0;
		}

		return codepage;
	}

	void estimate_line_wrap(HDC hdc, const wchar_t* text, int len, int width, pfc::list_t<wrapped_item>& out)
	{
		for (;;)
		{
			const wchar_t* next = wcschr(text, '\n');
			if (next == nullptr)
			{
				estimate_line_wrap_recur(hdc, text, wcslen(text), width, out);
				break;
			}

			const wchar_t* walk = next;

			while (walk > text && walk[-1] == '\r')
			{
				--walk;
			}

			estimate_line_wrap_recur(hdc, text, walk - text, width, out);
			text = next + 1;
		}
	}

	void estimate_line_wrap_recur(HDC hdc, const wchar_t* text, int len, int width, pfc::list_t<wrapped_item>& out)
	{
		int textLength = len;
		int textWidth = get_text_width(hdc, text, len);

		if (textWidth <= width || len <= 1)
		{
			wrapped_item item =
			{
				SysAllocStringLen(text, len),
				textWidth
			};
			out.add_item(item);
		}
		else
		{
			textLength = (len * width) / textWidth;

			if (get_text_width(hdc, text, textLength) < width)
			{
				while (get_text_width(hdc, text, min(len, textLength + 1)) <= width)
				{
					++textLength;
				}
			}
			else
			{
				while (get_text_width(hdc, text, textLength) > width && textLength > 1)
				{
					--textLength;
				}
			}

			{
				int fallbackTextLength = max(textLength, 1);

				while (textLength > 0 && !is_wrap_char(text[textLength - 1], text[textLength]))
				{
					--textLength;
				}

				if (textLength == 0)
				{
					textLength = fallbackTextLength;
				}

				wrapped_item item =
				{
					SysAllocStringLen(text, textLength),
					get_text_width(hdc, text, textLength)
				};
				out.add_item(item);
			}

			if (textLength < len)
			{
				estimate_line_wrap_recur(hdc, text + textLength, len - textLength, width, out);
			}
		}
	}

	void list(const char* path, bool files, bool recur, pfc::string_list_impl& out)
	{
		abort_callback_dummy abort;
		pfc::string8_fast folder;
		filesystem::g_get_canonical_path(path, folder);

		try
		{
			if (files)
			{
				if (recur)
				{
					foobar2000_io::listFilesRecur(folder, out, abort);
				}
				else
				{
					foobar2000_io::listFiles(folder, out, abort);
				}
			}
			else
			{
				foobar2000_io::listDirectories(folder, out, abort);
			}
		}
		catch (...) {}
	}

	wchar_t* make_sort_string(const char* in)
	{
		wchar_t* out = new wchar_t[estimate_utf8_to_wide(in) + 1];
		out[0] = ' '; // StrCmpLogicalW bug workaround.
		convert_utf8_to_wide_unchecked(out + 1, in);
		return out;
	}
}
