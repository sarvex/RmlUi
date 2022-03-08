/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "RmlUi_Platform_Win32.h"
#include "RmlUi_Include_Windows.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/SystemInterface.h>
#include <string.h>

static bool ProcessInputEvent(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
static void InitialiseKeymap();

static constexpr int KEYMAP_SIZE = 256;
static Rml::Input::KeyIdentifier key_identifier_map[KEYMAP_SIZE];
static Rml::Context* context = nullptr;

static HWND window_handle = nullptr;
static HINSTANCE instance_handle = nullptr;
static std::wstring instance_name;

static int window_width = 0;
static int window_height = 0;
static bool has_dpi_support = false;
static UINT window_dpi = USER_DEFAULT_SCREEN_DPI;

static double time_frequency;
static LARGE_INTEGER time_startup;

static HCURSOR cursor_default = nullptr;
static HCURSOR cursor_move = nullptr;
static HCURSOR cursor_pointer = nullptr;
static HCURSOR cursor_resize = nullptr;
static HCURSOR cursor_cross = nullptr;
static HCURSOR cursor_text = nullptr;
static HCURSOR cursor_unavailable = nullptr;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
	#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif
#ifndef WM_DPICHANGED
	#define WM_DPICHANGED 0x02E0
#endif

// Declare pointers to the DPI aware Windows API functions.
using ProcSetProcessDpiAwarenessContext = BOOL(WINAPI*)(HANDLE value);
using ProcGetDpiForWindow = UINT(WINAPI*)(HWND hwnd);
using ProcAdjustWindowRectExForDpi = BOOL(WINAPI*)(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi);

static ProcSetProcessDpiAwarenessContext procSetProcessDpiAwarenessContext = NULL;
static ProcGetDpiForWindow procGetDpiForWindow = NULL;
static ProcAdjustWindowRectExForDpi procAdjustWindowRectExForDpi = NULL;

static void UpdateWindowDpi()
{
	if (has_dpi_support)
	{
		UINT dpi = procGetDpiForWindow(window_handle);
		if (dpi != 0)
			window_dpi = dpi;
	}
}

static void UpdateWindowDimensions(int width = 0, int height = 0)
{
	if (width > 0)
		window_width = width;
	if (height > 0)
		window_height = height;

	if (context)
		context->SetDimensions(Rml::Vector2i(window_width, window_height));
}

static void SetContextDpRatio()
{
	if (context && has_dpi_support)
		context->SetDensityIndependentPixelRatio(RmlWin32::GetDensityIndependentPixelRatio());
}

static Rml::String ConvertToUTF8(const std::wstring& wstr)
{
	const int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), NULL, 0, NULL, NULL);
	Rml::String str(count, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, NULL, NULL);
	return str;
}

static std::wstring ConvertToUTF16(const Rml::String& str)
{
	const int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
	std::wstring wstr(count, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], count);
	return wstr;
}

double SystemInterface_Win32::GetElapsedTime()
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);

	return double(counter.QuadPart - time_startup.QuadPart) * time_frequency;
}

void SystemInterface_Win32::SetMouseCursor(const Rml::String& cursor_name)
{
	if (window_handle)
	{
		HCURSOR cursor_handle = nullptr;
		if (cursor_name.empty() || cursor_name == "arrow")
			cursor_handle = cursor_default;
		else if (cursor_name == "move")
			cursor_handle = cursor_move;
		else if (cursor_name == "pointer")
			cursor_handle = cursor_pointer;
		else if (cursor_name == "resize")
			cursor_handle = cursor_resize;
		else if (cursor_name == "cross")
			cursor_handle = cursor_cross;
		else if (cursor_name == "text")
			cursor_handle = cursor_text;
		else if (cursor_name == "unavailable")
			cursor_handle = cursor_unavailable;

		if (cursor_handle)
		{
			SetCursor(cursor_handle);
			SetClassLongPtrA(window_handle, GCLP_HCURSOR, (LONG_PTR)cursor_handle);
		}
	}
}

