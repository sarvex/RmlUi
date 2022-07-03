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

ElementBackgroundBorder::ElementBackgroundBorder() {}

ElementBackgroundBorder::~ElementBackgroundBorder() {}

void ElementBackgroundBorder::Render(Element* element)
{
	if (background_dirty || border_dirty)
	{
		for (size_t i = 0; i < size_t(BackgroundType::Count); i++)
		{
			if (geometries[i])
				geometries[i]->geometry.Release(true);
		}

		GenerateGeometry(element);

		background_dirty = false;
		border_dirty = false;
	}

	Geometry* shadow_geometry = GetGeometry(BackgroundType::BoxShadow);
	if (shadow_geometry && *shadow_geometry)
		shadow_geometry->Render(element->GetAbsoluteOffset(Box::BORDER));
	else if (Geometry* main_geometry = GetGeometry(BackgroundType::Main))
		main_geometry->Render(element->GetAbsoluteOffset(Box::BORDER));
}

void ElementBackgroundBorder::DirtyBackground()
{
	background_dirty = true;
}

void ElementBackgroundBorder::DirtyBorder()
{
	border_dirty = true;
}

Geometry* ElementBackgroundBorder::GetClipGeometry(Element* element, Box::Area clip_area)
{
	BackgroundType type = {};
	switch (clip_area)
	{
	case Rml::Box::BORDER:
		type = BackgroundType::ClipBorder;
		break;
	case Rml::Box::PADDING:
		type = BackgroundType::ClipPadding;
		break;
	case Rml::Box::CONTENT:
		type = BackgroundType::ClipContent;
		break;
	default:
		RMLUI_ERROR;
		return nullptr;
	}

	Geometry& geometry = GetOrCreateBackground(element, type).geometry;
	if (!geometry)
	{
		const Box& box = element->GetBox();
		const Vector4f border_radius = element->GetComputedValues().border_radius();
		GeometryUtilities::GenerateBackground(&geometry, box, {}, border_radius, Colourb(255), clip_area);
	}

	return &geometry;
}

void ElementBackgroundBorder::GenerateGeometry(Element* element)
{
	const ComputedValues& computed = element->GetComputedValues();
	const Property* p_box_shadow = element->GetLocalProperty(PropertyId::BoxShadow);

	const Vector4f border_radius = computed.border_radius();
	Colourb background_color = computed.background_color();
	Colourb border_colors[4] = {
		computed.border_top_color(),
		computed.border_right_color(),
		computed.border_bottom_color(),
		computed.border_left_color(),
	};

	if (!p_box_shadow)
	{
		// Apply opacity except if we have a box shadow. In the latter case the background is rendered opaquely into the box-shadow texture, while
		// opacity is applied to the entire box-shadow texture when that is rendered.
		const float opacity = computed.opacity();
		if (opacity < 1.f)
		{
			background_color.alpha = (byte)(opacity * (float)background_color.alpha);

			for (int i = 0; i < 4; ++i)
				border_colors[i].alpha = (byte)(opacity * (float)border_colors[i].alpha);
		}
	}

	Geometry& main_geometry = GetOrCreateBackground(element, BackgroundType::Main).geometry;

	for (int i = 0; i < element->GetNumBoxes(); i++)
	{
		Vector2f offset;
		const Box& box = element->GetBox(i, offset);
		GeometryUtilities::GenerateBackgroundBorder(&main_geometry, box, offset, border_radius, background_color, border_colors);
	}

	if (p_box_shadow)
	{
		RMLUI_ASSERT(p_box_shadow->value.GetType() == Variant::SHADOWLIST);
		const ShadowList& shadow_list = p_box_shadow->value.GetReference<ShadowList>();

		GenerateBoxShadow(element, shadow_list, border_radius, computed.opacity());
	}
}

