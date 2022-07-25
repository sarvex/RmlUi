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

#include "../../Include/RmlUi/Core/RenderState.h"
#include "../../Include/RmlUi/Core/Box.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/Geometry.h"
#include "../../Include/RmlUi/Core/GeometryUtilities.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "TransformState.h"

namespace Rml {

RenderState::RenderState(RenderInterface* render_interface) : render_interface(render_interface), stack(1) {}

void RenderState::BeginRender()
{
	RMLUI_ASSERTMSG(stack.size() == 1, "Unbalanced render state push/pop detected.");

	render_interface->EnableScissorRegion(false);
	supports_clip_mask = render_interface->EnableClipMask(false);
	render_interface->SetTransform(nullptr);

	stack.back() = State{};
}

void RenderState::Reset()
{
	Set(State{});
}

void RenderState::DisableScissorRegion()
{
	SetScissorRegion(Rectanglei::CreateInvalid());
}

void RenderState::SetScissorRegion(Rectanglei new_region)
{
	State& state = stack.back();
	const bool old_scissor_enable = state.scissor_region.Valid();
	const bool new_scissor_enable = new_region.Valid();

	if (new_scissor_enable != old_scissor_enable)
		render_interface->EnableScissorRegion(new_scissor_enable);

	if (new_scissor_enable)
	{
		new_region.Intersect(Rectanglei::FromSize(viewport_dimensions));

		if (new_region != state.scissor_region)
			render_interface->SetScissorRegion(new_region.Left(), new_region.Top(), new_region.Width(), new_region.Height());
	}

	state.scissor_region = new_region;
}

void RenderState::DisableClipMask()
{
	State& state = stack.back();
	if (!state.clip_mask_elements.empty())
	{
		state.clip_mask_elements.clear();
		ApplyClipMask(state.clip_mask_elements);
	}
}

void RenderState::SetClipMask(ClipMaskOperation clip_mask, Geometry* geometry, Vector2f translation)
{
	RMLUI_ASSERT(geometry);
	State& state = stack.back();
	state.clip_mask_elements = {ElementClip{clip_mask, geometry, translation, nullptr}};
	ApplyClipMask(state.clip_mask_elements);
}

void RenderState::SetClipMask(ElementClipList in_clip_elements)
{
	State& state = stack.back();
	if (state.clip_mask_elements != in_clip_elements)
	{
		state.clip_mask_elements = std::move(in_clip_elements);
		ApplyClipMask(state.clip_mask_elements);
	}
}

void RenderState::SetTransform(const Matrix4f* p_new_transform)
{
	State& state = stack.back();
	const Matrix4f*& p_active_transform = state.transform_pointer;

	// Only changed transforms are submitted.
	if (p_active_transform != p_new_transform)
	{
		Matrix4f& active_transform = state.transform;

		// Do a deep comparison as well to avoid submitting a new transform which is equal.
		if (!p_active_transform || !p_new_transform || (active_transform != *p_new_transform))
		{
			render_interface->SetTransform(p_new_transform);

			if (p_new_transform)
				active_transform = *p_new_transform;
		}

		p_active_transform = p_new_transform;
	}
}

Rectanglei RenderState::GetScissorState() const
{
	return stack.back().scissor_region;
}

void RenderState::ApplyClipMask(const ElementClipList& clip_elements)
{
	const bool clip_mask_enabled = !clip_elements.empty();
	render_interface->EnableClipMask(clip_mask_enabled);

	if (clip_mask_enabled)
	{
		const Matrix4f* initial_transform = stack.back().transform_pointer;

		for (const ElementClip& element_clip : clip_elements)
		{
			SetTransform(element_clip.transform);
			element_clip.clip_geometry->RenderToClipMask(element_clip.clip_mask, element_clip.absolute_offset);
		}

		// Apply the initially set transform in case it was changed.
		// TODO: Is it safe to dereference this old pointer?
		SetTransform(initial_transform);
	}
}

void RenderState::SetViewport(Vector2i dimensions)
{
	viewport_dimensions = dimensions;
}

void RenderState::Push()
{
	stack.push_back(State(stack.back()));
}

void RenderState::Pop()
{
	if (stack.size() >= 2)
	{
		const State& next = *(stack.end() - 2);
		Set(next);
		stack.pop_back();
	}
	else
	{
		RMLUI_ERRORMSG("Unbalanced render state push/pop.");
	}
}

void RenderState::Set(const State& next)
{
	SetScissorRegion(next.scissor_region);
	
	SetClipMask(next.clip_mask_elements);

	// TODO: Is it safe to submit an old pointer here (e.g. in case of Pop())?
	SetTransform(next.transform_pointer);
}

} // namespace Rml
