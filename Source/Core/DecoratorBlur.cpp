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
#include "../../Include/RmlUi/Core/PropertyDefinition.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "ComputeProperty.h"
#include "DecoratorElementData.h"

namespace Rml {

DecoratorBlur::DecoratorBlur() {}

DecoratorBlur::~DecoratorBlur() {}

bool DecoratorBlur::Initialise(NumericValue in_radius)
{
	radius_value = in_radius;
	return Any(in_radius.unit & Unit::LENGTH);
}

DecoratorDataHandle DecoratorBlur::GenerateElementData(Element* element) const
{
	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return INVALID_DECORATORDATAHANDLE;

	const float radius = element->ResolveLength(radius_value);
	CompiledFilterHandle handle = render_interface->CompileFilter("blur", Dictionary{{"radius", Variant(radius)}});

	BasicFilterElementData* element_data = GetBasicFilterElementDataPool().AllocateAndConstruct(render_interface, handle);
	return reinterpret_cast<DecoratorDataHandle>(element_data);
}

void DecoratorBlur::ReleaseElementData(DecoratorDataHandle handle) const
{
	BasicFilterElementData* element_data = reinterpret_cast<BasicFilterElementData*>(handle);
	RMLUI_ASSERT(element_data && element_data->render_interface);

	element_data->render_interface->ReleaseCompiledFilter(element_data->filter);
	GetBasicFilterElementDataPool().DestroyAndDeallocate(element_data);
}

void DecoratorBlur::RenderElement(Element* /*element*/, DecoratorDataHandle handle) const
{
	BasicFilterElementData* element_data = reinterpret_cast<BasicFilterElementData*>(handle);
	element_data->render_interface->AttachFilter(element_data->filter);
}

void DecoratorBlur::ModifyScissorRegion(Element* element, Rectanglef& scissor_region) const
{
	const float radius = element->ResolveLength(radius_value);
	const float blur_extent = 1.5f * Math::Max(radius, 1.f);
	scissor_region.Extend(blur_extent);
}

DecoratorBlurInstancer::DecoratorBlurInstancer() : DecoratorInstancer(DecoratorClass::Filter)
{
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

	auto decorator = MakeShared<DecoratorBlur>();
	if (decorator->Initialise(p_radius->GetNumericValue()))
		return decorator;

	return nullptr;
}

} // namespace Rml
