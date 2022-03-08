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
#include "RmlUi_Platform_GLFW.h"
#include "RmlUi_Renderer_GL2.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Debugger/Debugger.h>
#include <GLFW/glfw3.h>

static Rml::UniquePtr<RenderInterface_GL2> render_interface;
static Rml::UniquePtr<SystemInterface_GLFW> system_interface;

static GLFWwindow* window = nullptr;
static Rml::Context* context = nullptr;

static void SetupBackendCallbacks();
static void ProcessKeyDown(int glfw_key, int glfw_action, int glfw_mods);

bool Backend::InitializeInterfaces()
{
	RMLUI_ASSERT(!system_interface && !render_interface);

	system_interface = Rml::MakeUnique<SystemInterface_GLFW>();
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
	if (!RmlGLFW::Initialize())
		return false;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

	// Request stencil buffer of at least 8-bit size to supporting clipping on transformed elements.
	glfwWindowHint(GLFW_STENCIL_BITS, 8);

	// Enable MSAA for better-looking visuals, especially when transforms are applied.
	glfwWindowHint(GLFW_SAMPLES, 2);

	if (!RmlGLFW::CreateWindow(name, width, height, allow_resize, window))
		return false;

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	RmlGL2::Initialize();
	RmlGL2::SetViewport(width, height);

	SetupBackendCallbacks();

	return true;
}

void Backend::CloseWindow()
{
	RmlGL2::Shutdown();

	RmlGLFW::CloseWindow();
	RmlGLFW::Shutdown();

	window = nullptr;
}

void Backend::EventLoop(ShellIdleFunction idle_function)
{
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		idle_function();
	}
}

void Backend::RequestExit()
{
	glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void Backend::BeginFrame()
{
	RmlGL2::BeginFrame();
	RmlGL2::Clear();
}

void Backend::PresentFrame()
{
	RmlGL2::EndFrame();
	glfwSwapBuffers(window);
}

void Backend::SetContext(Rml::Context* new_context)
{
	context = new_context;
	RmlGLFW::SetContext(new_context);
}

static void SetupBackendCallbacks()
{
	// Override the default key event callback to add global shortcuts for the samples.
	glfwSetKeyCallback(window, [](GLFWwindow* /*window*/, int key, int /*scancode*/, int action, int mods) {
		RmlGLFW::SetActiveModifiers(mods);

		switch (action)
		{
		case GLFW_PRESS:
		case GLFW_REPEAT:
			ProcessKeyDown(key, action, mods);
			break;
		case GLFW_RELEASE:
			RmlGLFW::ProcessKeyCallback(key, action, mods);
			break;
		}
	});

	// Override the framebuffer size callback, so that we can set the OpenGL viewport as well.
	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* /*window*/, int width, int height) {
		RmlGL2::SetViewport(width, height);
		RmlGLFW::ProcessFramebufferSizeCallback(width, height);
	});
}

static void ProcessKeyDown(int glfw_key, int glfw_action, int glfw_mods)
{
	if (!context)
		return;

	Rml::Input::KeyIdentifier key_identifier = RmlGLFW::ConvertKey(glfw_key);
	const int key_modifier_state = RmlGLFW::ConvertKeyModifiers(glfw_mods);

	// Toggle debugger and set dp-ratio using Ctrl +/-/0 keys. These global shortcuts take priority.
	if (key_identifier == Rml::Input::KI_F8)
	{
		Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
	}
	else if (key_identifier == Rml::Input::KI_0 && key_modifier_state & Rml::Input::KM_CTRL)
	{
		context->SetDensityIndependentPixelRatio(1.f);
	}
	else if (key_identifier == Rml::Input::KI_1 && key_modifier_state & Rml::Input::KM_CTRL)
	{
		context->SetDensityIndependentPixelRatio(1.f);
	}
	else if ((key_identifier == Rml::Input::KI_OEM_MINUS || key_identifier == Rml::Input::KI_SUBTRACT) && key_modifier_state & Rml::Input::KM_CTRL)
	{
		const float new_dp_ratio = Rml::Math::Max(context->GetDensityIndependentPixelRatio() / 1.2f, 0.5f);
		context->SetDensityIndependentPixelRatio(new_dp_ratio);
	}
	else if ((key_identifier == Rml::Input::KI_OEM_PLUS || key_identifier == Rml::Input::KI_ADD) && key_modifier_state & Rml::Input::KM_CTRL)
	{
		const float new_dp_ratio = Rml::Math::Min(context->GetDensityIndependentPixelRatio() * 1.2f, 2.5f);
		context->SetDensityIndependentPixelRatio(new_dp_ratio);
	}
	else
	{
		// No global shortcuts detected, submit the key to platform handler.
		if (RmlGLFW::ProcessKeyCallback(glfw_key, glfw_action, glfw_mods))
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
