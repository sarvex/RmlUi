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

#include "ElementDecoration.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/Decorator.h"
#include "../../Include/RmlUi/Core/DecoratorInstancer.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementDocument.h"
#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/Profiling.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "../../Include/RmlUi/Core/StyleSheet.h"

namespace Rml {

ElementDecoration::ElementDecoration(Element* _element) : element(_element) {}

ElementDecoration::~ElementDecoration()
{
	ReleaseDecorators();
}

void ElementDecoration::InstanceDecorators()
{
	if (decorators_dirty)
	{
		decorators_dirty = false;
		decorators_data_dirty = true;
		ReloadDecorators();
	}
}

// Releases existing decorators and loads all decorators required by the element's definition.
void ElementDecoration::ReloadDecorators()
{
	RMLUI_ZoneScopedC(0xB22222);
	ReleaseDecorators();

	num_backgrounds = 0;
	num_filters = 0;
	num_backdrop_filters = 0;
	num_mask_images = 0;

	const ComputedValues& computed = element->GetComputedValues();

	if (!computed.has_decorator() && !computed.has_filter() && !computed.has_backdrop_filter() && !computed.has_mask_image())
		return;

	for (const PropertyId id : {PropertyId::Decorator, PropertyId::BackdropFilter, PropertyId::Filter, PropertyId::MaskImage})
	{
		const Property* property = element->GetLocalProperty(id);
		if (!property || property->unit != Property::DECORATOR)
			continue;

		DecoratorsPtr decorators_ptr = property->Get<DecoratorsPtr>();
		if (!decorators_ptr)
			continue;

		const StyleSheet* style_sheet = element->GetStyleSheet();
		if (!style_sheet)
			return;

		PropertySource document_source("", 0, "");
		const PropertySource* source = property->source.get();

		if (!source)
		{
			if (ElementDocument* document = element->GetOwnerDocument())
			{
				document_source.path = document->GetSourceURL();
				source = &document_source;
			}
		}

		const auto& decorator_list = style_sheet->InstanceDecorators(*decorators_ptr, source);

		const int list_size = (int)decorator_list.size();

		DecoratorClasses decorator_class = DecoratorClasses::Invalid;
		switch (id)
		{
		case PropertyId::Decorator:
			decorator_class = DecoratorClasses::Background;
			num_backgrounds = list_size;
			break;
		case PropertyId::Filter:
			decorator_class = DecoratorClasses::Filter;
			num_filters = list_size;
			break;
		case PropertyId::BackdropFilter:
			decorator_class = DecoratorClasses::BackdropFilter;
			num_backdrop_filters = list_size;
			break;
		case PropertyId::MaskImage:
			decorator_class = DecoratorClasses::MaskImage;
			num_mask_images = list_size;
			break;
		default:
			RMLUI_ERROR;
			break;
		}

		for (const SharedPtr<const Decorator>& decorator : decorator_list)
		{
			if (decorator)
			{
				DecoratorHandle decorator_handle;
				decorator_handle.decorator_data = 0;
				decorator_handle.decorator_class = decorator_class;
				decorator_handle.decorator = decorator;

				decorators.push_back(std::move(decorator_handle));
			}
		}
	}
}

// Loads a single decorator and adds it to the list of loaded decorators for this element.
void ElementDecoration::ReloadDecoratorsData()
{
	if (decorators_data_dirty)
	{
		decorators_data_dirty = false;

		for (DecoratorHandle& decorator : decorators)
		{
			if (decorator.decorator_data)
				decorator.decorator->ReleaseElementData(decorator.decorator_data);

			const DecoratorPaintingArea painting_area =
				(decorator.decorator_class == DecoratorClasses::Background ? DecoratorPaintingArea::PaddingBox : DecoratorPaintingArea::BorderBox);

			decorator.decorator_data = decorator.decorator->GenerateElementData(element, painting_area);
		}
	}
}

// Releases all existing decorators and frees their data.
void ElementDecoration::ReleaseDecorators()
{
	for (DecoratorHandle& decorator : decorators)
	{
		if (decorator.decorator_data)
			decorator.decorator->ReleaseElementData(decorator.decorator_data);
	}

	decorators.clear();
}

void ElementDecoration::RenderDecorators(RenderStage render_stage)
{
	InstanceDecorators();
	ReloadDecoratorsData();

	RMLUI_ASSERT(num_backgrounds + num_filters + num_backdrop_filters + num_mask_images == (int)decorators.size());

	if (num_backgrounds > 0)
	{
		if (render_stage == RenderStage::Decoration)
		{
			// Render the decorators attached to this element in its current state.
			// Render from back to front for correct render order.
			for (int i = num_backgrounds - 1; i >= 0; i--)
			{
				DecoratorHandle& decorator = decorators[i];
				decorator.decorator->RenderElement(element, decorator.decorator_data);
			}
		}
	}

	if (!num_backdrop_filters && !num_filters && !num_mask_images)
		return;

	Context* context = element->GetContext();
	RenderInterface* render_interface = context ? context->GetRenderInterface() : nullptr;
	if (!context || !render_interface)
		return;

	if (num_backdrop_filters > 0)
	{
		if (render_stage == RenderStage::Enter)
		{
			ElementUtilities::ApplyTransform(element);
			ElementUtilities::SetClippingRegion(element, true);

			Vector2f filter_origin, filter_size;
			ElementUtilities::GetElementRegionInWindowSpace(filter_origin, filter_size, element, Box::BORDER);
			// @performance Shrink scissor region to element region

			const int i0 = num_backgrounds;
			for (int i = i0; i < i0 + num_backdrop_filters; i++)
			{
				DecoratorHandle& decorator = decorators[i];
				decorator.decorator->RenderElement(element, decorator.decorator_data);
			}

			// ElementUtilities::ForceClippingRegion(element, Box::BORDER);
			render_interface->StackApply(BlitDestination::Stack, Vector2i(filter_origin), Vector2i(filter_size));
			ElementUtilities::SetClippingRegion(element);
		}
	}

	if (num_filters > 0 || num_mask_images > 0)
	{
		if (render_stage == RenderStage::Enter)
		{
			render_interface->StackPush();
		}
		else if (render_stage == RenderStage::Exit)
		{
			ElementUtilities::SetClippingRegion(element);

			Vector2f max_top_left, max_bottom_right;
			const int i0 = num_backgrounds + num_backdrop_filters;
			for (int i = i0; i < i0 + num_filters; i++)
			{
				DecoratorHandle& decorator = decorators[i];
				Vector2f top_left, bottom_right;

				decorator.decorator->GetClipExtension(top_left, bottom_right);

				max_top_left = Math::Max(max_top_left, top_left);
				max_bottom_right = Math::Max(max_bottom_right, bottom_right);
			}

			Vector2f filter_origin, filter_size;
			ElementUtilities::GetElementRegionInWindowSpace(filter_origin, filter_size, element, Box::BORDER, max_top_left, max_bottom_right);

			for (int i = i0; i < i0 + num_filters; i++)
			{
				DecoratorHandle& decorator = decorators[i];
				decorator.decorator->RenderElement(element, decorator.decorator_data);
			}

			if (num_mask_images > 0)
			{
				render_interface->StackPush();

				const int i0_mask = num_backgrounds + num_backdrop_filters + num_filters;
				for (int i = i0_mask; i < i0_mask + num_mask_images; i++)
				{
					DecoratorHandle& decorator = decorators[i];
					decorator.decorator->RenderElement(element, decorator.decorator_data);
				}

				render_interface->AttachMask(Vector2i(filter_origin), Vector2i(filter_size));
				render_interface->StackPop();
			}

			render_interface->StackApply(BlitDestination::BlendStackBelow, Vector2i(filter_origin), Vector2i(filter_size));
			render_interface->StackPop();
		}
	}
}

void ElementDecoration::DirtyDecorators()
{
	decorators_dirty = true;
}

void ElementDecoration::DirtyDecoratorsData()
{
	decorators_data_dirty = true;
}

} // namespace Rml
