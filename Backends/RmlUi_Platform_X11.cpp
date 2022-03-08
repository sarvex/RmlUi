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

#include "RmlUi_Platform_X11.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Types.h>
#include <X11/cursorfont.h>
#include <X11/extensions/xf86vmode.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef HAS_X11XKBLIB
	#include <X11/XKBlib.h>
#endif // HAS_X11XKBLIB
#include <X11/keysym.h>

// - State
static Rml::Context* context = nullptr;
static timeval start_time;

// -- Window
static Display* display = nullptr;
static Window window = 0;
static Cursor cursor_default = 0;
static Cursor cursor_move = 0;
static Cursor cursor_pointer = 0;
static Cursor cursor_resize = 0;
static Cursor cursor_cross = 0;
static Cursor cursor_text = 0;
static Cursor cursor_unavailable = 0;

// -- Clipboard
static Rml::String clipboard_text;
static Atom XA_atom = 4;
static Atom XA_STRING_atom = 31;
static Atom UTF8_atom;
static Atom CLIPBOARD_atom;
static Atom XSEL_DATA_atom;
static Atom TARGETS_atom;
static Atom TEXT_atom;

// -- Input
static void InitializeKeymap();
static void InitializeX11Keymap(Display* display);
static bool HandleKeyboardEvent(const XEvent& event);
static Rml::Character GetCharacterCode(Rml::Input::KeyIdentifier key_identifier, int key_modifier_state);

static const int KEYMAP_SIZE = 256;
static Rml::Input::KeyIdentifier key_identifier_map[KEYMAP_SIZE];

#ifdef HAS_X11XKBLIB
static bool has_xkblib = false;
#endif // HAS_X11XKBLIB

static int min_keycode, max_keycode, keysyms_per_keycode;
static KeySym* x11_key_mapping = nullptr;

static void XCopy(const Rml::String& clipboard_data, const XEvent& event)
{
	Atom format;
	if (UTF8_atom)
	{
		format = UTF8_atom;
	}
	else
	{
		format = XA_STRING_atom;
	}
	XSelectionEvent ev = {
		SelectionNotify, // the event type that will be sent to the requestor
		0,               // serial
		0,               // send_event
		event.xselectionrequest.display, event.xselectionrequest.requestor, event.xselectionrequest.selection, event.xselectionrequest.target,
		event.xselectionrequest.property,
		0 // time
	};
	int retval = 0;
	if (ev.target == TARGETS_atom)
	{
		retval = XChangeProperty(ev.display, ev.requestor, ev.property, XA_atom, 32, PropModeReplace, (unsigned char*)&format, 1);
	}
	else if (ev.target == XA_STRING_atom || ev.target == TEXT_atom)
	{
		retval = XChangeProperty(ev.display, ev.requestor, ev.property, XA_STRING_atom, 8, PropModeReplace, (unsigned char*)clipboard_data.c_str(),
			clipboard_data.size());
	}
	else if (ev.target == UTF8_atom)
	{
		retval = XChangeProperty(ev.display, ev.requestor, ev.property, UTF8_atom, 8, PropModeReplace, (unsigned char*)clipboard_data.c_str(),
			clipboard_data.size());
	}
	else
	{
		ev.property = 0;
	}
	if ((retval & 2) == 0)
	{
		// Notify the requestor that clipboard data is available
		XSendEvent(display, ev.requestor, 0, 0, (XEvent*)&ev);
	}
}

