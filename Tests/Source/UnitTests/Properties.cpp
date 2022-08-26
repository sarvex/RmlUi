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

#include "../Common/TestsInterface.h"
#include "../Common/TestsShell.h"
#include "../Common/TypesToString.h"
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StyleSheetTypes.h>
#include <doctest.h>

using namespace Rml;

TEST_CASE("Properties.flex")
{
	const Vector2i window_size(1024, 768);

	TestsSystemInterface system_interface;
	TestsRenderInterface render_interface;

	SetRenderInterface(&render_interface);
	SetSystemInterface(&system_interface);

	Rml::Initialise();

	Context* context = Rml::CreateContext("main", window_size);
	ElementDocument* document = context->CreateDocument();

	struct FlexTestCase {
		String flex_value;

		struct ExpectedValues {
			float flex_grow;
			float flex_shrink;
			String flex_basis;
		} expected;
	};

	FlexTestCase tests[] = {
		{"", {0.f, 1.f, "auto"}},
		{"none", {0.f, 0.f, "auto"}},
		{"auto", {1.f, 1.f, "auto"}},
		{"1", {1.f, 1.f, "0px"}},
		{"2", {2.f, 1.f, "0px"}},
		{"2 0", {2.f, 0.f, "0px"}},
		{"2 3", {2.f, 3.f, "0px"}},
		{"2 auto", {2.f, 1.f, "auto"}},
		{"2 0 auto", {2.f, 0.f, "auto"}},
		{"0 0 auto", {0.f, 0.f, "auto"}},
		{"0 0 50px", {0.f, 0.f, "50px"}},
		{"0 0 50px", {0.f, 0.f, "50px"}},
		{"0 0 0", {0.f, 0.f, "0px"}},
	};

	for (const FlexTestCase& test : tests)
	{
		if (!test.flex_value.empty())
		{
			CHECK(document->SetProperty("flex", test.flex_value));
		}

		CHECK(document->GetProperty<float>("flex-grow") == test.expected.flex_grow);
		CHECK(document->GetProperty<float>("flex-shrink") == test.expected.flex_shrink);
		CHECK(document->GetProperty("flex-basis")->ToString() == test.expected.flex_basis);
	}

	Rml::Shutdown();
}

static const String document_background = R"(
<rml>
<head>
	<style>
		body {
			left: 0;
			top: 0;
			right: 0;
			bottom: 0;
		}
		div {
			display: block;
			height: 128px;
			width: 128px;
			%s;
		}
	</style>
</head>

<body>
<div/>
</body>
</rml>
)";

TEST_CASE("Properties.background")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	struct BackgroundTestCase {
		String style;

		struct ExpectedValues {
			Colourb background_color;
			int num_decorators;
			int num_warnings;
		} expected;
	};

	const Colourb transparent(0, 0);
	const Colourb blue(0, 0, 255);

	// clang-format off
	BackgroundTestCase tests[] = {
		{"",                                                   {transparent, 0, 0}},
		{"background: blue",                                   {blue,        0, 0}},
		{"background: none",                                   {transparent, 0, 0}},
		{"background: image(url.png)",                         {transparent, 1, 0}},
		{"background: image(url.png), blue",                   {blue,        1, 0}},
		{"background: blue, image(url.png)",                   {transparent, 2, 1}},
		{"background: blue; background: none",                 {transparent, 0, 0}},
		{"background: none, blue",                             {blue,        0, 0}},
		{"background: blue; background: none",                 {transparent, 0, 0}},
		{"background: blue; background: image(url.png)",       {transparent, 1, 0}},
		{"background: image(url.png); background: blue",       {blue,        1, 0}},
		{"background: image(url.png); background: none, blue", {blue,        0, 0}},
		{"background: image(url.png) border-box, none, blue;", {blue,        2, 1}},
		{"background: image(url.png) border-box, tiled-horizontal(a, b, c);",       {transparent, 2, 0}},
		{"background: image(url.png) border-box, tiled-horizontal(a, b, c), blue;", {blue,        2, 0}},
	};
	// clang-format on

	for (const BackgroundTestCase& test : tests)
	{
		INFO(test.style);
		TestsShell::SetNumExpectedWarnings(test.expected.num_warnings);

		const String document_str =
			CreateString(document_background.size() + test.style.size() + 64, document_background.c_str(), test.style.c_str());

		ElementDocument* document = context->LoadDocumentFromMemory(document_str);
		document->Show();
		TestsShell::RenderLoop();

		Element* element = document->GetChild(0);
		const Colourb background_color = element->GetComputedValues().background_color();
		CHECK(background_color == test.expected.background_color);

		int num_decorators = 0;
		const Property* property = element->GetLocalProperty(PropertyId::Decorator);
		if (property && property->unit == Unit::DECORATOR)
		{
			if (DecoratorsPtr decorators_ptr = property->Get<DecoratorsPtr>())
				num_decorators = (int)decorators_ptr->list.size();
		}

		CHECK(num_decorators == test.expected.num_decorators);

		document->Close();
	}

	TestsShell::ShutdownShell();
}
