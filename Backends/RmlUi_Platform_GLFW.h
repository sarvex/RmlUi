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

#ifndef RMLUI_BACKENDS_PLATFORM_GLFW_H
#define RMLUI_BACKENDS_PLATFORM_GLFW_H

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>
#include <GLFW/glfw3.h>

class SystemInterface_GLFW : public Rml::SystemInterface {
public:
	double GetElapsedTime() override;

	void SetMouseCursor(const Rml::String& cursor_name) override;

	void SetClipboardText(const Rml::String& text) override;
	void GetClipboardText(Rml::String& text) override;
};

namespace RmlGLFW {

bool Initialize();
void Shutdown();

// Create and open the the window and setup default callbacks. The provided width and height determines the logical size of the window, while the
// returned width and height is the actual used framebuffer pixel size which may be different due to monitor DPI-settings.
bool CreateWindow(const char* name, int& inout_width, int& inout_height, bool allow_resize, GLFWwindow*& out_window);
void CloseWindow();

// Set the context to be used for input processing, window sizing, and content scaling (dp-ratio).
void SetContext(Rml::Context* context);

// During window creation, GLFW is setup with the following default callback functions. The callbacks can be overridden using the GLFW API when you
// need to for your own application. Then, to reinstate the default behavior on the context you may wish to call these functions manually. Arguments
// should be passed directly from the GLFW callbacks. The callbacks return true if the event is propagating, ie. was not handled by the context.
bool ProcessKeyCallback(int key, int action, int mods);
bool ProcessCharCallback(unsigned int codepoint);
bool ProcessCursorPosCallback(double xpos, double ypos);
bool ProcessMouseButtonCallback(int button, int action, int mods);
bool ProcessScrollCallback(double yoffset);
void ProcessFramebufferSizeCallback(int width, int height);
void ProcessContentScaleCallback(float xscale);

// When overriding the 'KeyCallback' or 'MouseButtonCallback', this should be called with the new modifers provided by GLFW.
void SetActiveModifiers(int glfw_mods);

Rml::Input::KeyIdentifier ConvertKey(int glfw_key);
int ConvertKeyModifiers(int glfw_mods);

} // namespace RmlGLFW

#endif