static bool XPaste(Atom target_atom, Rml::String& clipboard_data)
{
	XEvent event;

	// A SelectionRequest event will be sent to the clipboard owner, which should respond with SelectionNotify
	XConvertSelection(display, CLIPBOARD_atom, target_atom, XSEL_DATA_atom, window, CurrentTime);
	XSync(display, 0);
	XNextEvent(display, &event);

	if (event.type == SelectionNotify)
	{
		if (event.xselection.property == 0)
		{
			// If no owner for the specified selection exists, the X server generates
			// a SelectionNotify event with property None (0).
			return false;
		}
		if (event.xselection.selection == CLIPBOARD_atom)
		{
			int actual_format;
			unsigned long bytes_after, nitems;
			char* prop = nullptr;
			Atom actual_type;
			XGetWindowProperty(event.xselection.display, event.xselection.requestor, event.xselection.property,
				0L,    // offset
				(~0L), // length
				0,     // delete?
				AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, (unsigned char**)&prop);
			if (actual_type == UTF8_atom || actual_type == XA_STRING_atom)
			{
				clipboard_data = Rml::String(prop, prop + nitems);
				XFree(prop);
			}
			XDeleteProperty(event.xselection.display, event.xselection.requestor, event.xselection.property);
			return true;
		}
	}

	return false;
}

bool RmlX11::Initialize(Display* display)
{
	RMLUI_ASSERT(display);

	gettimeofday(&start_time, nullptr);

	InitializeKeymap();
	InitializeX11Keymap(display);

	return true;
}

void RmlX11::Shutdown() {}

bool RmlX11::OpenWindow(const char* name, int width, int height, bool allow_resize, Display* in_display, XVisualInfo* visual_info, Window* out_window)
{
	display = in_display;

	// Build up our window attributes.
	XSetWindowAttributes window_attributes;
	window_attributes.colormap = XCreateColormap(display, RootWindow(display, visual_info->screen), visual_info->visual, AllocNone);
	window_attributes.border_pixel = 0;
	window_attributes.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask;

	// Create the window.
	window = XCreateWindow(display, RootWindow(display, visual_info->screen), 0, 0, width, height, 0, visual_info->depth, InputOutput,
		visual_info->visual, CWBorderPixel | CWColormap | CWEventMask, &window_attributes);

	// Handle delete events in windowed mode.
	Atom delete_atom = XInternAtom(display, "WM_DELETE_WINDOW", True);
	XSetWMProtocols(display, window, &delete_atom, 1);

	// Capture the events we're interested in.
	XSelectInput(display, window, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);

	if (!allow_resize)
	{
		// Force the window to remain at the fixed size by asking the window manager nicely, it may choose to ignore us
		XSizeHints* win_size_hints = XAllocSizeHints(); // Allocate a size hint structure
		if (win_size_hints == nullptr)
		{
			fprintf(stderr, "XAllocSizeHints - out of memory\n");
		}
		else
		{
			// Initialize the structure and specify which hints will be providing
			win_size_hints->flags = PSize | PMinSize | PMaxSize;

			// Set the sizes we want the window manager to use
			win_size_hints->base_width = width;
			win_size_hints->base_height = height;
			win_size_hints->min_width = width;
			win_size_hints->min_height = height;
			win_size_hints->max_width = width;
			win_size_hints->max_height = height;

			// Pass the size hints to the window manager.
			XSetWMNormalHints(display, window, win_size_hints);

			// Free the size buffer
			XFree(win_size_hints);
		}
	}

	{
		// Create cursors
		cursor_default = XCreateFontCursor(display, XC_left_ptr);
		cursor_move = XCreateFontCursor(display, XC_fleur);
		cursor_pointer = XCreateFontCursor(display, XC_hand1);
		cursor_resize = XCreateFontCursor(display, XC_sizing);
		cursor_cross = XCreateFontCursor(display, XC_crosshair);
		cursor_text = XCreateFontCursor(display, XC_xterm);
		cursor_unavailable = XCreateFontCursor(display, XC_X_cursor);
	}

	// For copy & paste functions
	UTF8_atom = XInternAtom(display, "UTF8_STRING", 1);
	XSEL_DATA_atom = XInternAtom(display, "XSEL_DATA", 0);
	CLIPBOARD_atom = XInternAtom(display, "CLIPBOARD", 0);
	TARGETS_atom = XInternAtom(display, "TARGETS", 0);
	TEXT_atom = XInternAtom(display, "TEXT", 0);

	// Set the window title and show the window.
	XSetStandardProperties(display, window, name, "", 0L, nullptr, 0, nullptr);
	XMapRaised(display, window);

	*out_window = window;

	return true;
}