void ElementBackgroundBorder::GenerateBoxShadow(Element* element, const ShadowList& shadow_list, const Vector4f border_radius, const float opacity)
{
	// Find the box-shadow texture dimension and offset required to cover all box-shadows and element boxes combined.
	Vector2f element_offset_in_texture;
	Vector2i texture_dimensions;

	{
		Vector2f extend_min;
		Vector2f extend_max;

		// Extend the render-texture to encompass box-shadow blur and spread.
		for (const Shadow& shadow : shadow_list)
		{
			if (!shadow.inset)
			{
				const float extend = shadow.blur_radius + shadow.spread_distance;
				extend_min = Math::Min(extend_min, shadow.offset - Vector2f(extend));
				extend_max = Math::Max(extend_max, shadow.offset + Vector2f(extend));
			}
		}

		Vector2f boxes_min;
		Vector2f boxes_max;

		// Extend the render-texture further to cover all the element's boxes.
		for (int i = 0; i < element->GetNumBoxes(); i++)
		{
			Vector2f offset;
			const Box& box = element->GetBox(i, offset);
			boxes_min = Math::Min(boxes_min, offset);
			boxes_max = Math::Max(boxes_max, offset + box.GetSize(Box::BORDER));
		}

		auto RoundUp = [](Vector2f v) { return Vector2f(Math::RoundUpFloat(v.x), Math::RoundUpFloat(v.y)); };

		element_offset_in_texture = -RoundUp(extend_min + boxes_min);
		texture_dimensions = Vector2i(RoundUp(element_offset_in_texture + extend_max + boxes_max));
	}

	Geometry& main_geometry = *GetGeometry(BackgroundType::Main);

	// Callback for generating the box-shadow texture. Using a callback ensures that the texture can be regenerated at any time, for example if the
	// device loses its GPU context and the client calls Rml::ReleaseTextures().
	auto p_callback = [&main_geometry, element, border_radius, texture_dimensions, element_offset_in_texture, shadow_list](
						  RenderInterface* render_interface, const String& /*name*/, TextureHandle& out_handle, Vector2i& out_dimensions) -> bool {
		Context* context = element->GetContext();
		if (!context)
		{
			RMLUI_ERROR;
			return false;
		}
		RMLUI_ASSERT(context->GetRenderInterface() == render_interface);

		Geometry geometry_padding(render_interface);        // Render geometry for inner box-shadow.
		Geometry geometry_padding_border(render_interface); // Clipping mask for outer box-shadow.

		bool has_inner_shadow = false;
		bool has_outer_shadow = false;
		for (const Shadow& shadow : shadow_list)
		{
			if (shadow.inset)
				has_inner_shadow = true;
			else
				has_outer_shadow = true;
		}

		// Generate the geometry for all the element's boxes and extend the render-texture further to cover all of them.
		for (int i = 0; i < element->GetNumBoxes(); i++)
		{
			Vector2f offset;
			const Box& box = element->GetBox(i, offset);

			if (has_inner_shadow)
				GeometryUtilities::GenerateBackground(&geometry_padding, box, offset, border_radius, Colourb(255), Box::PADDING);
			if (has_outer_shadow)
				GeometryUtilities::GenerateBackground(&geometry_padding_border, box, offset, border_radius, Colourb(255), Box::BORDER);
		}

		RenderState& render_state = context->GetRenderState();
		RenderStateSession render_state_session(render_state);
		render_state.Reset();
		render_state.EnableScissorRegion(Vector2i(0), texture_dimensions);

		render_interface->StackPush();

		main_geometry.Render(element_offset_in_texture);

		for (int shadow_index = (int)shadow_list.size() - 1; shadow_index >= 0; shadow_index--)
		{
			const Shadow& shadow = shadow_list[shadow_index];
			const bool inset = shadow.inset;

			Vector4f spread_radii = border_radius;
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

			// Generate the shadow geometry. For outer box-shadows it is rendered normally, while for inner box-shadows it is used as a clipping mask.
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

				GeometryUtilities::GenerateBackground(&shadow_geometry, box, offset, spread_radii, shadow.color, inset ? Box::PADDING : Box::BORDER);
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

			if (inset)
			{
				render_state.SetClipMask(ClipMask::ClipOut, &shadow_geometry, shadow.offset + element_offset_in_texture);

				for (Rml::Vertex& vertex : geometry_padding.GetVertices())
					vertex.colour = shadow.color;

				geometry_padding.Release();
				geometry_padding.Render(element_offset_in_texture);

				render_state.SetClipMask(ClipMask::Clip, &geometry_padding, element_offset_in_texture);
			}
			else
			{
				render_state.SetClipMask(ClipMask::ClipOut, &geometry_padding_border, element_offset_in_texture);
				shadow_geometry.Render(shadow.offset + element_offset_in_texture);
			}

			if (blur)
			{
				render_interface->StackApply(BlitDestination::BlendStackBelow, {}, texture_dimensions);
				render_interface->StackPop();
				render_interface->ReleaseCompiledFilter(blur);
			}
		}

		render_state.DisableScissorRegion();
		render_state.DisableClipMask();

		TextureHandle shadow_texture = render_interface->RenderToTexture({}, texture_dimensions);
		render_interface->StackPop();

		out_dimensions = texture_dimensions;
		out_handle = shadow_texture;

		return true;
	};

	// Generate the geometry for the box-shadow texture.
	Background& shadow_background = GetOrCreateBackground(element, BackgroundType::BoxShadow);
	Geometry& shadow_geometry = shadow_background.geometry;
	Texture& shadow_texture = shadow_background.texture;

	Vector<Vertex>& vertices = shadow_geometry.GetVertices();
	Vector<int>& indices = shadow_geometry.GetIndices();
	vertices.resize(4);
	indices.resize(6);
	const byte alpha = byte(opacity * 255.f);
	GeometryUtilities::GenerateQuad(vertices.data(), indices.data(), -element_offset_in_texture, Vector2f(texture_dimensions), Colourb(255, alpha));

	shadow_texture.Set("box-shadow", p_callback);
	shadow_geometry.SetTexture(&shadow_texture);
}

Geometry* ElementBackgroundBorder::GetGeometry(BackgroundType type)
{
	if (Background* geometry = geometries[size_t(type)].get())
		return &geometry->geometry;
	return nullptr;
}

ElementBackgroundBorder::Background& ElementBackgroundBorder::GetOrCreateBackground(Element* element, BackgroundType type)
{
	UniquePtr<Background>& geometry = geometries[size_t(type)];
	if (!geometry)
		geometry = MakeUnique<Background>(element);
	return *geometry;
}

} // namespace Rml
