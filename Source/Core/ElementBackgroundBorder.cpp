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

#include "ElementBackgroundBorder.h"
#include "../../Include/RmlUi/Core/Box.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/GeometryUtilities.h"

namespace Rml {


ElementBackgroundBorder::ElementBackgroundBorder(Element* element) : geometry(element) {}

ElementBackgroundBorder::~ElementBackgroundBorder()
{
	if (shadow_texture)
	{
		if (Rml::RenderInterface* render_interface = geometry.GetRenderInterface())
			render_interface->ReleaseCompiledEffect(shadow_texture);
	}
}

void ElementBackgroundBorder::Render(Element * element)
{
	if (background_dirty || border_dirty)
	{
		GenerateGeometry(element);

		background_dirty = false;
		border_dirty = false;
	}

	if (shadow_texture)
	{
		RenderInterface* render_interface = element->GetRenderInterface();
		if (!render_interface)
		{
			RMLUI_ERROR;
			return;
		}

		render_interface->RenderEffect(shadow_texture, RenderStage::Decoration, 0, element);
	}
	else if (geometry)
	{
		geometry.Render(element->GetAbsoluteOffset(Box::BORDER));
	}
}

void ElementBackgroundBorder::DirtyBackground()
{
	background_dirty = true;
}

void ElementBackgroundBorder::DirtyBorder()
{
	border_dirty = true;
}

void ElementBackgroundBorder::GenerateGeometry(Element* element)
{
	const ComputedValues& computed = element->GetComputedValues();

	Colourb background_color = computed.background_color();
	Colourb border_colors[4] = {
		computed.border_top_color(),
		computed.border_right_color(),
		computed.border_bottom_color(),
		computed.border_left_color(),
	};

	// Apply opacity
	const float opacity = computed.opacity();
	background_color.alpha = (byte)(opacity * (float)background_color.alpha);

	if (opacity < 1)
	{
		for (int i = 0; i < 4; ++i)
			border_colors[i].alpha = (byte)(opacity * (float)border_colors[i].alpha);
	}

	geometry.GetVertices().clear();
	geometry.GetIndices().clear();

	const Vector4f radii(computed.border_top_left_radius(), computed.border_top_right_radius(), computed.border_bottom_right_radius(),
		computed.border_bottom_left_radius());

	for (int i = 0; i < element->GetNumBoxes(); i++)
	{
		Vector2f offset;
		const Box& box = element->GetBox(i, offset);
		GeometryUtilities::GenerateBackgroundBorder(&geometry, box, offset, radii, background_color, border_colors);
	}

	geometry.Release();

	if (shadow_texture)
	{
		if (RenderInterface* render_interface = element->GetRenderInterface())
			render_interface->ReleaseCompiledEffect(shadow_texture);
	}

	if (const Property* p_box_shadow = element->GetLocalProperty(PropertyId::BoxShadow))
	{
		RMLUI_ASSERT(p_box_shadow->value.GetType() == Variant::SHADOWLIST);
		const ShadowList& shadow_list = p_box_shadow->value.GetReference<ShadowList>();

		RenderInterface* render_interface = element->GetRenderInterface();
		if (!render_interface)
		{
			RMLUI_ERROR;
			return;
		}

		const Vector2f element_offset = element->GetAbsoluteOffset(Box::BORDER);

		static const Colourb opaque_colors[4];
		static const Colourb transparent_color(0, 0, 0, 0);

		Geometry geometry_border, geometry_padding;

		for (int i = 0; i < element->GetNumBoxes(); i++)
		{
			Vector2f offset;
			const Box& box = element->GetBox(i, offset);
			GeometryUtilities::GenerateBackgroundBorder(&geometry_padding, box, offset, radii, opaque_colors[0], nullptr);
			GeometryUtilities::GenerateBackgroundBorder(&geometry_border, box, offset, radii, transparent_color, opaque_colors);
		}

		render_interface->EnableScissorRegion(false);

		shadow_texture = render_interface->CompileEffect("render-to-texture", Dictionary{});
		render_interface->RenderEffect(shadow_texture, RenderStage::Enter, 0, element);

		constexpr int mask_padding = 0b001;
		constexpr int mask_border = 0b010;
		constexpr int mask_inset = 0b100;

		// TODO: Make the render texture position-independent.
		render_interface->StencilCommand(StencilCommand::Clear, 0);
		render_interface->StencilCommand(StencilCommand::Write, mask_padding);
		geometry_padding.Render(element_offset);
		render_interface->StencilCommand(StencilCommand::Write, mask_border);
		geometry_border.Render(element_offset);
		render_interface->StencilCommand(StencilCommand::WriteDisable);

		geometry.Render(element_offset);

		for (int shadow_index = (int)shadow_list.size() - 1; shadow_index >= 0; shadow_index--)
		{
			const Shadow& shadow = shadow_list[shadow_index];

			const bool inset = shadow.inset;
			const Colourb shadow_colors[4] = {
				shadow.color,
				shadow.color,
				shadow.color,
				shadow.color
			};
			
			Vector4f spread_radii = radii;
			for (int i = 0; i < 4; i++)
			{
				float& radius = spread_radii[i];
				float spread_factor = (inset ? -1.f : 1.f);
				if (radius < shadow.spread_distance)
				{
					const float ratio_minus_one = (radius / shadow.spread_distance) - 1.f;
					spread_factor *= 1.f + ratio_minus_one * ratio_minus_one * ratio_minus_one;
				}
				radius = Math::Max(radius + spread_factor * shadow.spread_distance, 0.f);
			}

			Geometry shadow_geometry;

			// Generate the shadow box
			for (int i = 0; i < element->GetNumBoxes(); i++)
			{
				Vector2f offset;
				Box box = element->GetBox(i, offset);
				const float signed_spread_distance = (inset ? -shadow.spread_distance : shadow.spread_distance);
				offset -= Vector2f(signed_spread_distance);

				for (int j = 0; j < (int)Box::NUM_EDGES; j++)
				{
					Box::Edge edge = (Box::Edge)j;
					const float new_size = box.GetEdge(Box::PADDING, edge) + signed_spread_distance;
					box.SetEdge(Box::PADDING, edge, new_size);
				}

				GeometryUtilities::GenerateBackgroundBorder(&shadow_geometry, box, offset, spread_radii, shadow.color, inset ? nullptr : shadow_colors);
			}

			CompiledEffectHandle rtt = render_interface->CompileEffect("render-to-texture", Dictionary{});
			CompiledEffectHandle blur = render_interface->CompileEffect("blur", Dictionary{{"radius", Variant(shadow.blur_radius)}});
			CompiledEffectHandle fullscreen_color = render_interface->CompileEffect("color", Dictionary{{"color", Variant(shadow.color)}});

			render_interface->RenderEffect(rtt, RenderStage::Enter, 0, element);

			if (inset)
			{
				render_interface->StencilCommand(StencilCommand::Write, mask_inset, mask_inset);
				shadow_geometry.Render(shadow.offset + element_offset);
				render_interface->StencilCommand(StencilCommand::WriteDisable);

				render_interface->StencilCommand(StencilCommand::TestEqual, 0, mask_inset);
				render_interface->RenderEffect(fullscreen_color, RenderStage::Decoration, 0, element);

				render_interface->StencilCommand(StencilCommand::Clear, 0, mask_inset);

				render_interface->StencilCommand(StencilCommand::TestEqual, mask_padding, mask_padding);
				render_interface->RenderEffect(blur, RenderStage::Decoration, 0, element);

				render_interface->RenderEffect(rtt, RenderStage::Exit, 0, element);
				render_interface->RenderEffect(rtt, RenderStage::Decoration, 0, element);
			}
			else
			{
				shadow_geometry.Render(shadow.offset + element_offset);

				render_interface->StencilCommand(StencilCommand::TestEqual, 0);
				render_interface->RenderEffect(blur, RenderStage::Decoration, 0, element);
				render_interface->RenderEffect(rtt, RenderStage::Exit, 0, element);

				render_interface->StencilCommand(StencilCommand::TestEqual, 0);
				render_interface->RenderEffect(rtt, RenderStage::Decoration, 0, element);
			}

			render_interface->StencilCommand(StencilCommand::TestDisable, 0);

			render_interface->ReleaseCompiledEffect(fullscreen_color);
			render_interface->ReleaseCompiledEffect(blur);
			render_interface->ReleaseCompiledEffect(rtt);
		}

		render_interface->RenderEffect(shadow_texture, RenderStage::Exit, 0, element);

		ElementUtilities::SetClippingRegion(element);
	}
}

} // namespace Rml