void RmlX11::CloseWindow()
{
	if (display)
	{
		XCloseDisplay(display);
		display = nullptr;
	}
}

bool RmlX11::HandleWindowEvent(const XEvent& event)
{
	switch (event.type)
	{
	case SelectionRequest:
	{
		if (XGetSelectionOwner(display, CLIPBOARD_atom) == window && event.xselectionrequest.selection == CLIPBOARD_atom)
		{
			XCopy(clipboard_text, event);
			return false;
		}
	}
	break;
	default:
	{
		return HandleKeyboardEvent(event);
	}
	break;
	}

	return true;
}

void RmlX11::DisplayError(const char* fmt, ...)
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

	printf("%s", buffer);
}

// Returns the seconds that have elapsed since program startup.
double SystemInterface_X11::GetElapsedTime()
{
	struct timeval now;

	gettimeofday(&now, nullptr);

	double sec = now.tv_sec - start_time.tv_sec;
	double usec = now.tv_usec - start_time.tv_usec;
	double result = sec + (usec / 1000000.0);

	return result;
}

void SystemInterface_X11::SetMouseCursor(const Rml::String& cursor_name)
{
	if (display && window)
	{
		Cursor cursor_handle = 0;
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
			XDefineCursor(display, window, cursor_handle);
		}
	}
}

void SystemInterface_X11::SetClipboardText(const Rml::String& text)
{
	clipboard_text = text;
	XSetSelectionOwner(display, CLIPBOARD_atom, window, 0);
}

void SystemInterface_X11::GetClipboardText(Rml::String& text)
{
	if (XGetSelectionOwner(display, CLIPBOARD_atom) != window)
	{
		if (!UTF8_atom || !XPaste(UTF8_atom, text))
		{
			// fallback
			XPaste(XA_STRING_atom, text);
		}
	}
	else
	{
		text = clipboard_text;
	}
}

void RmlX11::SetContextForInput(Rml::Context* new_context)
{
	context = new_context;
}

static void InitializeX11Keymap(Display* display)
{
	RMLUI_ASSERT(display != nullptr);

#ifdef HAS_X11XKBLIB
	int opcode_rtrn = -1;
	int event_rtrn = -1;
	int error_rtrn = -1;
	int major_in_out = -1;
	int minor_in_out = -1;

	// Xkb extension may not exist in the server.  This checks for its
	// existence and initializes the extension if available.
	has_xkblib = XkbQueryExtension(display, &opcode_rtrn, &event_rtrn, &error_rtrn, &major_in_out, &minor_in_out);

	// if Xkb isn't available, fall back to using XGetKeyboardMapping,
	// which may occur if RmlUi is compiled with Xkb support but the
	// server doesn't support it.  This occurs with older X11 servers or
	// virtual framebuffers such as x11vnc server.
	if (!has_xkblib)
#endif // HAS_X11XKBLIB
	{
		XDisplayKeycodes(display, &min_keycode, &max_keycode);

		RMLUI_ASSERT(x11_key_mapping != nullptr);
		x11_key_mapping = XGetKeyboardMapping(display, min_keycode, max_keycode + 1 - min_keycode, &keysyms_per_keycode);
	}
}

