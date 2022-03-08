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
#include "RmlUi_Renderer_GL2.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Debugger/Debugger.h>
#include <GL/glew.h>
#include <SDL.h>
#include <SDL_image.h>

#if !(SDL_VIDEO_RENDER_OGL)
	#error "Only the OpenGL SDL backend is supported."
#endif

class RenderInterface_GL2_SDL;

static SDL_Renderer* renderer = nullptr;
static SDL_GLContext glcontext = nullptr;

static Rml::Context* context = nullptr;
static int window_width = 0;
static int window_height = 0;
static bool running = false;

static Rml::UniquePtr<RenderInterface_GL2_SDL> render_interface;
static Rml::UniquePtr<SystemInterface_SDL> system_interface;

static void ProcessKeyDown(SDL_Event& event, Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state);

class RenderInterface_GL2_SDL : public RenderInterface_GL2 {
public:
	RenderInterface_GL2_SDL() {}

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture,
		const Rml::Vector2f& translation) override;

	bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture_handle) override;
};

void RenderInterface_GL2_SDL::RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture,
	const Rml::Vector2f& translation)
{
	SDL_Texture* sdl_texture = (SDL_Texture*)texture;
	if (sdl_texture)
	{
		SDL_GL_BindTexture(sdl_texture, nullptr, nullptr);
		texture = RenderInterface_GL2::TextureIgnoreBinding;
	}

	RenderInterface_GL2::RenderGeometry(vertices, num_vertices, indices, num_indices, texture, translation);

	if (sdl_texture)
	{
		SDL_GL_UnbindTexture(sdl_texture);
	}
}

bool RenderInterface_GL2_SDL::LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	Rml::FileInterface* file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
		return false;

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	char* buffer = new char[buffer_size];
	file_interface->Read(buffer, buffer_size, file_handle);
	file_interface->Close(file_handle);

	const size_t i = source.rfind('.');
	Rml::String extension = (i == Rml::String::npos ? Rml::String() : source.substr(i + 1));

	SDL_Surface* surface = IMG_LoadTyped_RW(SDL_RWFromMem(buffer, int(buffer_size)), 1, extension.c_str());

	bool success = false;

	if (surface)
	{
		SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

		if (texture)
		{
			texture_handle = (Rml::TextureHandle)texture;
			texture_dimensions = Rml::Vector2i(surface->w, surface->h);
			success = true;
		}

		SDL_FreeSurface(surface);
	}

	delete[] buffer;

	return success;
}

bool RenderInterface_GL2_SDL::GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions)
{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	Uint32 rmask = 0xff000000;
	Uint32 gmask = 0x00ff0000;
	Uint32 bmask = 0x0000ff00;
	Uint32 amask = 0x000000ff;
#else
	Uint32 rmask = 0x000000ff;
	Uint32 gmask = 0x0000ff00;
	Uint32 bmask = 0x00ff0000;
	Uint32 amask = 0xff000000;
#endif

	SDL_Surface* surface =
		SDL_CreateRGBSurfaceFrom((void*)source, source_dimensions.x, source_dimensions.y, 32, source_dimensions.x * 4, rmask, gmask, bmask, amask);
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_FreeSurface(surface);
	texture_handle = (Rml::TextureHandle)texture;
	return true;
}

void RenderInterface_GL2_SDL::ReleaseTexture(Rml::TextureHandle texture_handle)
{
	SDL_DestroyTexture((SDL_Texture*)texture_handle);
}

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

bool Backend::InitializeInterfaces()
{
	RMLUI_ASSERT(!system_interface && !render_interface);

	system_interface = Rml::MakeUnique<SystemInterface_SDL>();
	Rml::SetSystemInterface(system_interface.get());

	render_interface = Rml::MakeUnique<RenderInterface_GL2_SDL>();
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

	// Request stencil buffer of at least 8-bit size to supporting clipping on transformed elements.
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	// Enable linear filtering and MSAA for better-looking visuals, especially when transforms are applied.
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);

	const Uint32 window_flags = SDL_WINDOW_OPENGL;
	SDL_Window* window = nullptr;
	if (!RmlSDL::CreateWindow(name, width, height, allow_resize, window_flags, window))
	{
		// Try again on low-quality settings.
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

		if (!RmlSDL::CreateWindow(name, width, height, allow_resize, window_flags, window))
		{
			fprintf(stderr, "SDL error on create window: %s\n", SDL_GetError());
			return false;
		}
	}

	glcontext = SDL_GL_CreateContext(window);
	int oglIdx = -1;
	int nRD = SDL_GetNumRenderDrivers();
	for (int i = 0; i < nRD; i++)
	{
		SDL_RendererInfo info;
		if (!SDL_GetRenderDriverInfo(i, &info))
		{
			if (!strcmp(info.name, "opengl"))
			{
				oglIdx = i;
			}
		}
	}

	renderer = SDL_CreateRenderer(window, oglIdx, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer)
		return false;

	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
		fprintf(stderr, "GLEW error: %s\n", glewGetErrorString(err));
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	RmlGL2::Initialize();
	UpdateWindowDimensions(width, height);

	return true;
}

void Backend::CloseWindow()
{
	RmlGL2::Shutdown();

	SDL_DestroyRenderer(renderer);
	SDL_GL_DeleteContext(glcontext);

	renderer = nullptr;
	glcontext = nullptr;

	RmlSDL::CloseWindow();
	RmlSDL::Shutdown();
}

void Backend::EventLoop(ShellIdleFunction idle_function)
{
	running = true;

	while (running)
	{
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
}

void Backend::RequestExit()
{
	running = false;
}

void Backend::BeginFrame()
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	// SDL uses shaders that we need to disable here.
	glUseProgramObjectARB(0);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	RmlGL2::BeginFrame();
}

void Backend::PresentFrame()
{
	RmlGL2::EndFrame();

	// Draw a fake point just outside the screen to let SDL know that it needs to reset its state in case it wants to render a texture next frame.
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_RenderDrawPoint(renderer, -1, -1);

	SDL_RenderPresent(renderer);
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