void SystemInterface_Win32::SetClipboardText(const Rml::String& text_utf8)
{
	if (window_handle)
	{
		if (!OpenClipboard(window_handle))
			return;

		EmptyClipboard();

		const std::wstring text = ConvertToUTF16(text_utf8);
		const size_t size = sizeof(wchar_t) * (text.size() + 1);

		HGLOBAL clipboard_data = GlobalAlloc(GMEM_FIXED, size);
		memcpy(clipboard_data, text.data(), size);

		if (SetClipboardData(CF_UNICODETEXT, clipboard_data) == nullptr)
		{
			CloseClipboard();
			GlobalFree(clipboard_data);
		}
		else
			CloseClipboard();
	}
}

void SystemInterface_Win32::GetClipboardText(Rml::String& text)
{
	if (window_handle)
	{
		if (!OpenClipboard(window_handle))
			return;

		HANDLE clipboard_data = GetClipboardData(CF_UNICODETEXT);
		if (clipboard_data == nullptr)
		{
			CloseClipboard();
			return;
		}

		const wchar_t* clipboard_text = (const wchar_t*)GlobalLock(clipboard_data);
		if (clipboard_text)
			text = ConvertToUTF8(clipboard_text);
		GlobalUnlock(clipboard_data);

		CloseClipboard();
	}
}

bool RmlWin32::Initialize()
{
	instance_handle = GetModuleHandle(nullptr);
	InitialiseKeymap();

	LARGE_INTEGER time_ticks_per_second;
	QueryPerformanceFrequency(&time_ticks_per_second);
	QueryPerformanceCounter(&time_startup);

	time_frequency = 1.0 / (double)time_ticks_per_second.QuadPart;

	// Load cursors
	cursor_default = LoadCursor(nullptr, IDC_ARROW);
	cursor_move = LoadCursor(nullptr, IDC_SIZEALL);
	cursor_pointer = LoadCursor(nullptr, IDC_HAND);
	cursor_resize = LoadCursor(nullptr, IDC_SIZENWSE);
	cursor_cross = LoadCursor(nullptr, IDC_CROSS);
	cursor_text = LoadCursor(nullptr, IDC_IBEAM);
	cursor_unavailable = LoadCursor(nullptr, IDC_NO);

	// See if we have Per Monitor V2 DPI awareness. Requires Windows 10, version 1703.
	// Cast function pointers to void* first for MinGW not to emit errors.
	procSetProcessDpiAwarenessContext =
		(ProcSetProcessDpiAwarenessContext)(void*)GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "SetProcessDpiAwarenessContext");
	procGetDpiForWindow = (ProcGetDpiForWindow)(void*)GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "GetDpiForWindow");
	procAdjustWindowRectExForDpi =
		(ProcAdjustWindowRectExForDpi)(void*)GetProcAddress(GetModuleHandle(TEXT("User32.dll")), "AdjustWindowRectExForDpi");

	if (!has_dpi_support && procSetProcessDpiAwarenessContext != NULL && procGetDpiForWindow != NULL && procAdjustWindowRectExForDpi != NULL)
	{
		// Activate Per Monitor V2.
		if (procSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
			has_dpi_support = true;
	}

	return true;
}

void RmlWin32::Shutdown() {}