static bool HandleKeyboardEvent(const XEvent& event)
{
	// Process all mouse and keyboard events
	switch (event.type)
	{
	case ButtonPress:
	{
		int button_index;

		switch (event.xbutton.button)
		{
		case Button1:
			button_index = 0;
			break;
		case Button2:
			button_index = 2;
			break;
		case Button3:
			button_index = 1;
			break;
		case Button4:
			return context->ProcessMouseWheel(-1, RmlX11::GetKeyModifierState(event.xbutton.state));
		case Button5:
			return context->ProcessMouseWheel(1, RmlX11::GetKeyModifierState(event.xbutton.state));
		default:
			return true;
		}

		return context->ProcessMouseButtonDown(button_index, RmlX11::GetKeyModifierState(event.xbutton.state));
	}
	break;

	case ButtonRelease:
	{
		int button_index;

		switch (event.xbutton.button)
		{
		case Button1:
			button_index = 0;
			break;
		case Button2:
			button_index = 2;
			break;
		case Button3:
			button_index = 1;
			break;
		default:
			return true;
		}

		return context->ProcessMouseButtonUp(button_index, RmlX11::GetKeyModifierState(event.xbutton.state));
	}
	break;

	case MotionNotify:
		return context->ProcessMouseMove(event.xmotion.x, event.xmotion.y, RmlX11::GetKeyModifierState(event.xmotion.state));
		break;

	case KeyPress:
	{
		Rml::Input::KeyIdentifier key_identifier = RmlX11::ConvertKey(event.xkey.keycode);
		const int key_modifier_state = RmlX11::GetKeyModifierState(event.xkey.state);

		bool propagates = true;

		if (key_identifier != Rml::Input::KI_UNKNOWN)
			propagates = context->ProcessKeyDown(key_identifier, key_modifier_state);

		Rml::Character character = GetCharacterCode(key_identifier, key_modifier_state);
		if (character != Rml::Character::Null && !(key_modifier_state & Rml::Input::KM_CTRL))
			propagates &= context->ProcessTextInput(character);

		return propagates;
	}
	break;

	case KeyRelease:
	{
		Rml::Input::KeyIdentifier key_identifier = RmlX11::ConvertKey(event.xkey.keycode);
		const int key_modifier_state = RmlX11::GetKeyModifierState(event.xkey.state);

		bool propagates = true;

		if (key_identifier != Rml::Input::KI_UNKNOWN)
			propagates = context->ProcessKeyUp(key_identifier, key_modifier_state);

		return propagates;
	}
	break;
	}

	return true;
}

Rml::Input::KeyIdentifier RmlX11::ConvertKey(unsigned int x11_key_code)
{
	const int group_index = 0; // this is always 0 for our limited example
	Rml::Input::KeyIdentifier key_identifier = Rml::Input::KI_UNKNOWN;

#ifdef HAS_X11XKBLIB
	if (has_xkblib)
	{
		KeySym sym = XkbKeycodeToKeysym(display, x11_key_code, 0, group_index);

		key_identifier = key_identifier_map[sym & 0xFF];
	}
	else
#endif // HAS_X11XKBLIB
	{
		KeySym sym = x11_key_mapping[(x11_key_code - min_keycode) * keysyms_per_keycode + group_index];

		KeySym lower_sym, upper_sym;
		XConvertCase(sym, &lower_sym, &upper_sym);

		key_identifier = key_identifier_map[lower_sym & 0xFF];
	}

	return key_identifier;
}

int RmlX11::GetKeyModifierState(int x_state)
{
	int key_modifier_state = 0;

	if (x_state & ShiftMask)
		key_modifier_state |= Rml::Input::KM_SHIFT;

	if (x_state & LockMask)
		key_modifier_state |= Rml::Input::KM_CAPSLOCK;

	if (x_state & ControlMask)
		key_modifier_state |= Rml::Input::KM_CTRL;

	if (x_state & Mod5Mask)
		key_modifier_state |= Rml::Input::KM_ALT;

	if (x_state & Mod2Mask)
		key_modifier_state |= Rml::Input::KM_NUMLOCK;

	return key_modifier_state;
}

