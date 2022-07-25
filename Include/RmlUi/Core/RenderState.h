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

#ifndef RMLUI_CORE_RENDERSTATE_H
#define RMLUI_CORE_RENDERSTATE_H

#include "Box.h"
#include "RenderInterface.h"
#include "Types.h"

namespace Rml {

class Geometry;
class RenderInterface;
class RenderStateSession;

struct ElementClip {
	ClipMaskOperation clip_mask;
	Geometry* clip_geometry;
	Vector2f absolute_offset;
	const Matrix4f* transform;
};
inline bool operator==(const ElementClip& a, const ElementClip& b)
{
	return a.clip_mask == b.clip_mask && a.clip_geometry == b.clip_geometry && a.absolute_offset == b.absolute_offset && a.transform == b.transform;
}
inline bool operator!=(const ElementClip& a, const ElementClip& b)
{
	return !(a == b);
}
using ElementClipList = Vector<ElementClip>;

/**
    A wrapper over the render interface which tracks the following state:
       - Scissor
       - Clip mask
       - Transform
    All such operations on the render interface should go through this class. Pushing and popping the render state is supported through the
    RenderStateSession() object.
 */
class RMLUICORE_API RenderState : NonCopyMoveable {
public:
	RenderState(RenderInterface* render_interface);

	void BeginRender();

	void Reset();

	void DisableScissorRegion();
	void SetScissorRegion(Rectanglei region);

	void DisableClipMask();
	void SetClipMask(ElementClipList clip_elements);
	void SetClipMask(ClipMaskOperation clip_mask, Geometry* geometry, Vector2f translation);

	void SetTransform(const Matrix4f* new_transform);

	// Returns the scissor region if it is enabled, otherwise an invalid rectangle.
	Rectanglei GetScissorState() const;

	bool SupportsClipMask() const { return supports_clip_mask; }
	RenderInterface* GetRenderInterface() const { return render_interface; }

	void SetViewport(Vector2i dimensions);

private:
	struct State {
		Rectanglei scissor_region = Rectanglei::CreateInvalid();
		ElementClipList clip_mask_elements;
		const Matrix4f* transform_pointer = nullptr;
		Matrix4f transform;
	};

	void Push();
	void Pop();
	void Set(const State& next);

	void ApplyClipMask(const ElementClipList& clip_elements);

	RenderInterface* render_interface = nullptr;
	Vector2i viewport_dimensions;
	Vector<State> stack;
	bool supports_clip_mask = false;

	friend class Rml::RenderStateSession;
};

/**
    A RAII wrapper which pushes a new render state on construction and pops it on destruction, thereby restoring the original render state.

    Should only be constructed as a stack object.
 */
class RMLUICORE_API RenderStateSession : NonCopyMoveable {
public:
	explicit RenderStateSession(RenderState& render_state) : render_state(&render_state) { render_state.Push(); }
	~RenderStateSession() { Reset(); }

	void Reset()
	{
		if (render_state)
		{
			render_state->Pop();
			render_state = nullptr;
		}
	}

private:
	RenderState* render_state;
};

} // namespace Rml

#endif
