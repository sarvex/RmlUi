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

#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/Core.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementScroll.h"
#include "../../Include/RmlUi/Core/Factory.h"
#include "../../Include/RmlUi/Core/FontEngineInterface.h"
#include "../../Include/RmlUi/Core/GeometryUtilities.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "DataController.h"
#include "DataModel.h"
#include "DataView.h"
#include "ElementStyle.h"
#include "LayoutDetails.h"
#include "LayoutEngine.h"
#include "TransformState.h"
#include <limits>

namespace Rml {

// Builds and sets the box for an element.
static void SetBox(Element* element)
{
	Element* parent = element->GetParentNode();
	RMLUI_ASSERT(parent != nullptr);

	Vector2f containing_block = parent->GetBox().GetSize();
	containing_block.x -= parent->GetElementScroll()->GetScrollbarSize(ElementScroll::VERTICAL);
	containing_block.y -= parent->GetElementScroll()->GetScrollbarSize(ElementScroll::HORIZONTAL);

	Box box;
	LayoutDetails::BuildBox(box, containing_block, element);

	if (element->GetComputedValues().height().type != Style::Height::Auto)
		box.SetContent(Vector2f(box.GetSize().x, containing_block.y));

	element->SetBox(box);
}

// Positions an element relative to an offset parent.
static void SetElementOffset(Element* element, Vector2f offset)
{
	Vector2f relative_offset = element->GetParentNode()->GetBox().GetPosition(Box::CONTENT);
	relative_offset += offset;
	relative_offset.x += element->GetBox().GetEdge(Box::MARGIN, Box::LEFT);
	relative_offset.y += element->GetBox().GetEdge(Box::MARGIN, Box::TOP);

	element->SetOffset(relative_offset, element->GetParentNode());
}

Element* ElementUtilities::GetElementById(Element* root_element, const String& id)
{
	// Breadth first search on elements for the corresponding id
	typedef Queue<Element*> SearchQueue;
	SearchQueue search_queue;
	search_queue.push(root_element);

	while (!search_queue.empty())
	{
		Element* element = search_queue.front();
		search_queue.pop();
		
		if (element->GetId() == id)
		{
			return element;
		}
		
		// Add all children to search
		for (int i = 0; i < element->GetNumChildren(); i++)
			search_queue.push(element->GetChild(i));
	}

	return nullptr;
}

void ElementUtilities::GetElementsByTagName(ElementList& elements, Element* root_element, const String& tag)
{
	// Breadth first search on elements for the corresponding id
	typedef Queue< Element* > SearchQueue;
	SearchQueue search_queue;
	for (int i = 0; i < root_element->GetNumChildren(); ++i)
		search_queue.push(root_element->GetChild(i));

	while (!search_queue.empty())
	{
		Element* element = search_queue.front();
		search_queue.pop();

		if (element->GetTagName() == tag)
			elements.push_back(element);

		// Add all children to search.
		for (int i = 0; i < element->GetNumChildren(); i++)
			search_queue.push(element->GetChild(i));
	}
}

void ElementUtilities::GetElementsByClassName(ElementList& elements, Element* root_element, const String& class_name)
{
	// Breadth first search on elements for the corresponding id
	typedef Queue< Element* > SearchQueue;
	SearchQueue search_queue;
	for (int i = 0; i < root_element->GetNumChildren(); ++i)
		search_queue.push(root_element->GetChild(i));

	while (!search_queue.empty())
	{
		Element* element = search_queue.front();
		search_queue.pop();

		if (element->IsClassSet(class_name))
			elements.push_back(element);

		// Add all children to search.
		for (int i = 0; i < element->GetNumChildren(); i++)
			search_queue.push(element->GetChild(i));
	}
}

float ElementUtilities::GetDensityIndependentPixelRatio(Element * element)
{
	Context* context = element->GetContext();
	if (context == nullptr)
		return 1.0f;

	return context->GetDensityIndependentPixelRatio();
}

// Returns the width of a string rendered within the context of the given element.
int ElementUtilities::GetStringWidth(Element* element, const String& string, Character prior_character)
{
	FontFaceHandle font_face_handle = element->GetFontFaceHandle();
	if (font_face_handle == 0)
		return 0;

	return GetFontEngineInterface()->GetStringWidth(font_face_handle, string, prior_character);
}

// Generates the clipping region for an element.
bool ElementUtilities::GetClippingRegion(Vector2i& clip_origin, Vector2i& clip_dimensions, Element* element, ElementClipList* stencil_elements,
	bool force_clip_self)
{
	using Style::Clip;
	clip_origin = Vector2i(-1, -1);
	clip_dimensions = Vector2i(-1, -1);

	Clip target_element_clip = element->GetComputedValues().clip();
	if (target_element_clip == Clip::Type::None && !force_clip_self)
		return false;

	int num_ignored_clips = target_element_clip.GetNumber();

	// Search through the element's ancestors, finding all elements that clip their overflow and have overflow to clip.
	// For each that we find, we combine their clipping region with the existing clipping region, and so build up a
	// complete clipping region for the element.
	Element* clipping_element = (force_clip_self ? element : element->GetParentNode());

	bool clip_region_set = false;
	Vector2f clip_top_left;
	Vector2f clip_bottom_right;

	while (clipping_element != nullptr)
	{
		const bool force_clipping_current_element = (force_clip_self && clipping_element == element);
		const ComputedValues& clip_computed = clipping_element->GetComputedValues();
		const bool clip_enabled = (clip_computed.overflow_x() != Style::Overflow::Visible || clip_computed.overflow_y() != Style::Overflow::Visible);
		const bool clip_always = (clip_computed.clip() == Clip::Type::Always);
		const bool clip_none = (clip_computed.clip() == Clip::Type::None);
		const int clip_number = clip_computed.clip().GetNumber();

		// Merge the existing clip region with the current clip region if we aren't ignoring clip regions.
		if (((clip_always || clip_enabled) && num_ignored_clips == 0) || force_clipping_current_element)
		{
			const Box::Area client_area = (force_clipping_current_element ? Box::BORDER : clipping_element->GetClientArea());
			const bool has_clipping_content =
				(clip_always || force_clipping_current_element || clipping_element->GetClientWidth() < clipping_element->GetScrollWidth() - 0.5f ||
					clipping_element->GetClientHeight() < clipping_element->GetScrollHeight() - 0.5f);
			bool disable_scissor_clipping = false;

			if (stencil_elements)
			{
				const TransformState* transform_state = clipping_element->GetTransformState();
				const bool has_transform = (transform_state && transform_state->GetTransform());
				const bool has_border_radius = (clip_computed.border_top_left_radius() > 0.f || clip_computed.border_top_right_radius() > 0.f ||
					clip_computed.border_bottom_right_radius() > 0.f || clip_computed.border_bottom_left_radius() > 0.f);

				// If the element has border-radius we always use stencil clipping, since we can't easily predict if content is located on the
				// curved region to be clipped. If the element has a transform we only clip when the content clips.
				if (has_border_radius || (has_transform && has_clipping_content))
					stencil_elements->push_back(ElementClip{clipping_element, client_area});

				// If we only have border-radius then we add this element to the scissor region as well as the stencil buffer. This may help with eg.
				// culling text render calls. However, when we have a transform, the element cannot be added to the scissor region since its geometry
				// may be projected entirely elsewhere.
				disable_scissor_clipping = has_transform;
			}

			if (has_clipping_content && !disable_scissor_clipping)
			{
				// Shrink the scissor region to the element's client area.
				Vector2f element_top_left = clipping_element->GetAbsoluteOffset(client_area);
				Vector2f element_bottom_right = element_top_left + clipping_element->GetBox().GetSize(client_area);

				if (!clip_region_set)
				{
					clip_top_left = element_top_left;
					clip_bottom_right = element_bottom_right;
					clip_region_set = true;
				}
				else
				{
					clip_top_left = Math::Max(clip_top_left, element_top_left);
					clip_bottom_right = Math::Min(clip_bottom_right, element_bottom_right);
				}
			}
		}

		if (!force_clipping_current_element)
		{
			// If this region is meant to clip and we're skipping regions, update the counter.
			if (num_ignored_clips > 0 && clip_enabled)
				num_ignored_clips--;

			// Inherit how many clip regions this ancestor ignores.
			num_ignored_clips = Math::Max(num_ignored_clips, clip_number);

			// If this region ignores all clipping regions, then we do too.
			if (clip_none)
				break;
		}

		// Climb the tree to this region's parent.
		clipping_element = clipping_element->GetParentNode();
	}

	if (clip_region_set)
	{
		clip_origin = Vector2i(clip_top_left.Round());
		clip_dimensions = Math::Max(Vector2i(0), Vector2i(clip_bottom_right.Round()) - clip_origin);
	}

	return clip_region_set;
}

// Sets the clipping region from an element and its ancestors.
bool ElementUtilities::SetClippingRegion(Element* element, bool force_clip_self)
{
	RMLUI_ASSERT(element);
	Context* context = element->GetContext();
	RenderInterface* render_interface = context ? context->GetRenderInterface() : nullptr;

	if (!render_interface || !context)
		return false;

	RenderState& render_state = context->GetRenderState();

	Vector2i clip_origin = {-1, -1};
	Vector2i clip_dimensions = {-1, -1};
	ElementClipList stencil_elements;
	ElementClipList* stencil_elements_ptr = (render_state.supports_stencil ? &stencil_elements : nullptr);

	GetClippingRegion(clip_origin, clip_dimensions, element, stencil_elements_ptr, force_clip_self);

	Vector2i& active_origin = render_state.clip_origin;
	Vector2i& active_dimensions = render_state.clip_dimensions;
	ElementClipList& active_stencil_elements = render_state.clip_stencil_elements;

	if (clip_origin != active_origin || clip_dimensions != active_dimensions || stencil_elements != active_stencil_elements)
	{
		active_origin = clip_origin;
		active_dimensions = clip_dimensions;
		active_stencil_elements = std::move(stencil_elements);
		ElementUtilities::ApplyActiveClipRegion(render_interface, render_state);
	}

	return true;
}

void ElementUtilities::DisableClippingRegion(Context* context)
{
	RMLUI_ASSERT(context);
	RenderInterface* render_interface = context->GetRenderInterface();

	RenderState render_state;
	ApplyActiveClipRegion(render_interface, render_state);
}

void ElementUtilities::ApplyActiveClipRegion(RenderInterface* render_interface, RenderState& render_state)
{
	RMLUI_ASSERT(render_interface);

	const bool scissoring_enabled = (render_state.clip_dimensions != Vector2i(-1, -1));
	if (scissoring_enabled)
	{
		render_interface->EnableScissorRegion(true);
		render_interface->SetScissorRegion(render_state.clip_origin.x, render_state.clip_origin.y, render_state.clip_dimensions.x,
			render_state.clip_dimensions.y);
	}
	else
	{
		render_interface->EnableScissorRegion(false);
	}

	const ElementClipList& stencil_elements = render_state.clip_stencil_elements;
	const bool stencil_test_enabled = !stencil_elements.empty();

	if (stencil_test_enabled)
	{
		const Matrix4f* active_transform = render_state.transform_pointer;

		bool first_clip_mask = true;
		for (const ElementClip& element_clip : stencil_elements)
		{
			const Box::Area clip_area = element_clip.clip_area;
			Element* stencil_element = element_clip.element;
			const Box& box = stencil_element->GetBox();
			const ComputedValues& computed = stencil_element->GetComputedValues();
			const Vector4f radii(computed.border_top_left_radius(), computed.border_top_right_radius(), computed.border_bottom_right_radius(),
				computed.border_bottom_left_radius());

			ApplyTransform(stencil_element);

			// @performance: Store clipping geometry on element.
			Geometry geometry;
			GeometryUtilities::GenerateBackground(&geometry, box, {}, radii, Colourb(255), clip_area);

			const ClipMask clip_mask = (first_clip_mask ? ClipMask::Clip : ClipMask::ClipIntersect);
			geometry.SetClipMask(clip_mask, stencil_element->GetAbsoluteOffset(Box::BORDER));
			first_clip_mask = false;
		}

		// Apply the initially set transform in case it was changed.
		ApplyTransform(render_interface, render_state, active_transform);
	}

	render_interface->EnableClipMask(stencil_test_enabled);
}

bool ElementUtilities::GetBoundingBox(Vector2f& out_offset, Vector2f& out_size, Element* element, PaintArea area, Vector2f expand_top_left,
	Vector2f expand_bottom_right)
{
	RMLUI_ASSERT(element);

	const Box::Area box_area = ToBoxArea(area);
	const Vector2f element_origin = element->GetAbsoluteOffset(box_area);
	const Vector2f element_size = element->GetBox().GetSize(box_area);

	if (area == PaintArea::Auto)
	{
		// Extend the bounding box to include the element's box-shadow.
		if (const Property* p_box_shadow = element->GetLocalProperty(PropertyId::BoxShadow))
		{
			RMLUI_ASSERT(p_box_shadow->value.GetType() == Variant::SHADOWLIST);
			const ShadowList& shadow_list = p_box_shadow->value.GetReference<ShadowList>();

			Vector2f extend_min;
			Vector2f extend_max;
			for (const Shadow& shadow : shadow_list)
			{
				if (!shadow.inset)
				{
					const float extend = shadow.blur_radius + shadow.spread_distance;
					extend_min = Math::Min(extend_min, shadow.offset - Vector2f(extend));
					extend_max = Math::Max(extend_max, shadow.offset + Vector2f(extend));
				}
			}
			expand_top_left -= extend_min;
			expand_bottom_right += extend_max;
		}
	}

	const TransformState* transform_state = element->GetTransformState();
	const Matrix4f* transform = (transform_state ? transform_state->GetTransform() : nullptr);

	// Early exit in the common case of no transform.
	if (!transform)
	{
		out_offset = element_origin - expand_top_left;
		out_size = element_size + expand_top_left + expand_bottom_right;
		Math::ExpandToPixelGrid(out_offset, out_size);
		return true;
	}

	Context* context = element->GetContext();
	if (!context)
		return false;

	constexpr int num_corners = 4;
	Vector2f corners[num_corners] = {element_origin, element_origin + Vector2f(element_size.x, 0), element_origin + element_size,
		element_origin + Vector2f(0, element_size.y)};

	// Transform and project corners to window coordinates.
	const Vector2f window_size = Vector2f(context->GetDimensions());
	const Matrix4f project = Matrix4f::ProjectOrtho(0.f, window_size.x, 0.f, window_size.y, -1.f, 1.f);
	const Matrix4f project_transform = project * (*transform);

	for (int i = 0; i < num_corners; i++)
	{
		const Vector4f pos_transformed = project_transform * Vector4f(corners[i].x, corners[i].y, 0, 1);
		const Vector2f pos_ndc = Vector2f(pos_transformed.x, pos_transformed.y) / pos_transformed.w;
		const Vector2f pos_viewport = 0.5f * window_size * (pos_ndc + Vector2f(1));
		corners[i] = pos_viewport;
	}

	// Find the rectangle covering the projected corners.
	Vector2f pos_min = corners[0];
	Vector2f pos_max = corners[0];
	for (int i = 1; i < num_corners; i++)
	{
		pos_min = Math::Min(pos_min, corners[i]);
		pos_max = Math::Max(pos_max, corners[i]);
	}

	out_offset = pos_min - expand_top_left;
	out_size = pos_max + expand_bottom_right - out_offset;
	Math::ExpandToPixelGrid(out_offset, out_size);

	return true;
}

// Formats the contents of an element.
void ElementUtilities::FormatElement(Element* element, Vector2f containing_block)
{
	LayoutEngine::FormatElement(element, containing_block);
}

// Generates the box for an element.
void ElementUtilities::BuildBox(Box& box, Vector2f containing_block, Element* element, bool inline_element)
{
	LayoutDetails::BuildBox(box, containing_block, element, inline_element ? BoxContext::Inline : BoxContext::Block);
}

// Sizes an element, and positions it within its parent offset from the borders of its content area.
bool ElementUtilities::PositionElement(Element* element, Vector2f offset, PositionAnchor anchor)
{
	Element* parent = element->GetParentNode();
	if (parent == nullptr)
		return false;

	SetBox(element);

	Vector2f containing_block = element->GetParentNode()->GetBox().GetSize(Box::CONTENT);
	Vector2f element_block = element->GetBox().GetSize(Box::MARGIN);

	Vector2f resolved_offset = offset;

	if (anchor & RIGHT)
		resolved_offset.x = containing_block.x - (element_block.x + offset.x);

	if (anchor & BOTTOM)
		resolved_offset.y = containing_block.y - (element_block.y + offset.y);

	SetElementOffset(element, resolved_offset);

	return true;
}

bool ElementUtilities::ApplyTransform(Element* element)
{
	RMLUI_ASSERT(element);
	Context* context = element->GetContext();
	RenderInterface* render_interface = context ? context->GetRenderInterface() : nullptr;

	if (!render_interface || !context)
		return false;

	RenderState& render_state = context->GetRenderState();

	const Matrix4f* new_transform = nullptr;
	if (element)
	{
		if (const TransformState* state = element->GetTransformState())
			new_transform = state->GetTransform();
	}

	ApplyTransform(render_interface, render_state, new_transform);

	return true;
}

void ElementUtilities::ApplyTransform(RenderInterface* render_interface, RenderState& render_state, const Matrix4f* new_transform)
{
	RMLUI_ASSERT(render_interface);
	const Matrix4f*& old_transform = render_state.transform_pointer;

	// Only changed transforms are submitted.
	if (old_transform != new_transform)
	{
		Matrix4f& old_transform_value = render_state.transform;

		// Do a deep comparison as well to avoid submitting a new transform which is equal.
		if (!old_transform || !new_transform || (old_transform_value != *new_transform))
		{
			render_interface->SetTransform(new_transform);

			if (new_transform)
				old_transform_value = *new_transform;
		}

		old_transform = new_transform;
	}
}

static bool ApplyDataViewsControllersInternal(Element* element, const bool construct_structural_view, const String& structural_view_inner_rml)
{
	RMLUI_ASSERT(element);
	bool result = false;

	// If we have an active data model, check the attributes for any data bindings
	if (DataModel* data_model = element->GetDataModel())
	{
		struct ViewControllerInitializer {
			String type;
			String modifier_or_inner_rml;
			String expression;
			DataViewPtr view;
			DataControllerPtr controller;
			explicit operator bool() const { return view || controller; }
		};

		// Since data views and controllers may modify the element's attributes during initialization, we 
		// need to iterate over all the attributes _before_ initializing any views or controllers. We store
		// the information needed to initialize them in the following container.
		Vector<ViewControllerInitializer> initializer_list;

		for (auto& attribute : element->GetAttributes())
		{
			// Data views and controllers are declared by the following element attribute:
			//     data-[type]-[modifier]="[expression]"

			constexpr size_t data_str_length = sizeof("data-") - 1;

			const String& name = attribute.first;

			if (name.size() > data_str_length && name[0] == 'd' && name[1] == 'a' && name[2] == 't' && name[3] == 'a' && name[4] == '-')
			{
				const size_t type_end = name.find('-', data_str_length);
				const size_t type_size = (type_end == String::npos ? String::npos : type_end - data_str_length);
				String type_name = name.substr(data_str_length, type_size);

				ViewControllerInitializer initializer;

				// Structural data views are applied in a separate step from the normal views and controllers.
				if (construct_structural_view)
				{
					if (DataViewPtr view = Factory::InstanceDataView(type_name, element, true))
					{
						initializer.modifier_or_inner_rml = structural_view_inner_rml;
						initializer.view = std::move(view);
					}
				}
				else
				{
					if (Factory::IsStructuralDataView(type_name))
					{
						// Structural data views should cancel all other non-structural data views and controllers. Exit now.
						// Eg. in elements with a 'data-for' attribute, the data views should be constructed on the generated
						// children elements and not on the current element generating the 'for' view.
						return false;
					}

					const size_t modifier_offset = data_str_length + type_name.size() + 1;
					if (modifier_offset < name.size())
						initializer.modifier_or_inner_rml = name.substr(modifier_offset);

					if (DataViewPtr view = Factory::InstanceDataView(type_name, element, false))
						initializer.view = std::move(view);

					if (DataControllerPtr controller = Factory::InstanceDataController(type_name, element))
						initializer.controller = std::move(controller);
				}

				if (initializer)
				{
					initializer.type = std::move(type_name);
					initializer.expression = attribute.second.Get<String>();

					initializer_list.push_back(std::move(initializer));
				}
			}
		}

		// Now, we can safely initialize the data views and controllers, even modifying the element's attributes when desired.
		for (ViewControllerInitializer& initializer : initializer_list)
		{
			DataViewPtr& view = initializer.view;
			DataControllerPtr& controller = initializer.controller;

			if (view)
			{
				if (view->Initialize(*data_model, element, initializer.expression, initializer.modifier_or_inner_rml))
				{
					data_model->AddView(std::move(view));
					result = true;
				}
				else
					Log::Message(Log::LT_WARNING, "Could not add data-%s view to element: %s", initializer.type.c_str(), element->GetAddress().c_str());
			}

			if (controller)
			{
				if (controller->Initialize(*data_model, element, initializer.expression, initializer.modifier_or_inner_rml))
				{
					data_model->AddController(std::move(controller));
					result = true;
				}
				else
					Log::Message(Log::LT_WARNING, "Could not add data-%s controller to element: %s", initializer.type.c_str(), element->GetAddress().c_str());
			}
		}
	}

	return result;
}


bool ElementUtilities::ApplyDataViewsControllers(Element* element)
{
	return ApplyDataViewsControllersInternal(element, false, String());
}

bool ElementUtilities::ApplyStructuralDataViews(Element* element, const String& inner_rml)
{
	return ApplyDataViewsControllersInternal(element, true, inner_rml);
}

} // namespace Rml