bool RmlWin32::InitializeWindow(const char* in_name, int& inout_width, int& inout_height, bool allow_resize, HWND& out_window_handle,
	WNDPROC func_window_procedure)
{
	const std::wstring name = ConvertToUTF16(Rml::String(in_name));

	// Fill out the window class struct.
	WNDCLASSW window_class;
	window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	window_class.lpfnWndProc = func_window_procedure;
	window_class.cbClsExtra = 0;
	window_class.cbWndExtra = 0;
	window_class.hInstance = instance_handle;
	window_class.hIcon = LoadIcon(nullptr, IDI_WINLOGO);
	window_class.hCursor = cursor_default;
	window_class.hbrBackground = nullptr;
	window_class.lpszMenuName = nullptr;
	window_class.lpszClassName = name.data();

	if (!RegisterClassW(&window_class))
	{
		RmlWin32::DisplayError("Could not register window class.");
		RmlWin32::CloseWindow();
		return false;
	}

	window_handle = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE,
		name.data(),                                                                // Window class name.
		name.data(), WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW, 0, 0, // Window position.
		0, 0,                                                                       // Window size.
		nullptr, nullptr, instance_handle, nullptr);

	if (!window_handle)
	{
		RmlWin32::DisplayError("Could not create window.");
		RmlWin32::CloseWindow();
		return false;
	}

	UpdateWindowDpi();
	inout_width = (inout_width * (int)window_dpi) / USER_DEFAULT_SCREEN_DPI;
	inout_height = (inout_height * (int)window_dpi) / USER_DEFAULT_SCREEN_DPI;

	instance_name = name;

	DWORD style = (allow_resize ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_MAXIMIZEBOX));
	DWORD extended_style = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

	// Adjust the window size to take into account the edges
	RECT window_rect;
	window_rect.top = 0;
	window_rect.left = 0;
	window_rect.right = inout_width;
	window_rect.bottom = inout_height;
	if (has_dpi_support)
		procAdjustWindowRectExForDpi(&window_rect, style, FALSE, extended_style, window_dpi);
	else
		AdjustWindowRectEx(&window_rect, style, FALSE, extended_style);

	SetWindowLong(window_handle, GWL_EXSTYLE, extended_style);
	SetWindowLong(window_handle, GWL_STYLE, style);

	// Resize the window.
	SetWindowPos(window_handle, HWND_TOP, 0, 0, window_rect.right - window_rect.left, window_rect.bottom - window_rect.top, SWP_NOACTIVATE);

	out_window_handle = window_handle;

	return true;
}

void RmlWin32::ShowWindow()
{
	ShowWindow(window_handle, SW_SHOW);
	SetForegroundWindow(window_handle);
	SetFocus(window_handle);
}

void RmlWin32::CloseWindow()
{
	DestroyWindow(window_handle);
	UnregisterClassW((LPCWSTR)instance_name.data(), instance_handle);
	window_handle = nullptr;
}

LRESULT RmlWin32::WindowProcedure(HWND local_window_handle, UINT message, WPARAM w_param, LPARAM l_param)
{
	LRESULT result = TRUE;

	switch (message)
	{
	case WM_SIZE:
	{
		int width = LOWORD(l_param);
		int height = HIWORD(l_param);
		UpdateWindowDimensions(width, height);
		result = 0;
	}
	break;
	case WM_DPICHANGED:
	{
		UpdateWindowDpi();

		RECT* new_pos = (RECT*)l_param;
		SetWindowPos(local_window_handle, NULL, new_pos->left, new_pos->top, new_pos->right - new_pos->left, new_pos->bottom - new_pos->top,
			SWP_NOZORDER | SWP_NOACTIVATE);

		SetContextDpRatio();
		result = 0;
	}
	break;
	default:
	{
		result = ProcessInputEvent(local_window_handle, message, w_param, l_param);
	}
	break;
	}

	return result;
}

void RmlWin32::SetContext(Rml::Context* new_context)
{
	context = new_context;
	SetContextDpRatio();
	UpdateWindowDimensions();
}

float RmlWin32::GetDensityIndependentPixelRatio()
{
	return float(window_dpi) / float(USER_DEFAULT_SCREEN_DPI);
}

Rml::Input::KeyIdentifier RmlWin32::ConvertKey(int win32_key_code)
{
	if (win32_key_code >= 0 && win32_key_code < KEYMAP_SIZE)
		return key_identifier_map[win32_key_code];

	return Rml::Input::KI_UNKNOWN;
}

