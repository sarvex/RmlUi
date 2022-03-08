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

#ifndef RMLUI_BACKENDS_PLATFORM_WIN32_H
#define RMLUI_BACKENDS_PLATFORM_WIN32_H

#include "RmlUi_Include_Windows.h"
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

class SystemInterface_Win32 : public Rml::SystemInterface {
public:
	double GetElapsedTime() override;

	void SetMouseCursor(const Rml::String& cursor_name) override;

	void SetClipboardText(const Rml::String& text) override;
	void GetClipboardText(Rml::String& text) override;
};

namespace RmlWin32 {

bool Initialize();
void Shutdown();

// Create the window but don't show it yet. The provided width and height determines the logical size of the window, while the
// returned width and height is the actual used framebuffer pixel size which may be different due to monitor DPI-settings.
// Submit the function pointer for the window's event handler through the 'func_window_procedure' argument.
bool InitializeWindow(const char* name, int& inout_width, int& inout_height, bool allow_resize, HWND& out_window_handle,
	WNDPROC func_window_procedure);
void ShowWindow();
void CloseWindow();

// Window event handler for default behavior. Submits input to the context, and handles context sizing and DPI scaling (dp-ratio).
// Returns 0 if the event was handled by the context, ie. the event should stop propagating.
LRESULT WindowProcedure(HWND local_window_handle, UINT message, WPARAM w_param, LPARAM l_param);

// Set the context to be used for input processing, window sizing, and DPI scaling (dp-ratio).
void SetContext(Rml::Context* context);
// Return the dp-ratio (DPI scaling ratio).
float GetDensityIndependentPixelRatio();

Rml::Input::KeyIdentifier ConvertKey(int win32_key_code);
int GetKeyModifierState();

void DisplayError(const char* fmt, ...);

} // namespace RmlWin32

#endif
