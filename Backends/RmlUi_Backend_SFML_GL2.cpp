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
#include "RmlUi_Platform_SFML.h"
#include "RmlUi_Renderer_GL2.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Debugger/Debugger.h>
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>

class RenderInterface_GL2_SFML;

static Rml::Context* context = nullptr;
static int window_width = 0;
static int window_height = 0;
static bool running = false;
static sf::RenderWindow* render_window = nullptr;

static Rml::UniquePtr<RenderInterface_GL2_SFML> render_interface;
static Rml::UniquePtr<SystemInterface_SFML> system_interface;

static void ProcessKeyDown(sf::Event& event, Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state);

class RenderInterface_GL2_SFML : public RenderInterface_GL2 {
public:
	RenderInterface_GL2_SFML() {}

	void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture,
		const Rml::Vector2f& translation) override;

	bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture_handle) override;
};

void RenderInterface_GL2_SFML::RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::TextureHandle texture,
	const Rml::Vector2f& translation)
{
	if (texture)
	{
		sf::Texture::bind((sf::Texture*)texture);
		texture = RenderInterface_GL2::TextureIgnoreBinding;
	}

	RenderInterface_GL2::RenderGeometry(vertices, num_vertices, indices, num_indices, texture, translation);
}

bool RenderInterface_GL2_SFML::LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source)
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

	sf::Texture* texture = new sf::Texture();
	texture->setSmooth(true);

	bool success = texture->loadFromMemory(buffer, buffer_size);

	delete[] buffer;

	if (success)
	{
		texture_handle = (Rml::TextureHandle)texture;
		texture_dimensions = Rml::Vector2i(texture->getSize().x, texture->getSize().y);
	}
	else
	{
		delete texture;
	}

	return success;
}

bool RenderInterface_GL2_SFML::GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions)
{
	sf::Texture* texture = new sf::Texture();
	texture->setSmooth(true);

	if (!texture->create(source_dimensions.x, source_dimensions.y))
	{
		delete texture;
		return false;
	}

	texture->update(source, source_dimensions.x, source_dimensions.y, 0, 0);
	texture_handle = (Rml::TextureHandle)texture;

	return true;
}

void RenderInterface_GL2_SFML::ReleaseTexture(Rml::TextureHandle texture_handle)
{
	delete (sf::Texture*)texture_handle;
}

static void UpdateWindowDimensions(int width = 0, int height = 0)
{
	if (width > 0)
		window_width = width;
	if (height > 0)
		window_height = height;
	if (context)
		context->SetDimensions(Rml::Vector2i(window_width, window_height));

	if (window_width > 0 && window_height > 0)
	{
		sf::View view(sf::FloatRect(0.f, 0.f, (float)window_width, (float)window_height));
		render_window->setView(view);

		RmlGL2::SetViewport(window_width, window_height);
	}
}

bool Backend::InitializeInterfaces()
{
	RMLUI_ASSERT(!system_interface && !render_interface);

	system_interface = Rml::MakeUnique<SystemInterface_SFML>();
	Rml::SetSystemInterface(system_interface.get());

	render_interface = Rml::MakeUnique<RenderInterface_GL2_SFML>();
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
	if (!RmlSFML::Initialize())
		return false;

	if (!RmlSFML::CreateWindow(name, width, height, allow_resize, render_window) || !render_window)
		return false;

	render_window->setVerticalSyncEnabled(true);

	if (!render_window->isOpen())
		return false;

	RmlGL2::Initialize();
	UpdateWindowDimensions(width, height);

	return true;
}

void Backend::CloseWindow()
{
	RmlGL2::Shutdown();

	RmlSFML::CloseWindow();
	RmlSFML::Shutdown();

	context = nullptr;
	render_window = nullptr;
	window_width = 0;
	window_height = 0;
}

void Backend::EventLoop(ShellIdleFunction idle_function)
{
	running = true;
	sf::Event event;

	while (running)
	{
		while (render_window->pollEvent(event))
		{
			switch (event.type)
			{
			case sf::Event::Resized:
				UpdateWindowDimensions(render_window->getSize().x, render_window->getSize().y);
				break;
			case sf::Event::KeyPressed:
				ProcessKeyDown(event, RmlSFML::ConvertKey(event.key.code), RmlSFML::GetKeyModifierState());
				break;
			case sf::Event::Closed:
				running = false;
				break;
			default:
				RmlSFML::EventHandler(event);
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
	render_window->resetGLStates();
	render_window->clear();

	RmlGL2::BeginFrame();

#if 0
	// Draw a simple shape with SFML for demonstration purposes. Make sure to push and pop GL states as appropriate.
	sf::Vector2f circle_position(100.f, 100.f);

	render_window->pushGLStates();

	sf::CircleShape circle(50.f);
	circle.setPosition(circle_position);
	circle.setFillColor(sf::Color::Blue);
	circle.setOutlineColor(sf::Color::Red);
	circle.setOutlineThickness(10.f);
	render_window->draw(circle);

	render_window->popGLStates();
#endif
}

void Backend::PresentFrame()
{
	RmlGL2::EndFrame();
	render_window->display();
}

void Backend::SetContext(Rml::Context* new_context)
{
	context = new_context;
	RmlSFML::SetContextForInput(new_context);
	UpdateWindowDimensions();
}

static void ProcessKeyDown(sf::Event& event, Rml::Input::KeyIdentifier key_identifier, const int key_modifier_state)
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
		if (RmlSFML::EventHandler(event))
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
