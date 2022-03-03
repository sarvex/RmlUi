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

#ifndef RMLUI_CORE_TYPES_H
#define RMLUI_CORE_TYPES_H

#include "../Config/Config.h"

#include <cstdlib>
#include <memory>

#include "Traits.h"

namespace Rml {

// Commonly used basic types
using byte = unsigned char;
using ScriptObject = void*;
using std::size_t;

// Unicode code point
enum class Character : char32_t { Null, Replacement = 0xfffd };

// TODO
enum class RenderStage { Enter, Decoration, Exit };

}

#include "Colour.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Rectangle.h"
#include "Matrix4.h"
#include "ObserverPtr.h"

namespace Rml {

// Color and linear algebra
enum class ColorFormat { RGBA8, A8 };
using Colourf = Colour< float, 1 >;
using Colourb = Colour< byte, 255 >;
using Vector2i = Vector2< int >;
using Vector2f = Vector2< float >;
using Vector3i = Vector3< int >;
using Vector3f = Vector3< float >;
using Vector4i = Vector4< int >;
using Vector4f = Vector4< float >;
using Rectanglei = Rectangle< int >;
using Rectanglef = Rectangle< float >;
using ColumnMajorMatrix4f = Matrix4< float, ColumnMajorStorage< float > >;
using RowMajorMatrix4f = Matrix4< float, RowMajorStorage< float > >;
using Matrix4f = RMLUI_MATRIX4_TYPE;

// Common classes
class Element;
class ElementInstancer;
class ElementAnimation;
class Context;
class Event;
class Property;
class Variant;
class Transform;
class PropertyIdSet;
class Decorator;
class FontEffect;
struct Animation;
struct Transition;
struct TransitionList;
struct DecoratorDeclarationList;
enum class EventId : uint16_t;
enum class PropertyId : uint8_t;
enum class MediaQueryId : uint8_t;
enum class FamilyId : int;

// Types for external interfaces.
using FileHandle = uintptr_t;
using TextureHandle = uintptr_t;
using CompiledGeometryHandle = uintptr_t;
using DecoratorDataHandle = uintptr_t;
using FontFaceHandle = uintptr_t;
using FontEffectsHandle = uintptr_t;

using ElementPtr = UniqueReleaserPtr<Element>;
using ContextPtr = UniqueReleaserPtr<Context>;
using EventPtr = UniqueReleaserPtr<Event>;

// Container types for common classes
using ElementList = Vector< Element* >;
using OwnedElementList = Vector< ElementPtr >;
using VariantList = Vector< Variant >;
using ElementAnimationList = Vector< ElementAnimation >;

using AttributeNameList = SmallUnorderedSet< String >;
using PropertyMap = UnorderedMap< PropertyId, Property >;

using Dictionary = SmallUnorderedMap< String, Variant >;
using ElementAttributes = Dictionary;
using XMLAttributes = Dictionary;

using AnimationList = Vector<Animation>;
using FontEffectList = Vector<SharedPtr<const FontEffect>>;
struct FontEffects {
	FontEffectList list;
	String value;
};

// TODO, move
struct ColorStop {
	enum class Position { Auto, Number, Length };
	Colourb color;
	Position position;
	float position_value;
};
inline bool operator==(const ColorStop& a, const ColorStop& b)
{
	return a.color == b.color && a.position == b.position && a.position_value == b.position_value;
}
inline bool operator!=(const ColorStop& a, const ColorStop& b)
{
	return !(a == b);
}
using ColorStopList = Vector<ColorStop>;

// TODO, move
struct Shadow {
	Colourb color;
	Vector2f offset;
	float blur_radius = 0;
	float spread_distance = 0;
	bool inset = false;
};
using ShadowList = Vector<Shadow>;
inline bool operator==(const Shadow& a, const Shadow& b)
{
	return a.color == b.color && a.offset == b.offset && a.blur_radius == b.blur_radius && a.spread_distance == b.spread_distance &&
		a.inset == b.inset;
}
inline bool operator!=(const Shadow& a, const Shadow& b)
{
	return !(a == b);
}
enum class DecoratorClasses {
	Invalid = 0,
	Background = 1,
	Filter = 2,
	BackdropFilter = 4,
};
inline DecoratorClasses operator|(DecoratorClasses a, DecoratorClasses b)
{
	return DecoratorClasses(int(a) | int(b));
}
inline DecoratorClasses operator&(DecoratorClasses a, DecoratorClasses b)
{
	return DecoratorClasses(int(a) & int(b));
}

// Additional smart pointers
using TransformPtr = SharedPtr< Transform >;
using DecoratorsPtr = SharedPtr<const DecoratorDeclarationList>;
using FontEffectsPtr = SharedPtr<const FontEffects>;

// Data binding types
class DataView;
using DataViewPtr = UniqueReleaserPtr<DataView>;
class DataController;
using DataControllerPtr = UniqueReleaserPtr<DataController>;

} // namespace Rml


namespace std {
// Hash specialization for enum class types (required on some older compilers)
template <> struct hash<::Rml::PropertyId> {
	using utype = typename ::std::underlying_type<::Rml::PropertyId>::type;
	size_t operator() (const ::Rml::PropertyId& t) const { ::Rml::Hash<utype> h; return h(static_cast<utype>(t)); }
};
template <> struct hash<::Rml::Character> {
	using utype = typename ::std::underlying_type<::Rml::Character>::type;
	size_t operator() (const ::Rml::Character& t) const { ::Rml::Hash<utype> h; return h(static_cast<utype>(t)); }
};
template <> struct hash<::Rml::FamilyId> {
	using utype = typename ::std::underlying_type<::Rml::FamilyId>::type;
	size_t operator() (const ::Rml::FamilyId& t) const { ::std::hash<utype> h; return h(static_cast<utype>(t)); }
};
}

#endif
