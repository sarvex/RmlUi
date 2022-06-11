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
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/GeometryUtilities.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"

namespace Rml {

ElementBackgroundBorder::ElementBackgroundBorder(Element* element) : geometry(element) {}

ElementBackgroundBorder::~ElementBackgroundBorder()
{
	if (shadow_texture || shadow_geometry)
	{
		if (Rml::RenderInterface* render_interface = geometry.GetRenderInterface())
		{
			render_interface->ReleaseCompiledGeometry(shadow_geometry);
			render_interface->ReleaseTexture(shadow_texture);
		}
	}
}

void ElementBackgroundBorder::Render(Element* element)
{
	if (background_dirty || border_dirty)
	{
		GenerateGeometry(element);

		background_dirty = false;
		border_dirty = false;
	}

	if (shadow_geometry)
	{
		Context* context = element->GetContext();
		RenderInterface* render_interface = context ? context->GetRenderInterface() : nullptr;
		if (!context || !render_interface)
		{
			RMLUI_ERROR;
			return;
		}

		render_interface->RenderCompiledGeometry(shadow_geometry, element->GetAbsoluteOffset(Box::BORDER));
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

	if (shadow_texture || shadow_geometry)
	{
		if (RenderInterface* render_interface = element->GetRenderInterface())
		{
			render_interface->ReleaseCompiledGeometry(shadow_geometry);
			render_interface->ReleaseTexture(shadow_texture);
		}

		shadow_texture = 0;
		shadow_geometry = 0;
	}

	if (const Property* p_box_shadow = element->GetLocalProperty(PropertyId::BoxShadow))
	{
		RMLUI_ASSERT(p_box_shadow->value.GetType() == Variant::SHADOWLIST);
		const ShadowList& shadow_list = p_box_shadow->value.GetReference<ShadowList>();

		Context* context = element->GetContext();
		RenderInterface* render_interface = context ? context->GetRenderInterface() : nullptr;

		if (!render_interface || !context)
		{
			RMLUI_ERROR;
			return;
		}

		Geometry geometry_padding(element);
		Geometry geometry_padding_border(element); // TODO
		Vector2f element_offset_in_texture;
		Vector2i texture_dimensions;

		{
			Vector2f extend_top_left;
			Vector2f extend_bottom_right;

			for (const Shadow& shadow : shadow_list)
			{
				if (!shadow.inset)
				{
					const float extend = shadow.blur_radius + shadow.spread_distance;
					extend_top_left = Math::Max(extend_top_left, Vector2f(extend) - shadow.offset);
					extend_bottom_right = Math::Max(extend_bottom_right, Vector2f(extend) + shadow.offset);
				}
			}

			Vector2f offset_min;
			Vector2f offset_max;

			// Generate border and padding geometry, and extend the texture to encompass any additional boxes.
			for (int i = 0; i < element->GetNumBoxes(); i++)
			{
				static const Colourb opaque_colors[4];
				static const Colourb transparent_color(0, 0, 0, 0);
				Vector2f offset;
				const Box& box = element->GetBox(i, offset);
				GeometryUtilities::GenerateBackgroundBorder(&geometry_padding, box, offset, radii, opaque_colors[0], nullptr);
				GeometryUtilities::GenerateBackgroundBorder(&geometry_padding_border, box, offset, radii, opaque_colors[0], opaque_colors);
				offset_min = Math::Min(offset_min, offset);
				offset_max = Math::Max(offset_max, offset);
			}

			auto RoundUp = [](Vector2f v) { return Vector2f(Math::RoundUpFloat(v.x), Math::RoundUpFloat(v.y)); };

			element_offset_in_texture = RoundUp(extend_top_left - offset_min);
			texture_dimensions =
				Vector2i(RoundUp(element_offset_in_texture + element->GetBox().GetSize(Box::BORDER) + extend_bottom_right + offset_max));
		}

		RenderState& render_state = context->GetRenderState();
		const Matrix4f* active_element_transform = render_state.transform_pointer;
		ElementUtilities::ApplyTransform(render_interface, render_state, nullptr);
		render_interface->EnableClipMask(false);
		render_interface->EnableScissorRegion(true);
		render_interface->SetScissorRegion(0, 0, (int)texture_dimensions.x, (int)texture_dimensions.y);
		render_interface->StackPush();

		// TODO Make a RAII wrapper to easily push and pop renderer states. Clear all states on push.

		geometry.Render(element_offset_in_texture);

		for (int shadow_index = (int)shadow_list.size() - 1; shadow_index >= 0; shadow_index--)
		{
			const Shadow& shadow = shadow_list[shadow_index];

			const bool inset = shadow.inset;
			const Colourb shadow_colors[4] = {shadow.color, shadow.color, shadow.color, shadow.color};

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

				GeometryUtilities::GenerateBackgroundBorder(&shadow_geometry, box, offset, spread_radii, shadow.color,
					inset ? nullptr : shadow_colors);
			}

			CompiledFilterHandle blur = {};
			if (shadow.blur_radius > 0.5f)
			{
				blur = render_interface->CompileFilter("blur", Dictionary{{"radius", Variant(shadow.blur_radius)}});
				if (blur)
				{
					render_interface->StackPush();
					render_interface->AttachFilter(blur);
				}
			}

			render_interface->EnableScissorRegion(false);

			if (inset)
			{
				render_interface->SetClipMask(ClipMask::ClipOut, shadow_geometry.GetCompiledHandle(), shadow.offset + element_offset_in_texture);

				// TODO: Maybe it would be a lot better to add a color render effect and just use white here.
				for (Rml::Vertex& vertex : geometry_padding.GetVertices())
					vertex.colour = shadow.color;

				geometry_padding.Release();
				geometry_padding.Render(element_offset_in_texture);

				render_interface->SetClipMask(ClipMask::Clip, geometry_padding.GetCompiledHandle(), element_offset_in_texture);
			}
			else
			{
				render_interface->SetClipMask(ClipMask::ClipOut, geometry_padding_border.GetCompiledHandle(), element_offset_in_texture);
				shadow_geometry.Render(shadow.offset + element_offset_in_texture);
			}

			if (blur)
			{
				render_interface->StackApply(BlitDestination::StackBelow, {}, texture_dimensions);
				render_interface->StackPop();
				render_interface->ReleaseCompiledFilter(blur);
			}
		}

		render_interface->EnableClipMask(false);
		render_interface->EnableScissorRegion(false);

		shadow_texture = render_interface->RenderToTexture({}, texture_dimensions);
		render_interface->StackPop();

		Colourb color_white = Colourb(255, 255, 255, 255);
		Vertex vertices[4];
		int indices[6];
		GeometryUtilities::GenerateQuad(vertices, indices, -element_offset_in_texture, Vector2f(texture_dimensions), color_white);
		shadow_geometry = render_interface->CompileGeometry(vertices, 4, indices, 6, shadow_texture);

		ElementUtilities::ApplyTransform(render_interface, render_state, active_element_transform);
		ElementUtilities::ApplyActiveClipRegion(render_interface, render_state);
	}
}

} // namespace Rml
