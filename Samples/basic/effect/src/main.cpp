/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2018 Michael R. P. Ragazzon
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

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <Shell.h>

Rml::Context* context = nullptr;

void GameLoop()
{
	context->Update();

	Shell::BeginFrame();
	context->Render();
	Shell::PresentFrame();
}

#if defined RMLUI_PLATFORM_WIN32
	#include <RmlUi_Include_Windows.h>
int APIENTRY WinMain(HINSTANCE /*instance_handle*/, HINSTANCE /*previous_instance_handle*/, char* /*command_line*/, int /*command_show*/)
#else
int main(int /*argc*/, char** /*argv*/)
#endif
{
	const int width = 1024;
	const int height = 768;

	// Initializes and sets the system and render interfaces, creates a window, and attaches the renderer.
	if (!Shell::Initialize() || !Shell::OpenWindow("Effect Sample", width, height, true))
	{
		Shell::Shutdown();
		return -1;
	}

	// RmlUi initialisation.
	Rml::Initialise();

	// Create the main RmlUi context.
	context = Rml::CreateContext("main", Rml::Vector2i(width, height));
	if (!context)
	{
		Rml::Shutdown();
		Shell::Shutdown();
		return -1;
	}

	Rml::Debugger::Initialise(context);
	Shell::SetContext(context);
	Shell::LoadFonts();

	static constexpr float perspective_max = 3000.f;

	struct EffectData {
		bool show_menu = false;
		Rml::String submenu = "filter";

		struct Transform {
			float scale = 1.0f;
			Rml::Vector3f rotate;
			float perspective = perspective_max;
			Rml::Vector2f perspective_origin = Rml::Vector2f(50.f);
			bool transform_all = false;
		} transform;

		struct Filter {
			float opacity = 1.0f;
			float sepia = 0.0f;
			float grayscale = 0.0f;
			float brightness = 1.0f;
			float contrast = 1.0f;
			float invert = 0.0f;
			float blur = 0.0f;
			bool drop_shadow = false;
		} filter;
	} data;

	if (Rml::DataModelConstructor constructor = context->CreateDataModel("effects"))
	{
		constructor.Bind("show_menu", &data.show_menu);
		constructor.Bind("submenu", &data.submenu);

		constructor.Bind("scale", &data.transform.scale);
		constructor.Bind("rotate_x", &data.transform.rotate.x);
		constructor.Bind("rotate_y", &data.transform.rotate.y);
		constructor.Bind("rotate_z", &data.transform.rotate.z);
		constructor.Bind("perspective", &data.transform.perspective);
		constructor.Bind("perspective_origin_x", &data.transform.perspective_origin.x);
		constructor.Bind("perspective_origin_y", &data.transform.perspective_origin.y);
		constructor.Bind("transform_all", &data.transform.transform_all);

		constructor.Bind("opacity", &data.filter.opacity);
		constructor.Bind("sepia", &data.filter.sepia);
		constructor.Bind("grayscale", &data.filter.grayscale);
		constructor.Bind("brightness", &data.filter.brightness);
		constructor.Bind("contrast", &data.filter.contrast);
		constructor.Bind("invert", &data.filter.invert);
		constructor.Bind("blur", &data.filter.blur);
		constructor.Bind("drop_shadow", &data.filter.drop_shadow);

		constructor.BindEventCallback("reset", [&data](Rml::DataModelHandle handle, Rml::Event& /*ev*/, const Rml::VariantList& /*arguments*/) {
			if (data.submenu == "transform")
				data.transform = EffectData::Transform{};
			else if (data.submenu == "filter")
				data.filter = EffectData::Filter{};
			handle.DirtyAllVariables();
		});
	}

	// Load and show the tutorial document.
	if (Rml::ElementDocument* document = context->LoadDocument("basic/effect/data/effect.rml"))
		document->Show();

	Shell::EventLoop(GameLoop);

	// Shutdown RmlUi.
	Rml::Shutdown();

	Shell::CloseWindow();
	Shell::Shutdown();

	return 0;
}
