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
#include "Types.h"

namespace Rml {

struct ElementClip {
	Element* element = nullptr;
	Box::Area clip_area = Box::Area::PADDING;
};
inline bool operator==(const ElementClip& a, const ElementClip& b)
{
	return a.element == b.element && a.clip_area == b.clip_area;
}
inline bool operator!=(const ElementClip& a, const ElementClip& b)
{
	return !(a == b);
}
using ElementClipList = Vector<ElementClip>;

struct RenderState {
	bool supports_stencil = false;
	Vector2i clip_origin = {-1, -1};
	Vector2i clip_dimensions = {-1, -1};
	ElementClipList clip_stencil_elements;
	const Matrix4f* transform_pointer = nullptr;
	Matrix4f transform;
};

} // namespace Rml

#endif
