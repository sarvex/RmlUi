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

#include "../../Include/RmlUi/Core/Box.h"
#include "../../Include/RmlUi/Core/DecorationTypes.h"
#include <string.h>

namespace Rml {

// Initialises a zero-sized box.
Box::Box() : content(0, 0)
{
	memset(area_edges, 0, sizeof(area_edges));
}

// Initialises a box with a default content area and no padding, borders and margins.
Box::Box(Vector2f content) : content(content)
{
	memset(area_edges, 0, sizeof(area_edges));
}

Box::~Box()
{
}

// Returns the top-left position of one of the areas.
Vector2f Box::GetPosition(BoxArea area) const
{
	RMLUI_ASSERT(area != BoxArea::Auto);
	Vector2f area_position(-GetEdge(BoxArea::Margin, BoxEdge::Left), -GetEdge(BoxArea::Margin, BoxEdge::Top));
	for (int i = 0; i < (int)area; i++)
	{
		area_position.x += area_edges[i][(int)BoxEdge::Left];
		area_position.y += area_edges[i][(int)BoxEdge::Top];
	}

	return area_position;
}

Vector2f Box::GetSize() const
{
	return content;
}

Vector2f Box::GetSize(BoxArea area) const
{
	RMLUI_ASSERT(area != BoxArea::Auto);
	Vector2f area_size(content);
	for (int i = (int)area; i <= (int)BoxArea::Padding; i++)
	{
		area_size.x += (area_edges[i][(int)BoxEdge::Left] + area_edges[i][(int)BoxEdge::Right]);
		area_size.y += (area_edges[i][(int)BoxEdge::Top] + area_edges[i][(int)BoxEdge::Bottom]);
	}

	return area_size;
}

// Sets the size of the content area.
void Box::SetContent(Vector2f _content)
{
	content = _content;
}

// Sets the size of one of the segments of one of the box's outer areas.
void Box::SetEdge(BoxArea area, BoxEdge edge, float size)
{
	RMLUI_ASSERT(area != BoxArea::Auto);
	area_edges[(int)area][(int)edge] = size;
}

// Returns the size of one of the area segments.
float Box::GetEdge(BoxArea area, BoxEdge edge) const
{
	RMLUI_ASSERT(area != BoxArea::Auto);
	return area_edges[(int)area][(int)edge];
}

// Returns the cumulative size of one edge up to one of the box's areas.
float Box::GetCumulativeEdge(BoxArea area, BoxEdge edge) const
{
	RMLUI_ASSERT(area != BoxArea::Auto);
	float size = 0;
	int max_area = Math::Min((int)area, (int)BoxArea::Padding);
	for (int i = 0; i <= max_area; i++)
		size += area_edges[i][(int)edge];

	return size;
}

float Box::GetSizeAcross(BoxDirection direction, BoxArea area, BoxArea area_end) const
{
	static_assert((int)BoxDirection::Horizontal == 1 && (int)BoxDirection::Vertical == 0, "");
	RMLUI_ASSERT((int)area <= (int)area_end && (int)direction <= 1 && area_end != BoxArea::Auto);

	float size = 0.0f;

	if (area_end == BoxArea::Content)
		size = (direction == BoxDirection::Horizontal ? content.x : content.y);
	
	for (int i = (int)area; i <= (int)area_end && i < (int)BoxArea::Content; i++)
		size += (area_edges[i][(int)BoxEdge::Top + (int)direction] + area_edges[i][(int)BoxEdge::Bottom + (int)direction]);

	return size;
}

// Compares the size of the content area and the other area edges.
bool Box::operator==(const Box& rhs) const
{
	return content == rhs.content && memcmp(area_edges, rhs.area_edges, sizeof(area_edges)) == 0;
}

// Compares the size of the content area and the other area edges.
bool Box::operator!=(const Box& rhs) const
{
	return !(*this == rhs);
}

} // namespace Rml
