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

#include "DecoratorShader.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/GeometryUtilities.h"
#include "../../Include/RmlUi/Core/Math.h"
#include "../../Include/RmlUi/Core/PropertyDefinition.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "DecoratorBasicFilter.h"

namespace Rml {

DecoratorShader::DecoratorShader() {}

DecoratorShader::~DecoratorShader() {}

bool DecoratorShader::Initialise(String&& in_value, Box::Area in_render_area)
{
	value = std::move(in_value);
	render_area = in_render_area;
	return true;
}

DecoratorDataHandle DecoratorShader::GenerateElementData(Element* element) const
{
	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return INVALID_DECORATORDATAHANDLE;

	const Vector2f dimensions = element->GetBox().GetSize(render_area);
	CompiledEffectHandle effect_handle =
		render_interface->CompileEffect("shader", Dictionary{{"value", Variant(value)}, {"dimensions", Variant(dimensions)}});

	// TODO: Background geometry with radius (geometry caching in element background?)
	Vertex vertices[4];
	int indices[6];
	GeometryUtilities::GenerateQuad(vertices, indices, Vector2f(), dimensions, Colourb(255), Vector2f(), Vector2f(1));
	CompiledGeometryHandle geometry_handle = render_interface->CompileGeometry(vertices, 4, indices, 6, TextureHandle{});
	BasicEffectElementData* element_data = GetBasicEffectElementDataPool().AllocateAndConstruct(render_interface, effect_handle, geometry_handle);

	return reinterpret_cast<DecoratorDataHandle>(element_data);
}

void DecoratorShader::ReleaseElementData(DecoratorDataHandle handle) const
{
	BasicEffectElementData* element_data = reinterpret_cast<BasicEffectElementData*>(handle);
	RMLUI_ASSERT(element_data && element_data->render_interface);

	element_data->render_interface->ReleaseCompiledEffect(element_data->effect);
	element_data->render_interface->ReleaseCompiledGeometry(element_data->geometry);

	GetBasicEffectElementDataPool().DestroyAndDeallocate(element_data);
}

void DecoratorShader::RenderElement(Element* element, DecoratorDataHandle handle) const
{
	BasicEffectElementData* element_data = reinterpret_cast<BasicEffectElementData*>(handle);
	element_data->render_interface->RenderEffect(element_data->effect, element_data->geometry, element->GetAbsoluteOffset(render_area));
}

DecoratorShaderInstancer::DecoratorShaderInstancer() : DecoratorInstancer(DecoratorClasses::Background), ids{}
{
	ids.value = RegisterProperty("value", String()).AddParser("string").GetId();
	ids.render_area = RegisterProperty("render-area", String("padding-box")).AddParser("keyword", "border-box=1, padding-box, content-box").GetId();
	RegisterShorthand("decorator", "render-area,value", ShorthandType::FallThrough);
}

DecoratorShaderInstancer::~DecoratorShaderInstancer() {}

SharedPtr<Decorator> DecoratorShaderInstancer::InstanceDecorator(const String& /*name*/, const PropertyDictionary& properties_,
	const DecoratorInstancerInterface& /*interface_*/)
{
	const Property* p_value = properties_.GetProperty(ids.value);
	const Property* p_render_area = properties_.GetProperty(ids.render_area);
	if (!p_value || !p_render_area)
		return nullptr;

	String value = p_value->Get<String>();
	Box::Area render_area = (Box::Area)Math::Clamp(p_render_area->Get<int>(), (int)Box::BORDER, (int)Box::CONTENT);

	auto decorator = MakeShared<DecoratorShader>();
	if (decorator->Initialise(std::move(value), render_area))
		return decorator;

	return nullptr;
}

} // namespace Rml
