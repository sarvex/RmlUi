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

#include "DecoratorGradient.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/Geometry.h"
#include "../../Include/RmlUi/Core/GeometryUtilities.h"
#include "../../Include/RmlUi/Core/Math.h"
#include "../../Include/RmlUi/Core/PropertyDefinition.h"
#include "ComputeProperty.h"

/*
Gradient decorator usage in CSS:

decorator: gradient( direction start-color stop-color );

direction: horizontal|vertical;
start-color: #ff00ff;
stop-color: #00ff00;
*/

namespace Rml {

//=======================================================

DecoratorGradient::DecoratorGradient()
{
}

DecoratorGradient::~DecoratorGradient()
{
}

bool DecoratorGradient::Initialise(const Direction dir_, const Colourb start_, const Colourb stop_)
{
	dir = dir_;
	start = start_;
	stop = stop_;
	return true;
}

DecoratorDataHandle DecoratorGradient::GenerateElementData(Element* element) const
{
	Geometry* geometry = new Geometry(element);
	const Box& box = element->GetBox();

	const ComputedValues& computed = element->GetComputedValues();
	const float opacity = computed.opacity();

	const Vector4f border_radius{
		computed.border_top_left_radius(),
		computed.border_top_right_radius(),
		computed.border_bottom_right_radius(),
		computed.border_bottom_left_radius(),
	};
	GeometryUtilities::GenerateBackgroundBorder(geometry, element->GetBox(), Vector2f(0), border_radius, Colourb());

	// Apply opacity
	Colourb colour_start = start;
	colour_start.alpha = (byte)(opacity * (float)colour_start.alpha);
	Colourb colour_stop = stop;
	colour_stop.alpha = (byte)(opacity * (float)colour_stop.alpha);

	const Vector2f padding_offset = box.GetPosition(Box::PADDING);
	const Vector2f padding_size = box.GetSize(Box::PADDING);

	Vector<Vertex>& vertices = geometry->GetVertices();

	if (dir == Direction::Horizontal)
	{
		for (int i = 0; i < (int)vertices.size(); i++)
		{
			const float t = Math::Clamp((vertices[i].position.x - padding_offset.x) / padding_size.x, 0.0f, 1.0f);
			vertices[i].colour = Math::RoundedLerp(t, colour_start, colour_stop);
		}
	}
	else if (dir == Direction::Vertical)
	{
		for (int i = 0; i < (int)vertices.size(); i++)
		{
			const float t = Math::Clamp((vertices[i].position.y - padding_offset.y) / padding_size.y, 0.0f, 1.0f);
			vertices[i].colour = Math::RoundedLerp(t, colour_start, colour_stop);
		}
	}

	return reinterpret_cast<DecoratorDataHandle>(geometry);
}

void DecoratorGradient::ReleaseElementData(DecoratorDataHandle element_data) const
{
	delete reinterpret_cast<Geometry*>(element_data);
}

void DecoratorGradient::RenderElement(Element* element, DecoratorDataHandle element_data) const
{
	auto* data = reinterpret_cast<Geometry*>(element_data);
	data->Render(element->GetAbsoluteOffset(Box::BORDER));
}

//=======================================================

DecoratorGradientInstancer::DecoratorGradientInstancer()
{
	ids.direction = RegisterProperty("direction", "horizontal").AddParser("keyword", "horizontal, vertical").GetId();
	ids.start = RegisterProperty("start-color", "#ffffff").AddParser("color").GetId();
	ids.stop = RegisterProperty("stop-color", "#ffffff").AddParser("color").GetId();
	RegisterShorthand("decorator", "direction, start-color, stop-color", ShorthandType::FallThrough);
}

DecoratorGradientInstancer::~DecoratorGradientInstancer()
{
}

SharedPtr<Decorator> DecoratorGradientInstancer::InstanceDecorator(const String& name, const PropertyDictionary& properties_,
	const DecoratorInstancerInterface& RMLUI_UNUSED_PARAMETER(interface_))
{
	RMLUI_UNUSED(interface_);

	if (name == "gradient")
		gradient_type = GradientType::Straight;
	else if (name == "linear-gradient")
		gradient_type = GradientType::Linear;
	else if (name == "radial-gradient")
		gradient_type = GradientType::Radial;
	else
		return nullptr;

	DecoratorGradient::Direction dir = (DecoratorGradient::Direction)properties_.GetProperty(ids.direction)->Get<int>();
	Colourb start = properties_.GetProperty(ids.start)->Get<Colourb>();
	Colourb stop = properties_.GetProperty(ids.stop)->Get<Colourb>();

	auto decorator = MakeShared<DecoratorGradient>();
	if (decorator->Initialise(dir, start, stop))
	{
		return decorator;
	}

	return nullptr;
}

DecoratorLinearGradient::DecoratorLinearGradient() {}

DecoratorLinearGradient::~DecoratorLinearGradient() {}

bool DecoratorLinearGradient::Initialise(float in_angle, const ColorStopList& in_color_stops)
{
	angle = in_angle;
	color_stops = in_color_stops;
	return !color_stops.empty();
}

DecoratorDataHandle DecoratorLinearGradient::GenerateElementData(Element* element) const
{
	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return INVALID_DECORATORDATAHANDLE;

	CompiledEffectHandle handle =
		render_interface->CompileEffect("linear-gradient", Dictionary{{"angle", Variant(angle)}, {"color_stop_list", Variant(color_stops)}});

	return DecoratorDataHandle(handle);
}

void DecoratorLinearGradient::ReleaseElementData(DecoratorDataHandle handle) const
{
	// TODO: Get the render interface from element
	// RenderInterface* render_interface = element->GetRenderInterface();
	RenderInterface* render_interface = ::Rml::GetRenderInterface();
	if (!render_interface)
		return;
	render_interface->ReleaseCompiledEffect(CompiledEffectHandle(handle));
}

void DecoratorLinearGradient::RenderElement(Element* /*element*/, DecoratorDataHandle /*element_data*/) const
{
	RMLUI_ERROR;
}

void DecoratorLinearGradient::RenderElement(Element* element, DecoratorDataHandle element_data, RenderStage render_stage) const
{
	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return;

	render_interface->RenderEffect(CompiledEffectHandle(element_data), render_stage, 0, element);
}

DecoratorLinearGradientInstancer::DecoratorLinearGradientInstancer()
{
	ids.angle = RegisterProperty("angle", "0deg").AddParser("angle").GetId();
	ids.color_stop_list = RegisterProperty("color-stops", "").AddParser("color_stop_list").GetId();

	RegisterShorthand("decorator", "angle?, color-stops#", ShorthandType::RecursiveCommaSeparated);
}

DecoratorLinearGradientInstancer::~DecoratorLinearGradientInstancer() {}

SharedPtr<Decorator> DecoratorLinearGradientInstancer::InstanceDecorator(const String& /*name*/, const PropertyDictionary& properties_,
	const DecoratorInstancerInterface& /*interface_*/)
{
	const Property* p_angle = properties_.GetProperty(ids.angle);
	if (!p_angle || !(p_angle->unit & Property::ANGLE))
		return nullptr;
	const Property* p_color_stop_list = properties_.GetProperty(ids.color_stop_list);
	if (!p_color_stop_list || p_color_stop_list->unit != Property::COLORSTOPLIST)
		return nullptr;

	const float angle = ComputeAngle(*p_angle);

	const ColorStopList& color_stop_list = p_color_stop_list->value.GetReference<ColorStopList>();

	auto decorator = MakeShared<DecoratorLinearGradient>();
	if (decorator->Initialise(angle, std::move(color_stop_list)))
		return decorator;

	return nullptr;
}

} // namespace Rml