static void InitializeKeymap()
{
	// Initialise the key map with default values.
	memset(key_identifier_map, 0, sizeof(key_identifier_map));

	key_identifier_map[XK_BackSpace & 0xFF] = Rml::Input::KI_BACK;
	key_identifier_map[XK_Tab & 0xFF] = Rml::Input::KI_TAB;
	key_identifier_map[XK_Clear & 0xFF] = Rml::Input::KI_CLEAR;
	key_identifier_map[XK_Return & 0xFF] = Rml::Input::KI_RETURN;
	key_identifier_map[XK_Pause & 0xFF] = Rml::Input::KI_PAUSE;
	key_identifier_map[XK_Scroll_Lock & 0xFF] = Rml::Input::KI_SCROLL;
	key_identifier_map[XK_Escape & 0xFF] = Rml::Input::KI_ESCAPE;
	key_identifier_map[XK_Delete & 0xFF] = Rml::Input::KI_DELETE;

	key_identifier_map[XK_Kanji & 0xFF] = Rml::Input::KI_KANJI;
	//	key_identifier_map[XK_Muhenkan & 0xFF] = Rml::Input::; /* Cancel Conversion */
	//	key_identifier_map[XK_Henkan_Mode & 0xFF] = Rml::Input::; /* Start/Stop Conversion */
	//	key_identifier_map[XK_Henkan & 0xFF] = Rml::Input::; /* Alias for Henkan_Mode */
	//	key_identifier_map[XK_Romaji & 0xFF] = Rml::Input::; /* to Romaji */
	//	key_identifier_map[XK_Hiragana & 0xFF] = Rml::Input::; /* to Hiragana */
	//	key_identifier_map[XK_Katakana & 0xFF] = Rml::Input::; /* to Katakana */
	//	key_identifier_map[XK_Hiragana_Katakana & 0xFF] = Rml::Input::; /* Hiragana/Katakana toggle */
	//	key_identifier_map[XK_Zenkaku & 0xFF] = Rml::Input::; /* to Zenkaku */
	//	key_identifier_map[XK_Hankaku & 0xFF] = Rml::Input::; /* to Hankaku */
	//	key_identifier_map[XK_Zenkaku_Hankaku & 0xFF] = Rml::Input::; /* Zenkaku/Hankaku toggle */
	key_identifier_map[XK_Touroku & 0xFF] = Rml::Input::KI_OEM_FJ_TOUROKU;
	key_identifier_map[XK_Massyo & 0xFF] = Rml::Input::KI_OEM_FJ_MASSHOU;
	//	key_identifier_map[XK_Kana_Lock & 0xFF] = Rml::Input::; /* Kana Lock */
	//	key_identifier_map[XK_Kana_Shift & 0xFF] = Rml::Input::; /* Kana Shift */
	//	key_identifier_map[XK_Eisu_Shift & 0xFF] = Rml::Input::; /* Alphanumeric Shift */
	//	key_identifier_map[XK_Eisu_toggle & 0xFF] = Rml::Input::; /* Alphanumeric toggle */

	key_identifier_map[XK_Home & 0xFF] = Rml::Input::KI_HOME;
	key_identifier_map[XK_Left & 0xFF] = Rml::Input::KI_LEFT;
	key_identifier_map[XK_Up & 0xFF] = Rml::Input::KI_UP;
	key_identifier_map[XK_Right & 0xFF] = Rml::Input::KI_RIGHT;
	key_identifier_map[XK_Down & 0xFF] = Rml::Input::KI_DOWN;
	key_identifier_map[XK_Prior & 0xFF] = Rml::Input::KI_PRIOR;
	key_identifier_map[XK_Next & 0xFF] = Rml::Input::KI_NEXT;
	key_identifier_map[XK_End & 0xFF] = Rml::Input::KI_END;
	key_identifier_map[XK_Begin & 0xFF] = Rml::Input::KI_HOME;

	key_identifier_map[XK_Print & 0xFF] = Rml::Input::KI_SNAPSHOT;
	key_identifier_map[XK_Insert & 0xFF] = Rml::Input::KI_INSERT;
	key_identifier_map[XK_Num_Lock & 0xFF] = Rml::Input::KI_NUMLOCK;

	key_identifier_map[XK_KP_Space & 0xFF] = Rml::Input::KI_SPACE;
	key_identifier_map[XK_KP_Tab & 0xFF] = Rml::Input::KI_TAB;
	key_identifier_map[XK_KP_Enter & 0xFF] = Rml::Input::KI_NUMPADENTER;
	key_identifier_map[XK_KP_F1 & 0xFF] = Rml::Input::KI_F1;
	key_identifier_map[XK_KP_F2 & 0xFF] = Rml::Input::KI_F2;
	key_identifier_map[XK_KP_F3 & 0xFF] = Rml::Input::KI_F3;
	key_identifier_map[XK_KP_F4 & 0xFF] = Rml::Input::KI_F4;
	key_identifier_map[XK_KP_Home & 0xFF] = Rml::Input::KI_NUMPAD7;
	key_identifier_map[XK_KP_Left & 0xFF] = Rml::Input::KI_NUMPAD4;
	key_identifier_map[XK_KP_Up & 0xFF] = Rml::Input::KI_NUMPAD8;
	key_identifier_map[XK_KP_Right & 0xFF] = Rml::Input::KI_NUMPAD6;
	key_identifier_map[XK_KP_Down & 0xFF] = Rml::Input::KI_NUMPAD2;
	key_identifier_map[XK_KP_Prior & 0xFF] = Rml::Input::KI_NUMPAD9;
	key_identifier_map[XK_KP_Next & 0xFF] = Rml::Input::KI_NUMPAD3;
	key_identifier_map[XK_KP_End & 0xFF] = Rml::Input::KI_NUMPAD1;
	key_identifier_map[XK_KP_Begin & 0xFF] = Rml::Input::KI_NUMPAD5;
	key_identifier_map[XK_KP_Insert & 0xFF] = Rml::Input::KI_NUMPAD0;
	key_identifier_map[XK_KP_Delete & 0xFF] = Rml::Input::KI_DECIMAL;
	key_identifier_map[XK_KP_Equal & 0xFF] = Rml::Input::KI_OEM_NEC_EQUAL;
	key_identifier_map[XK_KP_Multiply & 0xFF] = Rml::Input::KI_MULTIPLY;
	key_identifier_map[XK_KP_Add & 0xFF] = Rml::Input::KI_ADD;
	key_identifier_map[XK_KP_Separator & 0xFF] = Rml::Input::KI_SEPARATOR;
	key_identifier_map[XK_KP_Subtract & 0xFF] = Rml::Input::KI_SUBTRACT;
	key_identifier_map[XK_KP_Decimal & 0xFF] = Rml::Input::KI_DECIMAL;
	key_identifier_map[XK_KP_Divide & 0xFF] = Rml::Input::KI_DIVIDE;

	key_identifier_map[XK_F1 & 0xFF] = Rml::Input::KI_F1;
	key_identifier_map[XK_F2 & 0xFF] = Rml::Input::KI_F2;
	key_identifier_map[XK_F3 & 0xFF] = Rml::Input::KI_F3;
	key_identifier_map[XK_F4 & 0xFF] = Rml::Input::KI_F4;
	key_identifier_map[XK_F5 & 0xFF] = Rml::Input::KI_F5;
	key_identifier_map[XK_F6 & 0xFF] = Rml::Input::KI_F6;
	key_identifier_map[XK_F7 & 0xFF] = Rml::Input::KI_F7;
	key_identifier_map[XK_F8 & 0xFF] = Rml::Input::KI_F8;
	key_identifier_map[XK_F9 & 0xFF] = Rml::Input::KI_F9;
	key_identifier_map[XK_F10 & 0xFF] = Rml::Input::KI_F10;
	key_identifier_map[XK_F11 & 0xFF] = Rml::Input::KI_F11;
	key_identifier_map[XK_F12 & 0xFF] = Rml::Input::KI_F12;
	key_identifier_map[XK_F13 & 0xFF] = Rml::Input::KI_F13;
	key_identifier_map[XK_F14 & 0xFF] = Rml::Input::KI_F14;
	key_identifier_map[XK_F15 & 0xFF] = Rml::Input::KI_F15;
	key_identifier_map[XK_F16 & 0xFF] = Rml::Input::KI_F16;
	key_identifier_map[XK_F17 & 0xFF] = Rml::Input::KI_F17;
	key_identifier_map[XK_F18 & 0xFF] = Rml::Input::KI_F18;
	key_identifier_map[XK_F19 & 0xFF] = Rml::Input::KI_F19;
	key_identifier_map[XK_F20 & 0xFF] = Rml::Input::KI_F20;
	key_identifier_map[XK_F21 & 0xFF] = Rml::Input::KI_F21;
	key_identifier_map[XK_F22 & 0xFF] = Rml::Input::KI_F22;
	key_identifier_map[XK_F23 & 0xFF] = Rml::Input::KI_F23;
	key_identifier_map[XK_F24 & 0xFF] = Rml::Input::KI_F24;

	key_identifier_map[XK_Shift_L & 0xFF] = Rml::Input::KI_LSHIFT;
	key_identifier_map[XK_Shift_R & 0xFF] = Rml::Input::KI_RSHIFT;
	key_identifier_map[XK_Control_L & 0xFF] = Rml::Input::KI_LCONTROL;
	key_identifier_map[XK_Control_R & 0xFF] = Rml::Input::KI_RCONTROL;
	key_identifier_map[XK_Caps_Lock & 0xFF] = Rml::Input::KI_CAPITAL;

	key_identifier_map[XK_Alt_L & 0xFF] = Rml::Input::KI_LMENU;
	key_identifier_map[XK_Alt_R & 0xFF] = Rml::Input::KI_RMENU;

	key_identifier_map[XK_space & 0xFF] = Rml::Input::KI_SPACE;
	key_identifier_map[XK_apostrophe & 0xFF] = Rml::Input::KI_OEM_7;
	key_identifier_map[XK_comma & 0xFF] = Rml::Input::KI_OEM_COMMA;
	key_identifier_map[XK_minus & 0xFF] = Rml::Input::KI_OEM_MINUS;
	key_identifier_map[XK_period & 0xFF] = Rml::Input::KI_OEM_PERIOD;
	key_identifier_map[XK_slash & 0xFF] = Rml::Input::KI_OEM_2;
	key_identifier_map[XK_0 & 0xFF] = Rml::Input::KI_0;
	key_identifier_map[XK_1 & 0xFF] = Rml::Input::KI_1;
	key_identifier_map[XK_2 & 0xFF] = Rml::Input::KI_2;
	key_identifier_map[XK_3 & 0xFF] = Rml::Input::KI_3;
	key_identifier_map[XK_4 & 0xFF] = Rml::Input::KI_4;
	key_identifier_map[XK_5 & 0xFF] = Rml::Input::KI_5;
	key_identifier_map[XK_6 & 0xFF] = Rml::Input::KI_6;
	key_identifier_map[XK_7 & 0xFF] = Rml::Input::KI_7;
	key_identifier_map[XK_8 & 0xFF] = Rml::Input::KI_8;
	key_identifier_map[XK_9 & 0xFF] = Rml::Input::KI_9;
	key_identifier_map[XK_semicolon & 0xFF] = Rml::Input::KI_OEM_1;
	key_identifier_map[XK_equal & 0xFF] = Rml::Input::KI_OEM_PLUS;
	key_identifier_map[XK_bracketleft & 0xFF] = Rml::Input::KI_OEM_4;
	key_identifier_map[XK_backslash & 0xFF] = Rml::Input::KI_OEM_5;
	key_identifier_map[XK_bracketright & 0xFF] = Rml::Input::KI_OEM_6;
	key_identifier_map[XK_grave & 0xFF] = Rml::Input::KI_OEM_3;
	key_identifier_map[XK_a & 0xFF] = Rml::Input::KI_A;
	key_identifier_map[XK_b & 0xFF] = Rml::Input::KI_B;
	key_identifier_map[XK_c & 0xFF] = Rml::Input::KI_C;
	key_identifier_map[XK_d & 0xFF] = Rml::Input::KI_D;
	key_identifier_map[XK_e & 0xFF] = Rml::Input::KI_E;
	key_identifier_map[XK_f & 0xFF] = Rml::Input::KI_F;
	key_identifier_map[XK_g & 0xFF] = Rml::Input::KI_G;
	key_identifier_map[XK_h & 0xFF] = Rml::Input::KI_H;
	key_identifier_map[XK_i & 0xFF] = Rml::Input::KI_I;
	key_identifier_map[XK_j & 0xFF] = Rml::Input::KI_J;
	key_identifier_map[XK_k & 0xFF] = Rml::Input::KI_K;
	key_identifier_map[XK_l & 0xFF] = Rml::Input::KI_L;
	key_identifier_map[XK_m & 0xFF] = Rml::Input::KI_M;
	key_identifier_map[XK_n & 0xFF] = Rml::Input::KI_N;
	key_identifier_map[XK_o & 0xFF] = Rml::Input::KI_O;
	key_identifier_map[XK_p & 0xFF] = Rml::Input::KI_P;
	key_identifier_map[XK_q & 0xFF] = Rml::Input::KI_Q;
	key_identifier_map[XK_r & 0xFF] = Rml::Input::KI_R;
	key_identifier_map[XK_s & 0xFF] = Rml::Input::KI_S;
	key_identifier_map[XK_t & 0xFF] = Rml::Input::KI_T;
	key_identifier_map[XK_u & 0xFF] = Rml::Input::KI_U;
	key_identifier_map[XK_v & 0xFF] = Rml::Input::KI_V;
	key_identifier_map[XK_w & 0xFF] = Rml::Input::KI_W;
	key_identifier_map[XK_x & 0xFF] = Rml::Input::KI_X;
	key_identifier_map[XK_y & 0xFF] = Rml::Input::KI_Y;
	key_identifier_map[XK_z & 0xFF] = Rml::Input::KI_Z;
}