int RmlWin32::GetKeyModifierState()
{
	int key_modifier_state = 0;

	if (GetKeyState(VK_CAPITAL) & 1)
		key_modifier_state |= Rml::Input::KM_CAPSLOCK;

	if (HIWORD(GetKeyState(VK_SHIFT)) & 1)
		key_modifier_state |= Rml::Input::KM_SHIFT;

	if (GetKeyState(VK_NUMLOCK) & 1)
		key_modifier_state |= Rml::Input::KM_NUMLOCK;

	if (HIWORD(GetKeyState(VK_CONTROL)) & 1)
		key_modifier_state |= Rml::Input::KM_CTRL;

	if (HIWORD(GetKeyState(VK_MENU)) & 1)
		key_modifier_state |= Rml::Input::KM_ALT;

	return key_modifier_state;
}

void RmlWin32::DisplayError(const char* fmt, ...)
{
	const int buffer_size = 1024;
	char buffer[buffer_size];
	va_list argument_list;

	// Print the message to the buffer.
	va_start(argument_list, fmt);
	int len = vsnprintf(buffer, buffer_size - 2, fmt, argument_list);
	if (len < 0 || len > buffer_size - 2)
	{
		len = buffer_size - 2;
	}
	buffer[len] = '\n';
	buffer[len + 1] = '\0';
	va_end(argument_list);

	MessageBoxW(window_handle, ConvertToUTF16(buffer).c_str(), L"Shell Error", MB_OK);
}

static bool ProcessInputEvent(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
	bool result = true;

	if (!context)
		return result;

	// Process all mouse and keyboard events
	switch (message)
	{
	case WM_LBUTTONDOWN:
		result = context->ProcessMouseButtonDown(0, RmlWin32::GetKeyModifierState());
		SetCapture(window);
		break;

	case WM_LBUTTONUP:
		ReleaseCapture();
		result = context->ProcessMouseButtonUp(0, RmlWin32::GetKeyModifierState());
		break;

	case WM_RBUTTONDOWN:
		result = context->ProcessMouseButtonDown(1, RmlWin32::GetKeyModifierState());
		break;

	case WM_RBUTTONUP:
		result = context->ProcessMouseButtonUp(1, RmlWin32::GetKeyModifierState());
		break;

	case WM_MBUTTONDOWN:
		result = context->ProcessMouseButtonDown(2, RmlWin32::GetKeyModifierState());
		break;

	case WM_MBUTTONUP:
		result = context->ProcessMouseButtonUp(2, RmlWin32::GetKeyModifierState());
		break;

	case WM_MOUSEMOVE:
		result = context->ProcessMouseMove(static_cast<int>((short)LOWORD(l_param)), static_cast<int>((short)HIWORD(l_param)),
			RmlWin32::GetKeyModifierState());
		break;

	case WM_MOUSEWHEEL:
		result = context->ProcessMouseWheel(static_cast<float>((short)HIWORD(w_param)) / static_cast<float>(-WHEEL_DELTA),
			RmlWin32::GetKeyModifierState());
		break;

	case WM_KEYDOWN:
		result = context->ProcessKeyDown(RmlWin32::ConvertKey((int)w_param), RmlWin32::GetKeyModifierState());
		break;

	case WM_KEYUP:
		result = context->ProcessKeyUp(RmlWin32::ConvertKey((int)w_param), RmlWin32::GetKeyModifierState());
		break;

	case WM_CHAR:
	{
		static wchar_t first_u16_code_unit = 0;

		const wchar_t c = (wchar_t)w_param;
		Rml::Character character = (Rml::Character)c;

		// Windows sends two-wide characters as two messages.
		if (c >= 0xD800 && c < 0xDC00)
		{
			// First 16-bit code unit of a two-wide character.
			first_u16_code_unit = c;
		}
		else
		{
			if (c >= 0xDC00 && c < 0xE000 && first_u16_code_unit != 0)
			{
				// Second 16-bit code unit of a two-wide character.
				Rml::String utf8 = ConvertToUTF8(std::wstring{first_u16_code_unit, c});
				character = Rml::StringUtilities::ToCharacter(utf8.data());
			}
			else if (c == '\r')
			{
				// Windows sends new-lines as carriage returns, convert to endlines.
				character = (Rml::Character)'\n';
			}

			first_u16_code_unit = 0;

			// Only send through printable characters.
			if (((char32_t)character >= 32 || character == (Rml::Character)'\n') && character != (Rml::Character)127)
				result = context->ProcessTextInput(character);
		}
	}
	break;
	}

	return result;
}

