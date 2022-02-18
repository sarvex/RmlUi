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
#include "../../Include/RmlUi/Core/Decorator.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementDocument.h"
#include "../../Include/RmlUi/Core/Profiling.h"
#include "../../Include/RmlUi/Core/DecoratorInstancer.h"
#include "../../Include/RmlUi/Core/StyleSheet.h"

namespace Rml {

ElementDecoration::ElementDecoration(Element* _element) : element(_element)
{}

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

	const ComputedValues& computed = element->GetComputedValues();

	if (!computed.has_decorator() && !computed.has_filter() && !computed.has_backdrop_filter())
		return;

	for (const PropertyId id : {PropertyId::Decorator, PropertyId::Filter, PropertyId::BackdropFilter})
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
		if (id == PropertyId::Decorator)
			num_backgrounds = list_size;
		else if (id == PropertyId::Filter)
			num_filters = list_size;
		else if (id == PropertyId::BackdropFilter)
			num_backdrop_filters = list_size;

		for (const SharedPtr<const Decorator>& decorator : decorator_list)
		{
			if (decorator)
			{
				DecoratorHandle decorator_handle;
				decorator_handle.decorator_data = 0;
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

			decorator.decorator_data = decorator.decorator->GenerateElementData(element);
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

	if (!num_backgrounds || !num_filters || !num_backdrop_filters)
		return;

	if (num_backgrounds > 0)
	{
		if (render_stage == RenderStage::Decoration)
		{
			// Render the decorators attached to this element in its current state.
			// Render from back to front for correct render order.
			for (int i = num_backgrounds - 1; i >= 0; i--)
			{
				DecoratorHandle& decorator = decorators[i];
				decorator.decorator->RenderElement(element, decorator.decorator_data, render_stage);
			}
		}
	}

	RenderInterface* render_interface = element->GetRenderInterface();
	if (!render_interface)
		return;

	if (num_filters > 0)
	{
		if (render_stage == RenderStage::Enter)
		{
			render_interface->ExecuteRenderCommand(RenderCommand::StackPush);
		}
		else if (render_stage == RenderStage::Exit)
		{
			render_interface->ExecuteRenderCommand(RenderCommand::StackToFilter);

			// TODO: clipping w/extents (or let decorators do it for us?)
			//ElementUtilities::ForceClippingRegion(element, Box::BORDER, Vector2f(-radius), Vector2f(2.f * radius));

			const int i0 = num_backgrounds;
			for (int i = i0 + num_filters - 1; i >= i0; i--)
			{
				DecoratorHandle& decorator = decorators[i];
				decorator.decorator->RenderElement(element, decorator.decorator_data, render_stage);
			}

			//render_interface->RenderEffect(CompiledEffectHandle(element_data), RenderSource::Stack, RenderTarget::StackBelow);

			render_interface->ExecuteRenderCommand(RenderCommand::StackPop);
			render_interface->ExecuteRenderCommand(RenderCommand::FilterToStack);

			//Rml::ElementUtilities::ApplyActiveClipRegion(element->GetContext(), render_interface);
		}
	}

	if (num_backdrop_filters > 0)
	{
		if (render_stage == RenderStage::BeforeDecoration)
		{
			render_interface->ExecuteRenderCommand(RenderCommand::StackToFilter);
			//ElementUtilities::ForceClippingRegion(element, Box::BORDER, Vector2f(-radius), Vector2f(2.f * radius));

			const int i0 = num_backgrounds + num_filters;
			for (int i = i0 + num_backdrop_filters - 1; i >= i0; i--)
			{
				DecoratorHandle& decorator = decorators[i];
				decorator.decorator->RenderElement(element, decorator.decorator_data, render_stage);
			}


			//render_interface->RenderEffect(CompiledEffectHandle(element_data), RenderSource::Stack, RenderTarget::Stack);

			render_interface->ExecuteRenderCommand(RenderCommand::FilterToStack);

			//Rml::ElementUtilities::ApplyActiveClipRegion(element->GetContext(), render_interface);
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
