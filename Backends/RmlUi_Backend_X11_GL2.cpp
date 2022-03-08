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

#include "RmlUi_Backend.h"
#include "RmlUi_Include_Xlib.h"
#include "RmlUi_Platform_X11.h"
#include "RmlUi_Renderer_GL2.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Debugger.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
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

namespace {

Rml::UniquePtr<RenderInterface_GL2> render_interface;
Rml::UniquePtr<SystemInterface_X11> system_interface;

Display* display = nullptr;
Window window = 0;
GLXContext gl_context = nullptr;

int window_width = 0;
int window_height = 0;

bool running = false;

Rml::Context* context = nullptr;

} // namespace

static void UpdateWindowDimensions(int width = 0, int height = 0)
{
	if (width > 0)
		window_width = width;
	if (height > 0)
		window_height = height;
	if (context)
		context->SetDimensions(Rml::Vector2i(window_width, window_height));
	
	RmlGL2::SetViewport(window_width, window_height);
}

static bool AttachToNative(XVisualInfo* visual_info)
{
	gl_context = glXCreateContext(display, visual_info, nullptr, GL_TRUE);
	if (!gl_context)
		return false;

	if (!glXMakeCurrent(display, window, gl_context))
		return false;

	if (!glXIsDirect(display, gl_context))
		RmlX11::DisplayError("OpenGL context does not support direct rendering; performance is likely to be poor.");

	Window root_window;
	int x, y;
	unsigned int width, height;
	unsigned int border_width, depth;
	XGetGeometry(display, window, &root_window, &x, &y, &width, &height, &border_width, &depth);

	RmlGL2::Initialize();

	return true;
}

static void DetachFromNative()
{
	// Shutdown OpenGL
	glXMakeCurrent(display, 0L, nullptr);
	glXDestroyContext(display, gl_context);
	gl_context = nullptr;
}

bool Backend::InitializeInterfaces()
{
	RMLUI_ASSERT(!system_interface && !render_interface);

	system_interface = Rml::MakeUnique<SystemInterface_X11>();
	Rml::SetSystemInterface(system_interface.get());

	render_interface = Rml::MakeUnique<RenderInterface_GL2>();
	Rml::SetRenderInterface(render_interface.get());

	return true;
}
void Backend::ShutdownInterfaces()
{
	render_interface.reset();
	system_interface.reset();
}

bool Backend::OpenWindow(const char* name, int width, int height, bool allow_resize)
{
	display = XOpenDisplay(0);
	if (!display)
		return false;

	window_width = width;
	window_height = height;

	// This initializes the keyboard to keycode mapping system of X11 itself. It must be done here after opening display as it needs to query the
	// connected X server display for information about its install keymap abilities.
	RmlX11::Initialize(display);

	int screen = XDefaultScreen(display);

	// Fetch an appropriate 32-bit visual interface.
	int attribute_list[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8,
		0L};

	XVisualInfo* visual_info = glXChooseVisual(display, screen, attribute_list);
	if (!visual_info)
		return false;

	if (!RmlX11::OpenWindow(name, width, height, allow_resize, display, visual_info, &window))
		return false;

	bool result = AttachToNative(visual_info);

	return result;
}

void Backend::CloseWindow()
{
	DetachFromNative();
	RmlX11::CloseWindow();
}

void Backend::SetContext(Rml::Context* new_context)
{
	context = new_context;
	UpdateWindowDimensions();
	RmlX11::SetContextForInput(context);
}

void Backend::EventLoop(ShellIdleFunction idle_function)
{
	running = true;

	// Loop on Peek/GetMessage until and exit has been requested
	while (running)
	{
		while (XPending(display) > 0)
		{
			XEvent event;
			char* event_type = nullptr;
			XNextEvent(display, &event);

			switch (event.type)
			{
			case ClientMessage:
			{
				// The only message we register for is WM_DELETE_WINDOW, so if we receive a client message then the
				// window has been closed.
				event_type = XGetAtomName(display, event.xclient.message_type);
				if (strcmp(event_type, "WM_PROTOCOLS") == 0)
					running = false;
				XFree(event_type);
				event_type = nullptr;
			}
			break;
			case ConfigureNotify:
			{
				int x = event.xconfigure.width;
				int y = event.xconfigure.height;

				UpdateWindowDimensions(x, y);
			}
			break;
			case KeyPress:
			{
				Rml::Input::KeyIdentifier key_identifier = RmlX11::ConvertKey(event.xkey.keycode);
				const int key_modifier_state = RmlX11::GetKeyModifierState(event.xkey.state);

				// Check for special key combinations.
				if (key_identifier == Rml::Input::KI_F8)
				{
					Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
				}
				else
				{
					// No special shortcut, pass the key on to the context.
					bool propagates = RmlX11::HandleWindowEvent(event);

					// Check for low-priority key combinations that are only activated if not already consumed by the context.
					if (propagates && key_identifier == Rml::Input::KI_R && key_modifier_state & Rml::Input::KM_CTRL)
					{
						for (int i = 0; i < context->GetNumDocuments(); i++)
						{
							Rml::ElementDocument* document = context->GetDocument(i);
							const Rml::String& src = document->GetSourceURL();
							if (src.size() > 4 && src.substr(src.size() - 4) == ".rml")
							{
								document->ReloadStyleSheet();
							}
						}
					}
				}
			}
			break;
			default:
			{
				// Pass unhandled events to the platform layer.
				RmlX11::HandleWindowEvent(event);
			}
			break;
			}
		}

		idle_function();
	}
}

void Backend::RequestExit()
{
	running = false;
}

void Backend::BeginFrame()
{
	RmlGL2::BeginFrame();
	RmlGL2::Clear();
}

void Backend::PresentFrame()
{
	RmlGL2::EndFrame();

	// Flips the OpenGL buffers.
	glXSwapBuffers(display, window);
}
