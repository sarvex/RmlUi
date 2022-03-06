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

#include "DecoratorDropShadow.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/PropertyDefinition.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "ComputeProperty.h"
#include "DecoratorBasicFilter.h"

namespace Rml {

DecoratorDropShadow::DecoratorDropShadow() {}

DecoratorDropShadow::~DecoratorDropShadow() {}

bool DecoratorDropShadow::Initialise(Colourb in_color, Vector2f in_offset, float in_sigma)
{
	color = in_color;
	offset = in_offset;
	sigma = in_sigma;

	// Position and expand the clipping region to cover both the native element *and* its offset shadow w/blur.
	const float blur_radius = 2.f * sigma;
	expand_top_left = Math::Max(-offset, Vector2f(0.f)) + Vector2f(blur_radius);
	expand_bottom_right = Math::Max(offset, Vector2f(0.f)) + Vector2f(blur_radius);

	return sigma >= 0.f;
}

DecoratorDataHandle DecoratorDropShadow::GenerateElementData(Element* element) const
{
	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return INVALID_DECORATORDATAHANDLE;

	CompiledEffectHandle handle =
		render_interface->CompileEffect("drop-shadow", Dictionary{{"color", Variant(color)}, {"offset", Variant(offset)}, {"sigma", Variant(sigma)}});

	BasicEffectElementData* element_data = GetBasicEffectElementDataPool().AllocateAndConstruct(render_interface, handle);
	return reinterpret_cast<DecoratorDataHandle>(element_data);
}

void DecoratorDropShadow::ReleaseElementData(DecoratorDataHandle handle) const
{
	BasicEffectElementData* element_data = reinterpret_cast<BasicEffectElementData*>(handle);
	RMLUI_ASSERT(element_data && element_data->render_interface);

	element_data->render_interface->ReleaseCompiledEffect(element_data->effect);
	GetBasicEffectElementDataPool().DestroyAndDeallocate(element_data);
}

void DecoratorDropShadow::RenderElement(Element* /*element*/, DecoratorDataHandle handle) const
{
	BasicEffectElementData* element_data = reinterpret_cast<BasicEffectElementData*>(handle);
	element_data->render_interface->RenderEffect(element_data->effect);
}

void DecoratorDropShadow::GetClipExtension(Vector2f& top_left, Vector2f& bottom_right) const
{
	top_left = expand_top_left;
	bottom_right = expand_bottom_right;
}

DecoratorDropShadowInstancer::DecoratorDropShadowInstancer() : DecoratorInstancer(DecoratorClasses::Filter | DecoratorClasses::BackdropFilter)
{
	ids.color = RegisterProperty("color", "black").AddParser("color").GetId();
	ids.offset_x = RegisterProperty("offset-x", "0px").AddParser("length").GetId();
	ids.offset_y = RegisterProperty("offset-y", "0px").AddParser("length").GetId();
	ids.sigma = RegisterProperty("sigma", "0px").AddParser("length").GetId();
	RegisterShorthand("decorator", "color, offset-x, offset-y, sigma", ShorthandType::FallThrough);
}

DecoratorDropShadowInstancer::~DecoratorDropShadowInstancer() {}

SharedPtr<Decorator> DecoratorDropShadowInstancer::InstanceDecorator(const String& /*name*/, const PropertyDictionary& properties_,
	const DecoratorInstancerInterface& /*interface_*/)
{
	const Property* p_color = properties_.GetProperty(ids.color);
	const Property* p_offset_x = properties_.GetProperty(ids.offset_x);
	const Property* p_offset_y = properties_.GetProperty(ids.offset_y);
	const Property* p_sigma = properties_.GetProperty(ids.sigma);
	if (!p_color || !p_offset_x || !p_offset_y || !p_sigma)
		return nullptr;

	// TODO dp/vp
	const Colourb color = p_color->Get<Colourb>();
	const float offset_x = ComputeAbsoluteLength(*p_offset_x, 1.f, Vector2f(0.f));
	const float offset_y = ComputeAbsoluteLength(*p_offset_y, 1.f, Vector2f(0.f));
	const float sigma = ComputeAbsoluteLength(*p_sigma, 1.f, Vector2f(0.f));

	auto decorator = MakeShared<DecoratorDropShadow>();
	if (decorator->Initialise(color, Vector2f(offset_x, offset_y), sigma))
		return decorator;

	return nullptr;
}
} // namespace Rml
