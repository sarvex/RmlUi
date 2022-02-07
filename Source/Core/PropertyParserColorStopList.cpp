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

#include "PropertyParserColorStopList.h"
#include <string.h>

namespace Rml {

PropertyParserColorStopList::PropertyParserColorStopList(PropertyParser* parser_color, PropertyParser* parser_number_length_percent) :
	parser_color(parser_color), parser_number_length_percent(parser_number_length_percent)
{
	RMLUI_ASSERT(parser_color && parser_number_length_percent);
}

PropertyParserColorStopList::~PropertyParserColorStopList() {}

bool PropertyParserColorStopList::ParseValue(Property& property, const String& value, const ParameterMap& /*parameters*/) const
{
	const ParameterMap empty_parameter_map;

	if (value.empty())
		return false;

	StringList color_stop_str_list;
	StringUtilities::ExpandString(color_stop_str_list, value);

	if (color_stop_str_list.empty())
		return false;

	ColorStopList color_stops;
	color_stops.reserve(color_stop_str_list.size());

	using Style::LengthPercentageAuto;

	for (const String& color_stop_str : color_stop_str_list)
	{
		StringList color_stop_str_pair;
		StringUtilities::ExpandString(color_stop_str_pair, color_stop_str, ' ', '(', ')', true);

		if (color_stop_str_pair.empty() || color_stop_str_pair.size() > 2)
			return false;

		Property p_color;
		if (!parser_color->ParseValue(p_color, color_stop_str_pair[0], empty_parameter_map))
			return false;

		ColorStop color_stop = {};
		color_stop.color = p_color.Get<Colourb>();

		Property p_position(LengthPercentageAuto::Auto);
		if (color_stop_str_pair.size() == 2 && color_stop_str_pair[1] != "auto")
		{
			if (!parser_number_length_percent->ParseValue(p_position, color_stop_str_pair[1], empty_parameter_map))
				return false;
		}

		switch (p_position.unit)
		{
		case Property::KEYWORD:
			color_stop.position = ColorStop::Position::Auto;
			break;
		case Property::NUMBER:
			color_stop.position = ColorStop::Position::Number;
			color_stop.position_value = p_position.Get<float>();
			break;
		case Property::PERCENT:
			color_stop.position = ColorStop::Position::Number;
			color_stop.position_value = 0.01f * p_position.Get<float>();
			break;
		case Property::PX:
			color_stop.position = ColorStop::Position::Length;
			color_stop.position_value = p_position.Get<float>();
			break;
		default:
			Log::Message(Log::LT_WARNING,
				"Unsupported color stop position unit encountered in '%s'. Only number, percent, 'px' and 'auto' values supported.",
				p_position.ToString().c_str());
			return false;
		}

		color_stops.push_back(color_stop);
	}

	property.value = Variant(std::move(color_stops));
	property.unit = Property::COLORSTOPLIST;

	return true;
}

} // namespace Rml