/**
    This map contains 4 different mappings from key identifiers to character codes. Each entry represents a different
    combination of shift and capslock state.
 */

static char ascii_map[4][51] = {
	// shift off and capslock off
	{0, ' ', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q',
		'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', ';', '=', ',', '-', '.', '/', '`', '[', '\\', ']', '\'', 0, 0},
	// shift on and capslock off
	{0, ' ', ')', '!', '@', '#', '$', '%', '^', '&', '*', '(', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q',
		'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', ':', '+', '<', '_', '>', '?', '~', '{', '|', '}', '"', 0, 0},
	// shift on and capslock on
	{0, ' ', ')', '!', '@', '#', '$', '%', '^', '&', '*', '(', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q',
		'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', ':', '+', '<', '_', '>', '?', '~', '{', '|', '}', '"', 0, 0},
	// shift off and capslock on
	{0, ' ', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q',
		'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', ';', '=', ',', '-', '.', '/', '`', '[', '\\', ']', '\'', 0, 0}};

static char keypad_map[2][18] = {{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '\n', '*', '+', 0, '-', '.', '/', '='},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\n', '*', '+', 0, '-', 0, '/', '='}};

// Returns the character code for a key identifer / key modifier combination.
static Rml::Character GetCharacterCode(Rml::Input::KeyIdentifier key_identifier, int key_modifier_state)
{
	using Rml::Character;

	// Check if we have a keycode capable of generating characters on the main keyboard (ie, not on the numeric
	// keypad; that is dealt with below).
	if (key_identifier <= Rml::Input::KI_OEM_102)
	{
		// Get modifier states
		bool shift = (key_modifier_state & Rml::Input::KM_SHIFT) > 0;
		bool capslock = (key_modifier_state & Rml::Input::KM_CAPSLOCK) > 0;

		// Return character code based on identifier and modifiers
		if (shift && !capslock)
			return (Character)ascii_map[1][key_identifier];

		if (shift && capslock)
			return (Character)ascii_map[2][key_identifier];

		if (!shift && capslock)
			return (Character)ascii_map[3][key_identifier];

		return (Character)ascii_map[0][key_identifier];
	}

	// Check if we have a keycode from the numeric keypad.
	else if (key_identifier <= Rml::Input::KI_OEM_NEC_EQUAL)
	{
		if (key_modifier_state & Rml::Input::KM_NUMLOCK)
			return (Character)keypad_map[0][key_identifier - Rml::Input::KI_NUMPAD0];
		else
			return (Character)keypad_map[1][key_identifier - Rml::Input::KI_NUMPAD0];
	}

	else if (key_identifier == Rml::Input::KI_RETURN)
		return (Character)'\n';

	return Character::Null;
}