// These are defined in winuser.h of MinGW 64 but are missing from MinGW 32
// Visual Studio has them by default
#if defined(__MINGW32__) && !defined(__MINGW64__)
	#define VK_OEM_NEC_EQUAL 0x92
	#define VK_OEM_FJ_JISHO 0x92
	#define VK_OEM_FJ_MASSHOU 0x93
	#define VK_OEM_FJ_TOUROKU 0x94
	#define VK_OEM_FJ_LOYA 0x95
	#define VK_OEM_FJ_ROYA 0x96
	#define VK_OEM_AX 0xE1
	#define VK_ICO_HELP 0xE3
	#define VK_ICO_00 0xE4
	#define VK_ICO_CLEAR 0xE6
#endif // !defined(__MINGW32__)  || defined(__MINGW64__)

static void InitialiseKeymap()
{
	// Initialise the key map with default values.
	memset(key_identifier_map, 0, sizeof(key_identifier_map));

	// Assign individual values.
	key_identifier_map['A'] = Rml::Input::KI_A;
	key_identifier_map['B'] = Rml::Input::KI_B;
	key_identifier_map['C'] = Rml::Input::KI_C;
	key_identifier_map['D'] = Rml::Input::KI_D;
	key_identifier_map['E'] = Rml::Input::KI_E;
	key_identifier_map['F'] = Rml::Input::KI_F;
	key_identifier_map['G'] = Rml::Input::KI_G;
	key_identifier_map['H'] = Rml::Input::KI_H;
	key_identifier_map['I'] = Rml::Input::KI_I;
	key_identifier_map['J'] = Rml::Input::KI_J;
	key_identifier_map['K'] = Rml::Input::KI_K;
	key_identifier_map['L'] = Rml::Input::KI_L;
	key_identifier_map['M'] = Rml::Input::KI_M;
	key_identifier_map['N'] = Rml::Input::KI_N;
	key_identifier_map['O'] = Rml::Input::KI_O;
	key_identifier_map['P'] = Rml::Input::KI_P;
	key_identifier_map['Q'] = Rml::Input::KI_Q;
	key_identifier_map['R'] = Rml::Input::KI_R;
	key_identifier_map['S'] = Rml::Input::KI_S;
	key_identifier_map['T'] = Rml::Input::KI_T;
	key_identifier_map['U'] = Rml::Input::KI_U;
	key_identifier_map['V'] = Rml::Input::KI_V;
	key_identifier_map['W'] = Rml::Input::KI_W;
	key_identifier_map['X'] = Rml::Input::KI_X;
	key_identifier_map['Y'] = Rml::Input::KI_Y;
	key_identifier_map['Z'] = Rml::Input::KI_Z;

	key_identifier_map['0'] = Rml::Input::KI_0;
	key_identifier_map['1'] = Rml::Input::KI_1;
	key_identifier_map['2'] = Rml::Input::KI_2;
	key_identifier_map['3'] = Rml::Input::KI_3;
	key_identifier_map['4'] = Rml::Input::KI_4;
	key_identifier_map['5'] = Rml::Input::KI_5;
	key_identifier_map['6'] = Rml::Input::KI_6;
	key_identifier_map['7'] = Rml::Input::KI_7;
	key_identifier_map['8'] = Rml::Input::KI_8;
	key_identifier_map['9'] = Rml::Input::KI_9;

	key_identifier_map[VK_BACK] = Rml::Input::KI_BACK;
	key_identifier_map[VK_TAB] = Rml::Input::KI_TAB;

	key_identifier_map[VK_CLEAR] = Rml::Input::KI_CLEAR;
	key_identifier_map[VK_RETURN] = Rml::Input::KI_RETURN;

	key_identifier_map[VK_PAUSE] = Rml::Input::KI_PAUSE;
	key_identifier_map[VK_CAPITAL] = Rml::Input::KI_CAPITAL;

	key_identifier_map[VK_KANA] = Rml::Input::KI_KANA;
	key_identifier_map[VK_HANGUL] = Rml::Input::KI_HANGUL;
	key_identifier_map[VK_JUNJA] = Rml::Input::KI_JUNJA;
	key_identifier_map[VK_FINAL] = Rml::Input::KI_FINAL;
	key_identifier_map[VK_HANJA] = Rml::Input::KI_HANJA;
	key_identifier_map[VK_KANJI] = Rml::Input::KI_KANJI;

	key_identifier_map[VK_ESCAPE] = Rml::Input::KI_ESCAPE;

	key_identifier_map[VK_CONVERT] = Rml::Input::KI_CONVERT;
	key_identifier_map[VK_NONCONVERT] = Rml::Input::KI_NONCONVERT;
	key_identifier_map[VK_ACCEPT] = Rml::Input::KI_ACCEPT;
	key_identifier_map[VK_MODECHANGE] = Rml::Input::KI_MODECHANGE;

	key_identifier_map[VK_SPACE] = Rml::Input::KI_SPACE;
	key_identifier_map[VK_PRIOR] = Rml::Input::KI_PRIOR;
	key_identifier_map[VK_NEXT] = Rml::Input::KI_NEXT;
	key_identifier_map[VK_END] = Rml::Input::KI_END;
	key_identifier_map[VK_HOME] = Rml::Input::KI_HOME;
	key_identifier_map[VK_LEFT] = Rml::Input::KI_LEFT;
	key_identifier_map[VK_UP] = Rml::Input::KI_UP;
	key_identifier_map[VK_RIGHT] = Rml::Input::KI_RIGHT;
	key_identifier_map[VK_DOWN] = Rml::Input::KI_DOWN;
	key_identifier_map[VK_SELECT] = Rml::Input::KI_SELECT;
	key_identifier_map[VK_PRINT] = Rml::Input::KI_PRINT;
	key_identifier_map[VK_EXECUTE] = Rml::Input::KI_EXECUTE;
	key_identifier_map[VK_SNAPSHOT] = Rml::Input::KI_SNAPSHOT;
	key_identifier_map[VK_INSERT] = Rml::Input::KI_INSERT;
	key_identifier_map[VK_DELETE] = Rml::Input::KI_DELETE;
	key_identifier_map[VK_HELP] = Rml::Input::KI_HELP;

	key_identifier_map[VK_LWIN] = Rml::Input::KI_LWIN;
	key_identifier_map[VK_RWIN] = Rml::Input::KI_RWIN;
	key_identifier_map[VK_APPS] = Rml::Input::KI_APPS;

	key_identifier_map[VK_SLEEP] = Rml::Input::KI_SLEEP;

	key_identifier_map[VK_NUMPAD0] = Rml::Input::KI_NUMPAD0;
	key_identifier_map[VK_NUMPAD1] = Rml::Input::KI_NUMPAD1;
	key_identifier_map[VK_NUMPAD2] = Rml::Input::KI_NUMPAD2;
	key_identifier_map[VK_NUMPAD3] = Rml::Input::KI_NUMPAD3;
	key_identifier_map[VK_NUMPAD4] = Rml::Input::KI_NUMPAD4;
	key_identifier_map[VK_NUMPAD5] = Rml::Input::KI_NUMPAD5;
	key_identifier_map[VK_NUMPAD6] = Rml::Input::KI_NUMPAD6;
	key_identifier_map[VK_NUMPAD7] = Rml::Input::KI_NUMPAD7;
	key_identifier_map[VK_NUMPAD8] = Rml::Input::KI_NUMPAD8;
	key_identifier_map[VK_NUMPAD9] = Rml::Input::KI_NUMPAD9;
	key_identifier_map[VK_MULTIPLY] = Rml::Input::KI_MULTIPLY;
	key_identifier_map[VK_ADD] = Rml::Input::KI_ADD;
	key_identifier_map[VK_SEPARATOR] = Rml::Input::KI_SEPARATOR;
	key_identifier_map[VK_SUBTRACT] = Rml::Input::KI_SUBTRACT;
	key_identifier_map[VK_DECIMAL] = Rml::Input::KI_DECIMAL;
	key_identifier_map[VK_DIVIDE] = Rml::Input::KI_DIVIDE;
	key_identifier_map[VK_F1] = Rml::Input::KI_F1;
	key_identifier_map[VK_F2] = Rml::Input::KI_F2;
	key_identifier_map[VK_F3] = Rml::Input::KI_F3;
	key_identifier_map[VK_F4] = Rml::Input::KI_F4;
	key_identifier_map[VK_F5] = Rml::Input::KI_F5;
	key_identifier_map[VK_F6] = Rml::Input::KI_F6;
	key_identifier_map[VK_F7] = Rml::Input::KI_F7;
	key_identifier_map[VK_F8] = Rml::Input::KI_F8;
	key_identifier_map[VK_F9] = Rml::Input::KI_F9;
	key_identifier_map[VK_F10] = Rml::Input::KI_F10;
	key_identifier_map[VK_F11] = Rml::Input::KI_F11;
	key_identifier_map[VK_F12] = Rml::Input::KI_F12;
	key_identifier_map[VK_F13] = Rml::Input::KI_F13;
	key_identifier_map[VK_F14] = Rml::Input::KI_F14;
	key_identifier_map[VK_F15] = Rml::Input::KI_F15;
	key_identifier_map[VK_F16] = Rml::Input::KI_F16;
	key_identifier_map[VK_F17] = Rml::Input::KI_F17;
	key_identifier_map[VK_F18] = Rml::Input::KI_F18;
	key_identifier_map[VK_F19] = Rml::Input::KI_F19;
	key_identifier_map[VK_F20] = Rml::Input::KI_F20;
	key_identifier_map[VK_F21] = Rml::Input::KI_F21;
	key_identifier_map[VK_F22] = Rml::Input::KI_F22;
	key_identifier_map[VK_F23] = Rml::Input::KI_F23;
	key_identifier_map[VK_F24] = Rml::Input::KI_F24;

	key_identifier_map[VK_NUMLOCK] = Rml::Input::KI_NUMLOCK;
	key_identifier_map[VK_SCROLL] = Rml::Input::KI_SCROLL;

	key_identifier_map[VK_OEM_NEC_EQUAL] = Rml::Input::KI_OEM_NEC_EQUAL;

	key_identifier_map[VK_OEM_FJ_JISHO] = Rml::Input::KI_OEM_FJ_JISHO;
	key_identifier_map[VK_OEM_FJ_MASSHOU] = Rml::Input::KI_OEM_FJ_MASSHOU;
	key_identifier_map[VK_OEM_FJ_TOUROKU] = Rml::Input::KI_OEM_FJ_TOUROKU;
	key_identifier_map[VK_OEM_FJ_LOYA] = Rml::Input::KI_OEM_FJ_LOYA;
	key_identifier_map[VK_OEM_FJ_ROYA] = Rml::Input::KI_OEM_FJ_ROYA;

	key_identifier_map[VK_SHIFT] = Rml::Input::KI_LSHIFT;
	key_identifier_map[VK_CONTROL] = Rml::Input::KI_LCONTROL;
	key_identifier_map[VK_MENU] = Rml::Input::KI_LMENU;

	key_identifier_map[VK_BROWSER_BACK] = Rml::Input::KI_BROWSER_BACK;
	key_identifier_map[VK_BROWSER_FORWARD] = Rml::Input::KI_BROWSER_FORWARD;
	key_identifier_map[VK_BROWSER_REFRESH] = Rml::Input::KI_BROWSER_REFRESH;
	key_identifier_map[VK_BROWSER_STOP] = Rml::Input::KI_BROWSER_STOP;
	key_identifier_map[VK_BROWSER_SEARCH] = Rml::Input::KI_BROWSER_SEARCH;
	key_identifier_map[VK_BROWSER_FAVORITES] = Rml::Input::KI_BROWSER_FAVORITES;
	key_identifier_map[VK_BROWSER_HOME] = Rml::Input::KI_BROWSER_HOME;

	key_identifier_map[VK_VOLUME_MUTE] = Rml::Input::KI_VOLUME_MUTE;
	key_identifier_map[VK_VOLUME_DOWN] = Rml::Input::KI_VOLUME_DOWN;
	key_identifier_map[VK_VOLUME_UP] = Rml::Input::KI_VOLUME_UP;
	key_identifier_map[VK_MEDIA_NEXT_TRACK] = Rml::Input::KI_MEDIA_NEXT_TRACK;
	key_identifier_map[VK_MEDIA_PREV_TRACK] = Rml::Input::KI_MEDIA_PREV_TRACK;
	key_identifier_map[VK_MEDIA_STOP] = Rml::Input::KI_MEDIA_STOP;
	key_identifier_map[VK_MEDIA_PLAY_PAUSE] = Rml::Input::KI_MEDIA_PLAY_PAUSE;
	key_identifier_map[VK_LAUNCH_MAIL] = Rml::Input::KI_LAUNCH_MAIL;
	key_identifier_map[VK_LAUNCH_MEDIA_SELECT] = Rml::Input::KI_LAUNCH_MEDIA_SELECT;
	key_identifier_map[VK_LAUNCH_APP1] = Rml::Input::KI_LAUNCH_APP1;
	key_identifier_map[VK_LAUNCH_APP2] = Rml::Input::KI_LAUNCH_APP2;

	key_identifier_map[VK_OEM_1] = Rml::Input::KI_OEM_1;
	key_identifier_map[VK_OEM_PLUS] = Rml::Input::KI_OEM_PLUS;
	key_identifier_map[VK_OEM_COMMA] = Rml::Input::KI_OEM_COMMA;
	key_identifier_map[VK_OEM_MINUS] = Rml::Input::KI_OEM_MINUS;
	key_identifier_map[VK_OEM_PERIOD] = Rml::Input::KI_OEM_PERIOD;
	key_identifier_map[VK_OEM_2] = Rml::Input::KI_OEM_2;
	key_identifier_map[VK_OEM_3] = Rml::Input::KI_OEM_3;

	key_identifier_map[VK_OEM_4] = Rml::Input::KI_OEM_4;
	key_identifier_map[VK_OEM_5] = Rml::Input::KI_OEM_5;
	key_identifier_map[VK_OEM_6] = Rml::Input::KI_OEM_6;
	key_identifier_map[VK_OEM_7] = Rml::Input::KI_OEM_7;
	key_identifier_map[VK_OEM_8] = Rml::Input::KI_OEM_8;

	key_identifier_map[VK_OEM_AX] = Rml::Input::KI_OEM_AX;
	key_identifier_map[VK_OEM_102] = Rml::Input::KI_OEM_102;
	key_identifier_map[VK_ICO_HELP] = Rml::Input::KI_ICO_HELP;
	key_identifier_map[VK_ICO_00] = Rml::Input::KI_ICO_00;

	key_identifier_map[VK_PROCESSKEY] = Rml::Input::KI_PROCESSKEY;

	key_identifier_map[VK_ICO_CLEAR] = Rml::Input::KI_ICO_CLEAR;

	key_identifier_map[VK_ATTN] = Rml::Input::KI_ATTN;
	key_identifier_map[VK_CRSEL] = Rml::Input::KI_CRSEL;
	key_identifier_map[VK_EXSEL] = Rml::Input::KI_EXSEL;
	key_identifier_map[VK_EREOF] = Rml::Input::KI_EREOF;
	key_identifier_map[VK_PLAY] = Rml::Input::KI_PLAY;
	key_identifier_map[VK_ZOOM] = Rml::Input::KI_ZOOM;
	key_identifier_map[VK_PA1] = Rml::Input::KI_PA1;
	key_identifier_map[VK_OEM_CLEAR] = Rml::Input::KI_OEM_CLEAR;
}
