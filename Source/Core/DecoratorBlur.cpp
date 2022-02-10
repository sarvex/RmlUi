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

#include "DecoratorBlur.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "ComputeProperty.h"

namespace Rml {

DecoratorBlur::DecoratorBlur() {}

DecoratorBlur::~DecoratorBlur() {}

bool DecoratorBlur::Initialise(float in_radius)
{
	radius = in_radius;
	return true;
}

DecoratorDataHandle DecoratorBlur::GenerateElementData(Element* element) const
{
	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return INVALID_DECORATORDATAHANDLE;

	CompiledEffectHandle handle = render_interface->CompileEffect("blur", Dictionary{{String("radius"), Variant(radius)}});

	return DecoratorDataHandle(handle);
}

void DecoratorBlur::ReleaseElementData(DecoratorDataHandle handle) const
{
	// TODO: Get the render interface from element
	// RenderInterface* render_interface = element->GetRenderInterface();
	RenderInterface* render_interface = ::Rml::GetRenderInterface();
	if (!render_interface)
		return;
	render_interface->ReleaseCompiledEffect(CompiledEffectHandle(handle));
}

void DecoratorBlur::RenderElement(Element* /*element*/, DecoratorDataHandle /*element_data*/) const
{
	RMLUI_ERROR;
}

void DecoratorBlur::RenderElement(Element* element, DecoratorDataHandle element_data, RenderStage render_stage) const
{
	if (render_stage != RenderStage::Enter && render_stage != RenderStage::Exit && render_stage != RenderStage::BeforeDecoration)
		return;

	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return;

	render_interface->RenderEffect(CompiledEffectHandle(element_data), render_stage, 0, element);
}

DecoratorBlurInstancer::DecoratorBlurInstancer()
{
	// register properties for the decorator
	ids.radius = RegisterProperty("radius", "0px").AddParser("length").GetId();
	RegisterShorthand("decorator", "radius", ShorthandType::FallThrough);
}

DecoratorBlurInstancer::~DecoratorBlurInstancer() {}

SharedPtr<Decorator> DecoratorBlurInstancer::InstanceDecorator(const String& /*name*/, const PropertyDictionary& properties_,
	const DecoratorInstancerInterface& /*interface_*/)
{
	const Property* p_radius = properties_.GetProperty(ids.radius);
	if (!p_radius)
		return nullptr;

	// TODO dp/vp
	const float radius = ComputeAbsoluteLength(*p_radius, 1.f, Vector2f(0.f));

	auto decorator = MakeShared<DecoratorBlur>();
	if (decorator->Initialise(radius))
		return decorator;

	return nullptr;
}

} // namespace Rml
