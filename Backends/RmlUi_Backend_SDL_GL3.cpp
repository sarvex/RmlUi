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
#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_GL3.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Debugger/Debugger.h>
#include <SDL.h>
#include <SDL_image.h>

#if defined RMLUI_PLATFORM_EMSCRIPTEN
	#include <emscripten.h>
#else
	#if !(SDL_VIDEO_RENDER_OGL)
		#error "Only the OpenGL SDL backend is supported."
	#endif
#endif

class RenderInterface_GL3_SDL;

static SDL_Window* window = nullptr;
static SDL_GLContext glcontext = nullptr;

static Rml::Context* context = nullptr;
static int window_width = 0;
static int window_height = 0;
static bool running = false;

static Rml::UniquePtr<RenderInterface_GL3_SDL> render_interface;
static Rml::UniquePtr<SystemInterface_SDL> system_interface;

static void ProcessKeyDown(SDL_Event& event, Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state);

class RenderInterface_GL3_SDL : public RenderInterface_GL3 {
public:
	RenderInterface_GL3_SDL() {}

	bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
};

bool RenderInterface_GL3_SDL::LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	Rml::FileInterface* file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
		return false;

	file_interface->Seek(file_handle, 0, SEEK_END);
	const size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	using Rml::byte;
	Rml::UniquePtr<byte[]> buffer(new byte[buffer_size]);
	file_interface->Read(buffer.get(), buffer_size, file_handle);
	file_interface->Close(file_handle);

	const size_t i = source.rfind('.');
	Rml::String extension = (i == Rml::String::npos ? Rml::String() : source.substr(i + 1));

	SDL_Surface* surface = IMG_LoadTyped_RW(SDL_RWFromMem(buffer.get(), int(buffer_size)), 1, extension.c_str());

	bool success = false;
	if (surface)
	{
		texture_dimensions.x = surface->w;
		texture_dimensions.y = surface->h;

		if (surface->format->format != SDL_PIXELFORMAT_RGBA32)
		{
			SDL_SetSurfaceAlphaMod(surface, SDL_ALPHA_OPAQUE);
			SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);

			SDL_Surface* new_surface = SDL_CreateRGBSurfaceWithFormat(0, surface->w, surface->h, 32, SDL_PIXELFORMAT_RGBA32);
			if (!new_surface)
				return false;

			if (SDL_BlitSurface(surface, 0, new_surface, 0) != 0)
				return false;

			SDL_FreeSurface(surface);
			surface = new_surface;
		}

		success = RenderInterface_GL3::GenerateTexture(texture_handle, (const Rml::byte*)surface->pixels, texture_dimensions);
		SDL_FreeSurface(surface);
	}

	return success;
}

static void UpdateWindowDimensions(int width = 0, int height = 0)
{
	if (width > 0)
		window_width = width;
	if (height > 0)
		window_height = height;
	if (context)
		context->SetDimensions(Rml::Vector2i(window_width, window_height));

	RmlGL3::SetViewport(window_width, window_height);
}

bool Backend::InitializeInterfaces()
{
	RMLUI_ASSERT(!system_interface && !render_interface);

	system_interface = Rml::MakeUnique<SystemInterface_SDL>();
	Rml::SetSystemInterface(system_interface.get());

	render_interface = Rml::MakeUnique<RenderInterface_GL3_SDL>();
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
	if (!RmlSDL::Initialize())
		return false;

#if defined RMLUI_PLATFORM_EMSCRIPTEN
	// GLES 3.0 (WebGL 2.0)
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	// GL 3.3 Core
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

	// As opposed to the GL2 renderer we don't need to specify any GL window attributes, because here we use our own frame buffers for rendering.

	const Uint32 window_flags = SDL_WINDOW_OPENGL;
	window = nullptr;
	if (!RmlSDL::CreateWindow(name, width, height, allow_resize, window_flags, window))
	{
		fprintf(stderr, "SDL error on create window: %s\n", SDL_GetError());
		return false;
	}

	glcontext = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, glcontext);
	SDL_GL_SetSwapInterval(1);

	if (!RmlGL3::Initialize())
	{
		fprintf(stderr, "Could not initialize OpenGL");
		return false;
	}

	UpdateWindowDimensions(width, height);

	return true;
}

void Backend::CloseWindow()
{
	RmlGL3::Shutdown();

	SDL_GL_DeleteContext(glcontext);

	glcontext = nullptr;

	RmlSDL::CloseWindow();
	RmlSDL::Shutdown();
}

static void EventLoopIteration(void* idle_function_ptr)
{
	ShellIdleFunction idle_function = (ShellIdleFunction)idle_function_ptr;
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_QUIT:
			running = false;
			break;
		case SDL_KEYDOWN:
			// Intercept keydown events to handle global sample shortcuts.
			ProcessKeyDown(event, RmlSDL::ConvertKey(event.key.keysym.sym), RmlSDL::GetKeyModifierState());
			break;
		case SDL_WINDOWEVENT:
			switch (event.window.event)
			{
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				UpdateWindowDimensions(event.window.data1, event.window.data2);
				break;
			}
			break;
		default:
			RmlSDL::EventHandler(event);
			break;
		}
	}

	idle_function();
}

void Backend::EventLoop(ShellIdleFunction idle_function)
{
	running = true;

#if defined RMLUI_PLATFORM_EMSCRIPTEN

	// Hand over control of the main loop to the WebAssembly runtime.
	emscripten_set_main_loop_arg(EventLoopIteration, (void*)idle_function, 0, true);

#else

	while (running)
		EventLoopIteration((void*)idle_function);

#endif
}

void Backend::RequestExit()
{
	running = false;
}

void Backend::BeginFrame()
{
	RmlGL3::Clear();
	RmlGL3::BeginFrame();
}

void Backend::PresentFrame()
{
	RmlGL3::EndFrame();

	SDL_GL_SwapWindow(window);
}

void Backend::SetContext(Rml::Context* new_context)
{
	context = new_context;
	RmlSDL::SetContextForInput(new_context);
	UpdateWindowDimensions();
}

static void ProcessKeyDown(SDL_Event& event, Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state)
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
		context->SetDensityIndependentPixelRatio(1.f);
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
		// No global shortcuts detected, submit the key to platform handler.
		if (RmlSDL::EventHandler(event))
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
