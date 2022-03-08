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
#include "RmlUi_Include_Windows.h"
#include "RmlUi_Platform_Win32.h"
#include "RmlUi_Renderer_GL2.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Debugger/Debugger.h>

static HWND window_handle = nullptr;
static HDC device_context = nullptr;
static HGLRC render_context = nullptr;

static Rml::Context* context = nullptr;
static bool running = false;

static Rml::UniquePtr<RenderInterface_GL2> render_interface;
static Rml::UniquePtr<SystemInterface_Win32> system_interface;

static bool AttachToNative();
static void DetachFromNative();
static void ProcessKeyDown(Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state);

static LRESULT CALLBACK WindowProcedureHandler(HWND local_window_handle, UINT message, WPARAM w_param, LPARAM l_param)
{
	switch (message)
	{
	case WM_CLOSE:
	{
		running = false;
		return 0;
	}
	break;
	case WM_SIZE:
	{
		// Intercept sizing to set the OpenGL viewport, then submit it to the platform handler for context sizing.
		int width = LOWORD(l_param);
		int height = HIWORD(l_param);
		RmlGL2::SetViewport(width, height);
		RmlWin32::WindowProcedure(local_window_handle, message, w_param, l_param);
		return 0;
	}
	break;
	case WM_KEYDOWN:
	{
		// Intercept and process keydown events because we add some global hotkeys to the RmlUi samples.
		ProcessKeyDown(RmlWin32::ConvertKey((int)w_param), RmlWin32::GetKeyModifierState());
		return 0;
	}
	break;
	default:
	{
		// Submit it to the platform handler for default input and window handling.
		LRESULT result = RmlWin32::WindowProcedure(local_window_handle, message, w_param, l_param);
		if (result == 0)
			return 0;
	}
	break;
	}

	// All unhandled messages go to DefWindowProc.
	return DefWindowProc(local_window_handle, message, w_param, l_param);
}

bool Backend::InitializeInterfaces()
{
	RMLUI_ASSERT(!system_interface && !render_interface);

	system_interface = Rml::MakeUnique<SystemInterface_Win32>();
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
	if (!RmlWin32::Initialize())
		return false;

	// Initialize the window but don't show it yet.
	if (!RmlWin32::InitializeWindow(name, width, height, allow_resize, window_handle, WindowProcedureHandler))
		return false;

	// Attach the OpenGL context.
	if (!AttachToNative())
	{
		CloseWindow();
		return false;
	}

	// Now we are ready to show the window.
	RmlWin32::ShowWindow();

	return true;
}

void Backend::CloseWindow()
{
	DetachFromNative();
	RmlWin32::CloseWindow();
	window_handle = nullptr;

	RmlWin32::Shutdown();
	RmlGL2::Shutdown();
}

void Backend::SetContext(Rml::Context* new_context)
{
	context = new_context;
	RmlWin32::SetContext(new_context);
}

void Backend::EventLoop(ShellIdleFunction idle_function)
{
	MSG message;
	running = true;

	// Loop on PeekMessage() / GetMessage() until exit has been requested.
	while (running)
	{
		if (PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE))
		{
			GetMessage(&message, nullptr, 0, 0);

			TranslateMessage(&message);
			DispatchMessage(&message);
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
	SwapBuffers(device_context);
}

static bool AttachToNative()
{
	RMLUI_ASSERT(window_handle);
	device_context = GetDC(window_handle);
	render_context = nullptr;

	if (device_context == nullptr)
	{
		RmlWin32::DisplayError("Could not get device context.");
		return false;
	}

	PIXELFORMATDESCRIPTOR pixel_format_descriptor;
	memset(&pixel_format_descriptor, 0, sizeof(pixel_format_descriptor));
	pixel_format_descriptor.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pixel_format_descriptor.nVersion = 1;
	pixel_format_descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pixel_format_descriptor.iPixelType = PFD_TYPE_RGBA;
	pixel_format_descriptor.cColorBits = 32;
	pixel_format_descriptor.cRedBits = 8;
	pixel_format_descriptor.cGreenBits = 8;
	pixel_format_descriptor.cBlueBits = 8;
	pixel_format_descriptor.cAlphaBits = 8;
	pixel_format_descriptor.cDepthBits = 24;
	pixel_format_descriptor.cStencilBits = 8;

	int pixel_format = ChoosePixelFormat(device_context, &pixel_format_descriptor);
	if (pixel_format == 0)
	{
		RmlWin32::DisplayError("Could not choose 32-bit pixel format.");
		return false;
	}

	if (SetPixelFormat(device_context, pixel_format, &pixel_format_descriptor) == FALSE)
	{
		RmlWin32::DisplayError("Could not set pixel format.");
		return false;
	}

	render_context = wglCreateContext(device_context);
	if (render_context == nullptr)
	{
		RmlWin32::DisplayError("Could not create OpenGL rendering context.");
		return false;
	}

	// Activate the rendering context.
	if (wglMakeCurrent(device_context, render_context) == FALSE)
	{
		RmlWin32::DisplayError("Unable to make rendering context current.");
		return false;
	}

	RmlGL2::Initialize();

	return true;
}

static void DetachFromNative()
{
	// Shutdown OpenGL
	if (render_context)
	{
		wglMakeCurrent(nullptr, nullptr);
		wglDeleteContext(render_context);
		render_context = nullptr;
	}

	if (device_context)
	{
		ReleaseDC(window_handle, device_context);
		device_context = nullptr;
	}
}

static void ProcessKeyDown(Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state)
{
	if (!context)
		return;

	// Toggle debugger and set dp-ratio using Ctrl +/-/0 keys. These global shortcuts take priority.
	if (key_identifier == Rml::Input::KI_F8)
	{
		Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
	}
	else if (key_identifier == Rml::Input::KI_0 && key_modifier_state & Rml::Input::KM_CTRL)
	{
		context->SetDensityIndependentPixelRatio(RmlWin32::GetDensityIndependentPixelRatio());
	}
	else if (key_identifier == Rml::Input::KI_1 && key_modifier_state & Rml::Input::KM_CTRL)
	{
		context->SetDensityIndependentPixelRatio(1.f);
	}
	else if (key_identifier == Rml::Input::KI_OEM_MINUS && key_modifier_state & Rml::Input::KM_CTRL)
	{
		const float new_dp_ratio = Rml::Math::Max(context->GetDensityIndependentPixelRatio() / 1.2f, 0.5f);
		context->SetDensityIndependentPixelRatio(new_dp_ratio);
	}
	else if (key_identifier == Rml::Input::KI_OEM_PLUS && key_modifier_state & Rml::Input::KM_CTRL)
	{
		const float new_dp_ratio = Rml::Math::Min(context->GetDensityIndependentPixelRatio() * 1.2f, 2.5f);
		context->SetDensityIndependentPixelRatio(new_dp_ratio);
	}
	else
	{
		// No global shortcuts detected, submit the key to the context.
		if (context->ProcessKeyDown(key_identifier, key_modifier_state))
		{
			// The key was not consumed, check for shortcuts that are of lower priority.
			if (key_identifier == Rml::Input::KI_R && key_modifier_state & Rml::Input::KM_CTRL)
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
}
