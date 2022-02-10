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

#include "DecoratorBasicFilter.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "ComputeProperty.h"

namespace Rml {

DecoratorBasicFilter::DecoratorBasicFilter() {}

DecoratorBasicFilter::~DecoratorBasicFilter() {}

bool DecoratorBasicFilter::Initialise(const String& in_name, float in_value)
{
	name = in_name;
	value = in_value;
	return true;
}

DecoratorDataHandle DecoratorBasicFilter::GenerateElementData(Element* element) const
{
	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return INVALID_DECORATORDATAHANDLE;

	CompiledEffectHandle handle = render_interface->CompileEffect(name, Dictionary{{"value", Variant(value)}});

	return DecoratorDataHandle(handle);
}

void DecoratorBasicFilter::ReleaseElementData(DecoratorDataHandle handle) const
{
	// TODO: Get the render interface from element
	// RenderInterface* render_interface = element->GetRenderInterface();
	RenderInterface* render_interface = ::Rml::GetRenderInterface();
	if (!render_interface)
		return;
	render_interface->ReleaseCompiledEffect(CompiledEffectHandle(handle));
}

void DecoratorBasicFilter::RenderElement(Element* /*element*/, DecoratorDataHandle /*element_data*/) const
{
	RMLUI_ERROR;
}

void DecoratorBasicFilter::RenderElement(Element* element, DecoratorDataHandle element_data, RenderStage render_stage) const
{
	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return;

	render_interface->RenderEffect(CompiledEffectHandle(element_data), render_stage, 0, element);
}

DecoratorBasicFilterInstancer::DecoratorBasicFilterInstancer()
{
	ids.value = RegisterProperty("value", "1").AddParser("number_percent").GetId();
	RegisterShorthand("decorator", "value", ShorthandType::FallThrough);
}

DecoratorBasicFilterInstancer::~DecoratorBasicFilterInstancer() {}

SharedPtr<Decorator> DecoratorBasicFilterInstancer::InstanceDecorator(const String& name, const PropertyDictionary& properties_,
	const DecoratorInstancerInterface& /*interface_*/)
{
	const Property* p_value = properties_.GetProperty(ids.value);
	if (!p_value)
		return nullptr;

	float value = p_value->Get<float>();
	if (p_value->unit == Property::PERCENT)
		value *= 0.01f;

	auto decorator = MakeShared<DecoratorBasicFilter>();
	if (decorator->Initialise(name, value))
		return decorator;

	return nullptr;
}

} // namespace Rml
