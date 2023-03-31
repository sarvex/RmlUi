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

#include "RmlUi_Renderer_GL3.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/GeometryUtilities.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Platform.h>
#include <RmlUi/Core/SystemInterface.h>
#include <string.h>

#if defined(RMLUI_PLATFORM_WIN32) && !defined(__MINGW32__)
	// function call missing argument list
	#pragma warning(disable : 4551)
	// unreferenced local function has been removed
	#pragma warning(disable : 4505)
#endif

#if defined RMLUI_PLATFORM_EMSCRIPTEN
	#define RMLUI_SHADER_HEADER_VERSION "#version 300 es\nprecision highp float;\n"
	#include <GLES3/gl3.h>
#else
	#define RMLUI_SHADER_HEADER_VERSION "#version 330\n"
	#define GLAD_GL_IMPLEMENTATION
	#include "RmlUi_Include_GL3.h"
#endif

#define RMLUI_PREMULTIPLIED_ALPHA 1

#define MAX_NUM_STOPS 16
#define BLUR_SIZE 7
#define NUM_WEIGHTS ((BLUR_SIZE + 1) / 2)

#define RMLUI_STRINGIFY_IMPL(x) #x
#define RMLUI_STRINGIFY(x) RMLUI_STRINGIFY_IMPL(x)

#define RMLUI_SHADER_HEADER     \
	RMLUI_SHADER_HEADER_VERSION \
	"#define RMLUI_PREMULTIPLIED_ALPHA " RMLUI_STRINGIFY(RMLUI_PREMULTIPLIED_ALPHA) "\n#define MAX_NUM_STOPS " RMLUI_STRINGIFY(MAX_NUM_STOPS) "\n"

static constexpr int blur_size = BLUR_SIZE;
static constexpr int num_weights = NUM_WEIGHTS;

static const char* shader_vert_main = RMLUI_SHADER_HEADER R"(
uniform vec2 _translate;
uniform mat4 _transform;

in vec2 inPosition;
in vec4 inColor0;
in vec2 inTexCoord0;

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
	fragTexCoord = inTexCoord0;
	fragColor = inColor0;

#if RMLUI_PREMULTIPLIED_ALPHA
	// Pre-multiply vertex colors with their alpha.
	fragColor.rgb = fragColor.rgb * fragColor.a;
#endif

	vec2 translatedPos = inPosition + _translate;
	vec4 outPos = _transform * vec4(translatedPos, 0.0, 1.0);

    gl_Position = outPos;
}
)";
static const char* shader_frag_texture = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	finalColor = fragColor * texColor;
}
)";
static const char* shader_frag_color = RMLUI_SHADER_HEADER R"(
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	finalColor = fragColor;
}
)";
enum class ShaderGradientFunction { Linear, Radial, Conic, RepeatingLinear, RepeatingRadial, RepeatingConic }; // Must match shader defines below.
static const char* shader_frag_gradient = RMLUI_SHADER_HEADER R"(
#define LINEAR 0
#define RADIAL 1
#define CONIC 2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC 5
#define PI 3.14159265

uniform int _func; // one of above defines
uniform vec2 _p;   // linear: starting point,         radial: center,                        conic: center
uniform vec2 _v;   // linear: vector to ending point, radial: 2d curvature (inverse radius), conic: angled unit vector
uniform vec4 _stop_colors[MAX_NUM_STOPS];
uniform float _stop_positions[MAX_NUM_STOPS]; // normalized, 0 -> starting point, 1 -> ending point
uniform int _num_stops;

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

vec4 mix_stop_colors(float t) {
	vec4 color = _stop_colors[0];

	for (int i = 1; i < _num_stops; i++)
		color = mix(color, _stop_colors[i], smoothstep(_stop_positions[i-1], _stop_positions[i], t));

	return color;
}

void main() {
	float t = 0;

	if (_func == LINEAR || _func == REPEATING_LINEAR)
	{
		float dist_square = dot(_v, _v);
		vec2 V = fragTexCoord - _p;
		t = dot(_v, V) / dist_square;
	}
	else if (_func == RADIAL || _func == REPEATING_RADIAL)
	{
		vec2 V = fragTexCoord - _p;
		t = length(_v * V);
	}
	else if (_func == CONIC || _func == REPEATING_CONIC)
	{
		mat2 R = mat2(_v.x, -_v.y, _v.y, _v.x);
		vec2 V = R * (fragTexCoord - _p);
		t = 0.5 + atan(-V.x, V.y) / (2.0 * PI);
	}

	if (_func == REPEATING_LINEAR || _func == REPEATING_RADIAL || _func == REPEATING_CONIC)
	{
		float t0 = _stop_positions[0];
		float t1 = _stop_positions[_num_stops - 1];
		t = t0 + mod(t - t0, t1 - t0);
	}

	finalColor = fragColor * mix_stop_colors(t);
}
)";
// "Creation" by Danilo Guanabara, based on: https://www.shadertoy.com/view/XsXXDn
static const char* shader_frag_creation = RMLUI_SHADER_HEADER R"(
uniform float _value;
uniform vec2 _dimensions;

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

void main() {    
	float t = _value;
	vec3 c;
	float l;
	for (int i = 0; i < 3; i++) {
		vec2 p = fragTexCoord;
		vec2 uv = p;
		p -= .5;
		p.x *= _dimensions.x / _dimensions.y;
		float z = t + float(i) * .07;
		l = length(p);
		uv += p / l * (sin(z) + 1.) * abs(sin(l * 9. - z - z));
		c[i] = .01 / length(mod(uv, 1.) - .5);
	}
	finalColor = vec4(c / l, fragColor.a);
}
)";

static const char* shader_vert_passthrough = RMLUI_SHADER_HEADER R"(
in vec2 inPosition;
in vec2 inTexCoord0;

out vec2 fragTexCoord;

void main() {
	fragTexCoord = inTexCoord0;
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
)";
static const char* shader_frag_passthrough = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	finalColor = texture(_tex, fragTexCoord);
}
)";
static const char* shader_frag_color_matrix = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform mat4 _color_matrix;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	finalColor = _color_matrix * texColor;
}
)";
static const char* shader_frag_drop_shadow = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform vec2 _texCoordMin;
uniform vec2 _texCoordMax;
uniform vec4 _color;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	finalColor = texture(_tex, clamp(fragTexCoord, _texCoordMin, _texCoordMax)).a * _color;
}
)";
static const char* shader_frag_blend_mask = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform sampler2D _texMask;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	float maskAlpha = texture(_texMask, fragTexCoord).a;
	finalColor = texColor * maskAlpha;
}
)";

#define RMLUI_SHADER_BLUR_HEADER \
	RMLUI_SHADER_HEADER "\n#define BLUR_SIZE " RMLUI_STRINGIFY(BLUR_SIZE) "\n#define NUM_WEIGHTS " RMLUI_STRINGIFY(NUM_WEIGHTS)

static const char* shader_vert_blur = RMLUI_SHADER_BLUR_HEADER R"(
uniform vec2 _texelOffset;

in vec3 inPosition;
in vec2 inTexCoord0;

out vec2 fragTexCoord[BLUR_SIZE];

void main() {
	for(int i = 0; i < BLUR_SIZE; i++)
		fragTexCoord[i] = inTexCoord0 - float(i - NUM_WEIGHTS + 1) * _texelOffset;
    gl_Position = vec4(inPosition, 1.0);
}
)";
static const char* shader_frag_blur = RMLUI_SHADER_BLUR_HEADER R"(
uniform sampler2D _tex;
uniform float _weights[NUM_WEIGHTS];
uniform vec2 _texCoordMin;
uniform vec2 _texCoordMax;
uniform float _value;

in vec2 fragTexCoord[BLUR_SIZE];
out vec4 finalColor;

void main() {    
	vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
	for(int i = 0; i < BLUR_SIZE; i++)
		color += texture(_tex, clamp(fragTexCoord[i], _texCoordMin, _texCoordMax)) * _weights[abs(i - NUM_WEIGHTS + 1)];
	finalColor = color * _value;
}
)";

enum class ProgramId {
	None,
	Color,
	Texture,
	Gradient,
	Creation,
	Passthrough,
	ColorMatrix,
	DropShadow,
	BlendMask,
	Blur,
	Count,
};
enum class VertShaderId {
	Main,
	Passthrough,
	Blur,
	Count,
};
enum class FragShaderId {
	Color,
	Texture,
	Gradient,
	Creation,
	Passthrough,
	ColorMatrix,
	DropShadow,
	BlendMask,
	Blur,
	Count,
};
enum class UniformId {
	Translate,
	Transform,
	Tex,
	Value,
	Color,
	ColorMatrix,
	TexelOffset,
	TexCoordMin,
	TexCoordMax,
	Weights,
	TexMask,
	Func,
	P,
	V,
	StopColors,
	StopPositions,
	NumStops,
	Dimensions,
	Count,
};

namespace Gfx {

static const char* const program_uniform_names[(size_t)UniformId::Count] = {"_translate", "_transform", "_tex", "_value", "_color", "_color_matrix",
	"_texelOffset", "_texCoordMin", "_texCoordMax", "_weights[0]", "_texMask", "_func", "_p", "_v", "_stop_colors[0]", "_stop_positions[0]",
	"_num_stops", "_dimensions"};

enum class VertexAttribute { Position, Color0, TexCoord0, Count };
static const char* const vertex_attribute_names[(size_t)VertexAttribute::Count] = {"inPosition", "inColor0", "inTexCoord0"};

struct VertShaderDefinition {
	VertShaderId id;
	const char* name_str;
	const char* code_str;
};
struct FragShaderDefinition {
	FragShaderId id;
	const char* name_str;
	const char* code_str;
};
struct ProgramDefinition {
	ProgramId id;
	const char* name_str;
	VertShaderId vert_shader;
	FragShaderId frag_shader;
};

// clang-format off
static const VertShaderDefinition vert_shader_definitions[] = {
	{VertShaderId::Main,        "main",         shader_vert_main},
	{VertShaderId::Passthrough, "passthrough",  shader_vert_passthrough},
	{VertShaderId::Blur,        "blur",         shader_vert_blur},
};
static const FragShaderDefinition frag_shader_definitions[] = {
	{FragShaderId::Color,       "color",        shader_frag_color},
	{FragShaderId::Texture,     "texture",      shader_frag_texture},
	{FragShaderId::Gradient,    "gradient",     shader_frag_gradient},
	{FragShaderId::Creation,    "creation",     shader_frag_creation},
	{FragShaderId::Passthrough, "passthrough",  shader_frag_passthrough},
	{FragShaderId::ColorMatrix, "color_matrix", shader_frag_color_matrix},
	{FragShaderId::DropShadow,  "drop_shadow",  shader_frag_drop_shadow},
	{FragShaderId::BlendMask,   "blend_mask",   shader_frag_blend_mask},
	{FragShaderId::Blur,        "blur",         shader_frag_blur},
};
static const ProgramDefinition program_definitions[] = {
	{ProgramId::Color,       "color",        VertShaderId::Main,        FragShaderId::Color},
	{ProgramId::Texture,     "texture",      VertShaderId::Main,        FragShaderId::Texture},
	{ProgramId::Gradient,    "gradient",     VertShaderId::Main,        FragShaderId::Gradient},
	{ProgramId::Creation,    "creation",     VertShaderId::Main,        FragShaderId::Creation},
	{ProgramId::Passthrough, "passthrough",  VertShaderId::Passthrough, FragShaderId::Passthrough},
	{ProgramId::ColorMatrix, "color_matrix", VertShaderId::Passthrough, FragShaderId::ColorMatrix},
	{ProgramId::DropShadow,  "drop_shadow",  VertShaderId::Passthrough, FragShaderId::DropShadow},
	{ProgramId::BlendMask,   "blend_mask",   VertShaderId::Passthrough, FragShaderId::BlendMask},
	{ProgramId::Blur,        "blur",         VertShaderId::Blur,        FragShaderId::Blur},
};
// clang-format on

template <typename T, typename Enum>
class EnumArray {
public:
	const T& operator[](Enum id) const
	{
		RMLUI_ASSERT((size_t)id < (size_t)Enum::Count);
		return ids[size_t(id)];
	}
	T& operator[](Enum id)
	{
		RMLUI_ASSERT((size_t)id < (size_t)Enum::Count);
		return ids[size_t(id)];
	}
	auto begin() { return ids.begin(); }
	auto end() { return ids.end(); }

private:
	Rml::Array<T, (size_t)Enum::Count> ids = {};
};

using Programs = EnumArray<GLuint, ProgramId>;
using VertShaders = EnumArray<GLuint, VertShaderId>;
using FragShaders = EnumArray<GLuint, FragShaderId>;

class Uniforms {
public:
	GLint Get(ProgramId id, UniformId uniform) const
	{
		auto it = map.find(ToKey(id, uniform));
		if (it != map.end())
			return it->second;
		return -1;
	}
	void Insert(ProgramId id, UniformId uniform, GLint location) { map[ToKey(id, uniform)] = location; }

private:
	using Key = std::uint64_t;
	Key ToKey(ProgramId id, UniformId uniform) const { return (static_cast<Key>(id) << 32) | static_cast<Key>(uniform); }
	Rml::UnorderedMap<Key, GLint> map;
};

struct CompiledGeometryData {
	Rml::TextureHandle texture;
	GLuint vao;
	GLuint vbo;
	GLuint ibo;
	GLsizei draw_count;
};

struct FramebufferData {
	int width, height;
	GLuint framebuffer;
	GLuint color_tex_buffer;
	GLuint color_render_buffer;
	GLuint depth_stencil_buffer;
	bool owns_depth_stencil_buffer;
};

enum class FramebufferAttachment { None, Depth, DepthStencil };

static Programs programs;
static VertShaders vert_shaders;
static FragShaders frag_shaders;
static Uniforms uniforms;
static ProgramId active_program = ProgramId::None;
static Rml::Matrix4f projection;

static RenderInterface_GL3* render_interface = nullptr;

static void CheckGLError(const char* operation_name)
{
#ifdef RMLUI_DEBUG
	GLenum error_code = glGetError();
	if (error_code != GL_NO_ERROR)
	{
		static const Rml::Pair<GLenum, const char*> error_names[] = {{GL_INVALID_ENUM, "GL_INVALID_ENUM"}, {GL_INVALID_VALUE, "GL_INVALID_VALUE"},
			{GL_INVALID_OPERATION, "GL_INVALID_OPERATION"}, {GL_OUT_OF_MEMORY, "GL_OUT_OF_MEMORY"}};
		const char* error_str = "''";
		for (auto& err : error_names)
		{
			if (err.first == error_code)
			{
				error_str = err.second;
				break;
			}
		}
		Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL error during %s. Error code 0x%x (%s).", operation_name, error_code, error_str);
	}
#endif
	(void)operation_name;
}

static bool CreateShader(GLuint& out_shader_id, GLenum shader_type, const char* code_string)
{
	RMLUI_ASSERT(shader_type == GL_VERTEX_SHADER || shader_type == GL_FRAGMENT_SHADER);

	GLuint id = glCreateShader(shader_type);
	glShaderSource(id, 1, (const GLchar**)&code_string, NULL);
	glCompileShader(id);

	GLint status = 0;
	glGetShaderiv(id, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint info_log_length = 0;
		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &info_log_length);
		char* info_log_string = new char[info_log_length + 1];
		glGetShaderInfoLog(id, info_log_length, NULL, info_log_string);

		Rml::Log::Message(Rml::Log::LT_ERROR, "Compile failure in OpenGL shader: %s", info_log_string);
		delete[] info_log_string;
		glDeleteShader(id);
		return false;
	}

	CheckGLError("CreateShader");

	out_shader_id = id;
	return true;
}

static bool CreateProgram(GLuint& out_program, Uniforms& inout_uniform_map, ProgramId program_id, GLuint vertex_shader, GLuint fragment_shader)
{
	GLuint id = glCreateProgram();
	RMLUI_ASSERT(id);

	for (GLuint i = 0; i < (GLuint)VertexAttribute::Count; i++)
		glBindAttribLocation(id, i, vertex_attribute_names[i]);

	CheckGLError("BindAttribLocations");

	glAttachShader(id, vertex_shader);
	glAttachShader(id, fragment_shader);

	glLinkProgram(id);

	glDetachShader(id, vertex_shader);
	glDetachShader(id, fragment_shader);

	GLint status = 0;
	glGetProgramiv(id, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint info_log_length = 0;
		glGetProgramiv(id, GL_INFO_LOG_LENGTH, &info_log_length);
		char* info_log_string = new char[info_log_length + 1];
		glGetProgramInfoLog(id, info_log_length, NULL, info_log_string);

		Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL program linking failure: %s", info_log_string);
		delete[] info_log_string;
		glDeleteProgram(id);
		return false;
	}

	out_program = id;

	// Make a lookup table for the uniform locations.
	GLint num_active_uniforms = 0;
	glGetProgramiv(id, GL_ACTIVE_UNIFORMS, &num_active_uniforms);

	constexpr size_t name_size = 64;
	GLchar name_buf[name_size] = "";
	for (int unif = 0; unif < num_active_uniforms; ++unif)
	{
		GLint array_size = 0;
		GLenum type = 0;
		GLsizei actual_length = 0;
		glGetActiveUniform(id, unif, name_size, &actual_length, &array_size, &type, name_buf);
		GLint location = glGetUniformLocation(id, name_buf);

		// See if we have the name in our pre-defined name list.
		UniformId program_uniform = UniformId::Count;
		for (int i = 0; i < (int)UniformId::Count; i++)
		{
			const char* uniform_name = program_uniform_names[i];
			if (strcmp(name_buf, uniform_name) == 0)
			{
				program_uniform = (UniformId)i;
				break;
			}
		}

		if ((size_t)program_uniform < (size_t)UniformId::Count)
		{
			inout_uniform_map.Insert(program_id, program_uniform, location);
		}
		else
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL program uses unknown uniform '%s'.", name_buf);
			return false;
		}
	}

	CheckGLError("CreateProgram");

	return true;
}

static bool CreateFramebuffer(FramebufferData& out_fb, int width, int height, int samples, FramebufferAttachment attachment,
	GLuint shared_depth_stencil_buffer)
{
#ifdef RMLUI_PLATFORM_EMSCRIPTEN
	constexpr GLint wrap_mode = GL_CLAMP_TO_EDGE;
#else
	constexpr GLint wrap_mode = GL_CLAMP_TO_BORDER; // GL_REPEAT GL_MIRRORED_REPEAT GL_CLAMP_TO_EDGE
#endif

	constexpr GLenum color_format = GL_RGBA8;   // GL_RGBA8 GL_SRGB8_ALPHA8 GL_RGBA16F
	constexpr GLint min_mag_filter = GL_LINEAR; // GL_NEAREST
	const Rml::Colourf border_color(0.f, 0.f);

	GLuint framebuffer = 0;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	GLuint color_tex_buffer = 0;
	GLuint color_render_buffer = 0;
	if (samples > 0)
	{
		glGenRenderbuffers(1, &color_render_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, color_render_buffer);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, color_format, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_render_buffer);
	}
	else
	{
		glGenTextures(1, &color_tex_buffer);
		glBindTexture(GL_TEXTURE_2D, color_tex_buffer);
		glTexImage2D(GL_TEXTURE_2D, 0, color_format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_mag_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, min_mag_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);
#ifndef RMLUI_PLATFORM_EMSCRIPTEN
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &border_color[0]);
#endif

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_buffer, 0);
	}

	// Create depth/stencil buffer storage attachment.
	GLuint depth_stencil_buffer = 0;
	if (attachment != FramebufferAttachment::None)
	{
		if (shared_depth_stencil_buffer)
		{
			// Share depth/stencil buffer
			depth_stencil_buffer = shared_depth_stencil_buffer;
		}
		else
		{
			// Create new depth/stencil buffer
			glGenRenderbuffers(1, &depth_stencil_buffer);
			glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil_buffer);

			const GLenum internal_format = (attachment == FramebufferAttachment::DepthStencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24);
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internal_format, width, height);
		}

		const GLenum attachment_type = (attachment == FramebufferAttachment::DepthStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment_type, GL_RENDERBUFFER, depth_stencil_buffer);
	}

	const GLuint framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL framebuffer could not be generated. Error code %x.", framebuffer_status);
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	CheckGLError("CreateFramebuffer");

	out_fb = {};
	out_fb.width = width;
	out_fb.height = height;
	out_fb.framebuffer = framebuffer;
	out_fb.color_tex_buffer = color_tex_buffer;
	out_fb.color_render_buffer = color_render_buffer;
	out_fb.depth_stencil_buffer = depth_stencil_buffer;
	out_fb.owns_depth_stencil_buffer = !shared_depth_stencil_buffer;

	return true;
}

void DestroyFramebuffer(FramebufferData& fb)
{
	if (fb.framebuffer)
		glDeleteFramebuffers(1, &fb.framebuffer);
	if (fb.color_tex_buffer)
		glDeleteTextures(1, &fb.color_tex_buffer);
	if (fb.color_render_buffer)
		glDeleteRenderbuffers(1, &fb.color_render_buffer);
	if (fb.owns_depth_stencil_buffer && fb.depth_stencil_buffer)
		glDeleteRenderbuffers(1, &fb.depth_stencil_buffer);
	fb = {};
}

void BindTexture(const FramebufferData& fb)
{
	if (!fb.color_tex_buffer)
	{
		RMLUI_ERRORMSG("Only framebuffers with color textures can be bound as textures. This framebuffer probably uses multisampling which needs a "
					   "blit step first.");
	}

	glBindTexture(GL_TEXTURE_2D, fb.color_tex_buffer);
}

static bool CreateShaders()
{
	RMLUI_ASSERT(std::all_of(vert_shaders.begin(), vert_shaders.end(), [](auto&& value) { return value == 0; }));
	RMLUI_ASSERT(std::all_of(frag_shaders.begin(), frag_shaders.end(), [](auto&& value) { return value == 0; }));
	RMLUI_ASSERT(std::all_of(programs.begin(), programs.end(), [](auto&& value) { return value == 0; }));
	auto ReportError = [](const char* type, const char* name) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Could not create OpenGL %s: '%s'.", type, name);
		return false;
	};

	for (const VertShaderDefinition& def : vert_shader_definitions)
	{
		if (!CreateShader(vert_shaders[def.id], GL_VERTEX_SHADER, def.code_str))
			return ReportError("vertex shader", def.name_str);
	}

	for (const FragShaderDefinition& def : frag_shader_definitions)
	{
		if (!CreateShader(frag_shaders[def.id], GL_FRAGMENT_SHADER, def.code_str))
			return ReportError("fragment shader", def.name_str);
	}

	for (const ProgramDefinition& def : program_definitions)
	{
		if (!CreateProgram(programs[def.id], uniforms, def.id, vert_shaders[def.vert_shader], frag_shaders[def.frag_shader]))
			return ReportError("program", def.name_str);
	}

	glUseProgram(programs[ProgramId::BlendMask]);
	glUniform1i(uniforms.Get(ProgramId::BlendMask, UniformId::TexMask), 1);

	glUseProgram(0);

	return true;
}

static void DestroyShaders()
{
	for (auto&& id : programs)
		glDeleteProgram(id);

	for (auto&& id : vert_shaders)
		glDeleteShader(id);

	for (auto&& id : frag_shaders)
		glDeleteShader(id);

	vert_shaders = {};
	frag_shaders = {};
	programs = {};
}

void UseProgram(ProgramId program_id)
{
	if (active_program != program_id)
	{
		if (program_id != ProgramId::None)
			glUseProgram(programs[program_id]);
		active_program = program_id;
	}
}

} // namespace Gfx

RenderInterface_GL3::RenderInterface_GL3()
{
	RMLUI_ASSERT(!Gfx::render_interface);
	Gfx::render_interface = this;
}
RenderInterface_GL3::~RenderInterface_GL3()
{
	Gfx::render_interface = nullptr;
}

void RenderInterface_GL3::RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, const Rml::TextureHandle texture,
	const Rml::Vector2f& translation)
{
	Rml::CompiledGeometryHandle geometry = CompileGeometry(vertices, num_vertices, indices, num_indices, texture);

	if (geometry)
	{
		RenderCompiledGeometry(geometry, translation);
		ReleaseCompiledGeometry(geometry);
	}
}

Rml::CompiledGeometryHandle RenderInterface_GL3::CompileGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices,
	Rml::TextureHandle texture)
{
	constexpr GLenum draw_usage = GL_STATIC_DRAW;

	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint ibo = 0;

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ibo);
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Rml::Vertex) * num_vertices, (const void*)vertices, draw_usage);

	glEnableVertexAttribArray((GLuint)Gfx::VertexAttribute::Position);
	glVertexAttribPointer((GLuint)Gfx::VertexAttribute::Position, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex),
		(const GLvoid*)(offsetof(Rml::Vertex, position)));

	glEnableVertexAttribArray((GLuint)Gfx::VertexAttribute::Color0);
	glVertexAttribPointer((GLuint)Gfx::VertexAttribute::Color0, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex),
		(const GLvoid*)(offsetof(Rml::Vertex, colour)));

	glEnableVertexAttribArray((GLuint)Gfx::VertexAttribute::TexCoord0);
	glVertexAttribPointer((GLuint)Gfx::VertexAttribute::TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex),
		(const GLvoid*)(offsetof(Rml::Vertex, tex_coord)));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int) * num_indices, (const void*)indices, draw_usage);
	glBindVertexArray(0);

	Gfx::CheckGLError("CompileGeometry");

	Gfx::CompiledGeometryData* geometry = new Gfx::CompiledGeometryData;
	geometry->texture = texture;
	geometry->vao = vao;
	geometry->vbo = vbo;
	geometry->ibo = ibo;
	geometry->draw_count = num_indices;

	return (Rml::CompiledGeometryHandle)geometry;
}

void RenderInterface_GL3::RenderCompiledGeometry(Rml::CompiledGeometryHandle handle, const Rml::Vector2f& translation)
{
	Gfx::CompiledGeometryData* geometry = (Gfx::CompiledGeometryData*)handle;

	if (geometry->texture == TexturePostprocess)
	{
		// Do nothing.
	}
	else if (geometry->texture)
	{
		Gfx::UseProgram(ProgramId::Texture);
		SubmitTransformUniform(translation);
		if (geometry->texture != TextureIgnoreBinding)
			glBindTexture(GL_TEXTURE_2D, (GLuint)geometry->texture);
	}
	else
	{
		Gfx::UseProgram(ProgramId::Color);
		SubmitTransformUniform(translation);
	}

	glBindVertexArray(geometry->vao);
	glDrawElements(GL_TRIANGLES, geometry->draw_count, GL_UNSIGNED_INT, (const GLvoid*)0);

	Gfx::CheckGLError("RenderCompiledGeometry");
}

void RenderInterface_GL3::ReleaseCompiledGeometry(Rml::CompiledGeometryHandle handle)
{
	Gfx::CompiledGeometryData* geometry = (Gfx::CompiledGeometryData*)handle;

	glDeleteVertexArrays(1, &geometry->vao);
	glDeleteBuffers(1, &geometry->vbo);
	glDeleteBuffers(1, &geometry->ibo);

	delete geometry;
}

/// Flip vertical axis of the rectangle, and move its origin to the vertically opposite side of the viewport.
/// @note Changes coordinate system from RmlUi to OpenGL, or equivalently in reverse.
/// @note The Rectangle::Top and Rectangle::Bottom members will have reverse meaning in the returned rectangle.
static Rml::Rectanglei VerticallyFlipped(Rml::Rectanglei rect, int viewport_height)
{
	RMLUI_ASSERT(rect.Valid());
	Rml::Rectanglei flipped_rect = rect;
	flipped_rect.p0.y = viewport_height - rect.p1.y;
	flipped_rect.p1.y = viewport_height - rect.p0.y;
	return flipped_rect;
}

void RenderInterface_GL3::SetScissor(Rml::Rectanglei region, bool vertically_flip)
{
	if (region.Valid() != scissor_state.Valid())
	{
		if (region.Valid())
			glEnable(GL_SCISSOR_TEST);
		else
			glDisable(GL_SCISSOR_TEST);
	}

	if (region.Valid() && vertically_flip)
		region = VerticallyFlipped(region, viewport_height);

	if (region.Valid() && region != scissor_state)
		glScissor(region.Left(), viewport_height - region.Bottom(), region.Width(), region.Height());

	Gfx::CheckGLError("SetScissorRegion");
	scissor_state = region;
}
void RenderInterface_GL3::DisableScissor()
{
	SetScissor(Rml::Rectanglei::CreateInvalid());
}

// Set to byte packing, or the compiler will expand our struct, which means it won't read correctly from file
#pragma pack(1)
struct TGAHeader {
	char idLength;
	char colourMapType;
	char dataType;
	short int colourMapOrigin;
	short int colourMapLength;
	char colourMapDepth;
	short int xOrigin;
	short int yOrigin;
	short int width;
	short int height;
	char bitsPerPixel;
	char imageDescriptor;
};
// Restore packing
#pragma pack()

bool RenderInterface_GL3::LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	Rml::FileInterface* file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
	{
		return false;
	}

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	if (buffer_size <= sizeof(TGAHeader))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Texture file size is smaller than TGAHeader, file is not a valid TGA image.");
		file_interface->Close(file_handle);
		return false;
	}

	using Rml::byte;
	byte* buffer = new byte[buffer_size];
	file_interface->Read(buffer, buffer_size, file_handle);
	file_interface->Close(file_handle);

	TGAHeader header;
	memcpy(&header, buffer, sizeof(TGAHeader));

	int color_mode = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4; // We always make 32bit textures

	if (header.dataType != 2)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Only 24/32bit uncompressed TGAs are supported.");
		delete[] buffer;
		return false;
	}

	// Ensure we have at least 3 colors
	if (color_mode < 3)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Only 24 and 32bit textures are supported.");
		delete[] buffer;
		return false;
	}

	const byte* image_src = buffer + sizeof(TGAHeader);
	byte* image_dest = new byte[image_size];

	// Targa is BGR, swap to RGB and flip Y axis
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : (header.height - y - 1) * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			image_dest[write_index] = image_src[read_index + 2];
			image_dest[write_index + 1] = image_src[read_index + 1];
			image_dest[write_index + 2] = image_src[read_index];
			if (color_mode == 4)
				image_dest[write_index + 3] = image_src[read_index + 3];
			else
				image_dest[write_index + 3] = 255;

			write_index += 4;
			read_index += color_mode;
		}
	}

	texture_dimensions.x = header.width;
	texture_dimensions.y = header.height;

	bool success = GenerateTexture(texture_handle, image_dest, texture_dimensions);

	delete[] image_dest;
	delete[] buffer;

	return success;
}

bool RenderInterface_GL3::GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions)
{
	GLuint texture_id = 0;
	glGenTextures(1, &texture_id);
	if (texture_id == 0)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to generate texture.");
		return false;
	}

#if RMLUI_PREMULTIPLIED_ALPHA
	using Rml::byte;
	Rml::UniquePtr<byte[]> source_premultiplied;
	if (source)
	{
		const size_t num_bytes = source_dimensions.x * source_dimensions.y * 4;
		source_premultiplied = Rml::UniquePtr<byte[]>(new byte[num_bytes]);

		for (size_t i = 0; i < num_bytes; i += 4)
		{
			const byte alpha = source[i + 3];
			for (size_t j = 0; j < 3; j++)
				source_premultiplied[i + j] = byte((int(source[i + j]) * int(alpha)) / 255);
			source_premultiplied[i + 3] = alpha;
		}

		source = source_premultiplied.get();
	}
#endif

	glBindTexture(GL_TEXTURE_2D, texture_id);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, source_dimensions.x, source_dimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, source);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	texture_handle = (Rml::TextureHandle)texture_id;

	return true;
}

bool RenderInterface_GL3::GenerateRenderTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i dimensions)
{
	return GenerateTexture(texture_handle, nullptr, dimensions);
}

void RenderInterface_GL3::DrawFullscreenQuad(Rml::Vector2f uv_offset, Rml::Vector2f uv_scaling)
{
	// Draw a fullscreen quad.
	Rml::Vertex vertices[4];
	int indices[6];
	Rml::GeometryUtilities::GenerateQuad(vertices, indices, Rml::Vector2f(-1), Rml::Vector2f(2), {});
	if (uv_offset != Rml::Vector2f() || uv_scaling != Rml::Vector2f(1.f))
	{
		for (Rml::Vertex& vertex : vertices)
			vertex.tex_coord = (vertex.tex_coord * uv_scaling) + uv_offset;
	}

	RenderGeometry(vertices, 4, indices, 6, RenderInterface_GL3::TexturePostprocess, {});
}

void RenderInterface_GL3::ReleaseTexture(Rml::TextureHandle texture_handle)
{
	glDeleteTextures(1, (GLuint*)&texture_handle);
}

void RenderInterface_GL3::SetTransform(const Rml::Matrix4f* new_transform)
{
	transform = Gfx::projection * (new_transform ? *new_transform : Rml::Matrix4f::Identity());
	program_transform_dirty.set();
}

class RenderState {
public:
	// Push a new layer. All references to previously retrieved layers are invalidated.
	void PushLayer()
	{
		RMLUI_ASSERT(layers_size <= (int)fb_layers.size());

		if (layers_size == (int)fb_layers.size())
		{
			constexpr int num_samples = 2;
			// All framebuffers should share a single stencil buffer.
			GLuint shared_depth_stencil = (fb_layers.empty() ? 0 : fb_layers.front().depth_stencil_buffer);

			fb_layers.push_back(Gfx::FramebufferData{});
			Gfx::CreateFramebuffer(fb_layers.back(), width, height, num_samples, Gfx::FramebufferAttachment::DepthStencil, shared_depth_stencil);
		}

		layers_size += 1;
	}

	// Push a clone of the active layer. All references to previously retrieved layers are invalidated.
	void PushLayerClone()
	{
		RMLUI_ASSERT(layers_size > 0);
		fb_layers.insert(fb_layers.begin() + layers_size, Gfx::FramebufferData{fb_layers[layers_size - 1]});
		layers_size += 1;
	}

	// Pop the top layer. All references to previously retrieved layers are invalidated.
	void PopLayer()
	{
		RMLUI_ASSERT(layers_size > 0);
		layers_size -= 1;

		// Only cloned framebuffers are removed. Other framebuffers remain for later re-use.
		if (IsCloneOfBelow(layers_size))
			fb_layers.erase(fb_layers.begin() + layers_size);
	}

	const Gfx::FramebufferData& GetTopLayer() const
	{
		RMLUI_ASSERT(layers_size > 0);
		return fb_layers[layers_size - 1];
	}

	const Gfx::FramebufferData& GetPostprocessPrimary() { return EnsureFramebufferPostprocess(0); }
	const Gfx::FramebufferData& GetPostprocessSecondary() { return EnsureFramebufferPostprocess(1); }
	const Gfx::FramebufferData& GetPostprocessTertiary() { return EnsureFramebufferPostprocess(2); }
	const Gfx::FramebufferData& GetMask() { return EnsureFramebufferPostprocess(3); }

	void SwapPostprocessPrimarySecondary() { std::swap(fb_postprocess[0], fb_postprocess[1]); }

	void BeginFrame(int new_width, int new_height)
	{
		RMLUI_ASSERT(layers_size == 0);

		if (new_width != width || new_height != height)
		{
			width = new_width;
			height = new_height;

			DestroyFramebuffers();
		}

		PushLayer();
	}

	void EndFrame()
	{
		RMLUI_ASSERT(layers_size == 1);
		PopLayer();
	}

	void Shutdown() { DestroyFramebuffers(); }

private:
	void DestroyFramebuffers()
	{
		RMLUI_ASSERTMSG(layers_size == 0, "Do not call this during frame rendering, that is, between BeginFrame() and EndFrame().");

		for (Gfx::FramebufferData& fb : fb_layers)
			Gfx::DestroyFramebuffer(fb);

		fb_layers.clear();

		for (Gfx::FramebufferData& fb : fb_postprocess)
			Gfx::DestroyFramebuffer(fb);
	}

	bool IsCloneOfBelow(int layer_index) const
	{
		const bool result =
			(layer_index >= 1 && layer_index < (int)fb_layers.size() && fb_layers[layer_index].framebuffer == fb_layers[layer_index - 1].framebuffer);
		return result;
	}

	const Gfx::FramebufferData& EnsureFramebufferPostprocess(int index)
	{
		Gfx::FramebufferData& fb = fb_postprocess[index];
		if (!fb.framebuffer)
			Gfx::CreateFramebuffer(fb, width, height, 0, Gfx::FramebufferAttachment::None, 0);
		return fb;
	}

	int width = 0, height = 0;

	// The number of active layers is manually tracked since we re-use the framebuffers stored in the fb_layers stack.
	int layers_size = 0;

	Rml::Vector<Gfx::FramebufferData> fb_layers;
	Rml::Array<Gfx::FramebufferData, 4> fb_postprocess = {};
};

static RenderState render_state;

static inline Rml::Colourf ToPremultipliedAlpha(Rml::Colourb c0)
{
	Rml::Colourf result;
	result.alpha = (1.f / 255.f) * float(c0.alpha);
	result.red = (1.f / 255.f) * float(c0.red) * result.alpha;
	result.green = (1.f / 255.f) * float(c0.green) * result.alpha;
	result.blue = (1.f / 255.f) * float(c0.blue) * result.alpha;
	return result;
}

enum class CompiledShaderType { Invalid = 0, Gradient, Creation };
struct CompiledShader {
	CompiledShaderType type;

	// Gradient
	ShaderGradientFunction gradient_function;
	Rml::Vector2f p;
	Rml::Vector2f v;
	Rml::Vector<float> stop_positions;
	Rml::Vector<Rml::Colourf> stop_colors;

	// Shader
	Rml::Vector2f dimensions;
};

Rml::CompiledShaderHandle RenderInterface_GL3::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto ApplyColorStopList = [](CompiledShader& shader, const Rml::Dictionary& shader_parameters) {
		auto it = shader_parameters.find("color_stop_list");
		RMLUI_ASSERT(it != shader_parameters.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST);
		const Rml::ColorStopList& color_stop_list = it->second.GetReference<Rml::ColorStopList>();
		const int num_stops = Rml::Math::Min((int)color_stop_list.size(), MAX_NUM_STOPS);

		shader.stop_positions.resize(num_stops);
		shader.stop_colors.resize(num_stops);
		for (int i = 0; i < num_stops; i++)
		{
			const Rml::ColorStop& stop = color_stop_list[i];
			RMLUI_ASSERT(stop.position.unit == Rml::Unit::NUMBER);
			shader.stop_positions[i] = stop.position.number;
			shader.stop_colors[i] = ToPremultipliedAlpha(stop.color);
		}
	};

	CompiledShader shader = {};

	if (name == "linear-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (repeating ? ShaderGradientFunction::RepeatingLinear : ShaderGradientFunction::Linear);
		shader.p = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
		shader.v = Rml::Get(parameters, "p1", Rml::Vector2f(0.f)) - shader.p;
		ApplyColorStopList(shader, parameters);
	}
	else if (name == "radial-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (repeating ? ShaderGradientFunction::RepeatingRadial : ShaderGradientFunction::Radial);
		shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
		shader.v = Rml::Vector2f(1.f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
		ApplyColorStopList(shader, parameters);
	}
	else if (name == "conic-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (repeating ? ShaderGradientFunction::RepeatingConic : ShaderGradientFunction::Conic);
		shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
		const float angle = Rml::Get(parameters, "angle", 0.f);
		shader.v = {Rml::Math::Cos(angle), Rml::Math::Sin(angle)};
		ApplyColorStopList(shader, parameters);
	}
	else if (name == "shader")
	{
		const Rml::String value = Rml::Get(parameters, "value", Rml::String());
		if (value == "creation")
		{
			shader.type = CompiledShaderType::Creation;
			shader.dimensions = Rml::Get(parameters, "dimensions", Rml::Vector2f(0.f));
		}
	}

	if (shader.type != CompiledShaderType::Invalid)
		return reinterpret_cast<Rml::CompiledShaderHandle>(new CompiledShader(std::move(shader)));

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", name.c_str());
	return {};
}

static void SetTexCoordLimits(ProgramId program, Rml::Rectanglei rectangle_flipped, Rml::Vector2i framebuffer_size)
{
#ifdef RMLUI_DEBUG
	GLint gl_id = {};
	glGetIntegerv(GL_CURRENT_PROGRAM, &gl_id);
	RMLUI_ASSERTMSG((GLuint)gl_id == Gfx::programs[program], "Passed-in program must be currently active.");
#endif

	// Offset by half-texel values so that texture lookups are clamped to fragment centers, thereby avoiding color
	// bleeding from neighboring texels due to bilinear interpolation.
	const Rml::Vector2f min = (Rml::Vector2f(rectangle_flipped.p0) + Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);
	const Rml::Vector2f max = (Rml::Vector2f(rectangle_flipped.p1) - Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);

	glUniform2f(Gfx::uniforms.Get(program, UniformId::TexCoordMin), min.x, min.y);
	glUniform2f(Gfx::uniforms.Get(program, UniformId::TexCoordMax), max.x, max.y);
}

static void SigmaToParameters(const float desired_sigma, int& out_pass_level, float& out_sigma)
{
	constexpr int max_num_passes = 10;
	static_assert(max_num_passes < 31, "");
	constexpr float max_single_pass_sigma = 3.0f;
	out_pass_level = Rml::Math::Clamp(Rml::Math::Log2(int(desired_sigma * (2.f / max_single_pass_sigma))), 0, max_num_passes);
	out_sigma = Rml::Math::Clamp(desired_sigma / float(1 << out_pass_level), 0.0f, max_single_pass_sigma);
}

static void SetBlurWeights(float sigma)
{
	float weights[num_weights];
	float normalization = 0.0f;
	for (int i = 0; i < num_weights; i++)
	{
		if (Rml::Math::AbsoluteValue(sigma) < 0.1f)
			weights[i] = float(i == 0);
		else
			weights[i] = Rml::Math::Exp(-float(i * i) / (2.0f * sigma * sigma)) / (Rml::Math::SquareRoot(2.f * Rml::Math::RMLUI_PI) * sigma);

		normalization += (i == 0 ? 1.f : 2.0f) * weights[i];
	}
	for (int i = 0; i < num_weights; i++)
		weights[i] /= normalization;

	glUniform1fv(Gfx::uniforms.Get(ProgramId::Blur, UniformId::Weights), (GLsizei)num_weights, &weights[0]);
}

void RenderInterface_GL3::RenderBlurPass(const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp)
{
	auto SetTexelOffset = [](Rml::Vector2f blur_direction, int texture_dimension) {
		const Rml::Vector2f texel_offset = blur_direction * (1.0f / float(texture_dimension));
		glUniform2f(Gfx::uniforms.Get(ProgramId::Blur, UniformId::TexelOffset), texel_offset.x, texel_offset.y);
	};

	// Vertical
	Gfx::BindTexture(source_destination);
	glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);

	SetTexelOffset({0.f, 1.f}, source_destination.height);
	DrawFullscreenQuad();

	// Horizontal
	Gfx::BindTexture(temp);
	glBindFramebuffer(GL_FRAMEBUFFER, source_destination.framebuffer);

	SetTexelOffset({1.f, 0.f}, temp.width);
	DrawFullscreenQuad();
}

void RenderInterface_GL3::RenderBlur(float sigma, const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp,
	const Rml::Rectanglei window_flipped)
{
	RMLUI_ASSERT(&source_destination != &temp && source_destination.width == temp.width && source_destination.height == temp.height);
	RMLUI_ASSERT(window_flipped.Valid());

	int pass_level = 0;
	SigmaToParameters(sigma, pass_level, sigma);

	const Rml::Rectanglei original_scissor = scissor_state;

#if 0
	// Debug coloring
	DisableScissor();
	glClearColor(1, 0, 0, 1);
	glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0, 0, 0, 0);
#endif

	// Begin by downscale so that the blur pass can be done at a reduced resolution for large sigma.
	Rml::Rectanglei scissor = window_flipped;

	Gfx::UseProgram(ProgramId::Passthrough);
	SetScissor(scissor, true);

	// Downscale by iterative half-scaling with bilinear filtering, to reduce aliasing.
	glViewport(0, 0, source_destination.width / 2, source_destination.height / 2);

	// Scale UVs if we have even dimensions, such that texture fetches align perfectly between texels, thereby producing a 50% blend of
	// neighboring texels.
	const Rml::Vector2f uv_scaling = {(source_destination.width % 2 == 1) ? (1.f - 1.f / float(source_destination.width)) : 1.f,
		(source_destination.height % 2 == 1) ? (1.f - 1.f / float(source_destination.height)) : 1.f};

	// Move the texture data to the temp buffer if the last downscaling end up at the source_destination buffer.
	const bool transfer_to_temp_buffer = (pass_level % 2 == 0);

	for (int i = 0; i < pass_level; i++)
	{
		scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
		scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
		const bool from_source = (i % 2 == 0);
		Gfx::BindTexture(from_source ? source_destination : temp);
		glBindFramebuffer(GL_FRAMEBUFFER, (from_source ? temp : source_destination).framebuffer);
		SetScissor(scissor, true);

		DrawFullscreenQuad({}, uv_scaling);
	}

	glViewport(0, 0, source_destination.width, source_destination.height);

	if (transfer_to_temp_buffer)
	{
		Gfx::BindTexture(source_destination);
		glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);
		DrawFullscreenQuad();
	}

#if 0
	// Debug coloring
	glBindFramebuffer(GL_FRAMEBUFFER, source_destination.framebuffer);
	Gfx::render_interface->EnableScissorRegion(false);
	glClearColor(0, 0, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	Gfx::render_interface->EnableScissorRegion(true);
	glClearColor(0, 0, 0, 0);
#endif

	// Set up uniforms.
	Gfx::UseProgram(ProgramId::Blur);
	SetBlurWeights(sigma);
	SetTexCoordLimits(ProgramId::Blur, scissor, {source_destination.width, source_destination.height});
	const float blending_magnitude = 1.f;
	glUniform1f(Gfx::uniforms.Get(ProgramId::Blur, UniformId::Value), blending_magnitude);

	// Now do the actual render pass.
	RenderBlurPass(temp, source_destination);

	// Blit the blurred image to the scissor region with upscaling.
	SetScissor(window_flipped, true);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, temp.framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, source_destination.framebuffer);

	const Rml::Vector2i src_min = scissor.p0;
	const Rml::Vector2i src_max = scissor.p1;
	const Rml::Vector2i dst_min = window_flipped.p0;
	const Rml::Vector2i dst_max = window_flipped.p1;
	glBlitFramebuffer(src_min.x, src_min.y, src_max.x, src_max.y, dst_min.x, dst_min.y, dst_max.x, dst_max.y, GL_COLOR_BUFFER_BIT, GL_LINEAR);

	// The above upscale blit might be jittery at low resolutions (large pass levels). This is especially noticable when moving an element with
	// backdrop blur around or when trying to click/hover an element within a blurred region since it may be rendered at an offset. For more stable
	// and accurate rendering we next upscale the blur image by an exact power-of-two. However, this may not fill the edges completely so we need to
	// do the above first. Note that this strategy may sometimes result in visible seams. Alternatively, we could try to enlargen the window to the
	// next power-of-two size and then downsample and blur that.
	const Rml::Vector2i target_min = src_min * (1 << pass_level);
	const Rml::Vector2i target_max = src_max * (1 << pass_level);
	if (target_min != dst_min || target_max != dst_max)
	{
		glBlitFramebuffer(src_min.x, src_min.y, src_max.x, src_max.y, target_min.x, target_min.y, target_max.x, target_max.y, GL_COLOR_BUFFER_BIT,
			GL_LINEAR);
	}

	// Restore render state.
	SetScissor(original_scissor);

	Gfx::CheckGLError("Blur");
}

void RenderInterface_GL3::SetCustomShader(Rml::CompiledShaderHandle shader_handle)
{
	const CompiledShader& shader = *reinterpret_cast<CompiledShader*>(shader_handle);
	const CompiledShaderType type = shader.type;

	switch (type)
	{
	case CompiledShaderType::Gradient:
	{
		RMLUI_ASSERT(shader.stop_positions.size() == shader.stop_colors.size());
		const int num_stops = (int)shader.stop_positions.size();

		Gfx::UseProgram(ProgramId::Gradient);
		glUniform1i(Gfx::uniforms.Get(ProgramId::Gradient, UniformId::Func), static_cast<int>(shader.gradient_function));
		glUniform2fv(Gfx::uniforms.Get(ProgramId::Gradient, UniformId::P), 1, &shader.p.x);
		glUniform2fv(Gfx::uniforms.Get(ProgramId::Gradient, UniformId::V), 1, &shader.v.x);
		glUniform1i(Gfx::uniforms.Get(ProgramId::Gradient, UniformId::NumStops), num_stops);
		glUniform1fv(Gfx::uniforms.Get(ProgramId::Gradient, UniformId::StopPositions), num_stops, shader.stop_positions.data());
		glUniform4fv(Gfx::uniforms.Get(ProgramId::Gradient, UniformId::StopColors), num_stops, shader.stop_colors[0]);
	}
	break;
	case CompiledShaderType::Creation:
	{
		const double time = Rml::GetSystemInterface()->GetElapsedTime();

		Gfx::UseProgram(ProgramId::Creation);
		glUniform1f(Gfx::uniforms.Get(ProgramId::Creation, UniformId::Value), (float)time);
		glUniform2fv(Gfx::uniforms.Get(ProgramId::Creation, UniformId::Dimensions), 1, &shader.dimensions.x);
	}
	break;
	case CompiledShaderType::Invalid:
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render shader %d.", (int)type);
	}
	break;
	}

	Gfx::CheckGLError("AttachShader");
}

void RenderInterface_GL3::ReleaseCompiledShader(Rml::CompiledShaderHandle effect_handle)
{
	delete reinterpret_cast<CompiledShader*>(effect_handle);
}

enum class FilterType { Invalid = 0, Passthrough, ColorMatrix, Blur, DropShadow };
struct CompiledFilter {
	FilterType type;

	// Passthrough
	float blend_factor;

	// ColorMatrix
	Rml::Matrix4f color_matrix;

	// Blur
	float sigma;

	// Drop shadow
	Rml::Vector2f offset;
	Rml::Colourb color;
};

Rml::CompiledFilterHandle RenderInterface_GL3::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters)
{
	CompiledFilter filter = {};

	if (name == "blur")
	{
		filter.type = FilterType::Blur;
		filter.sigma = 0.5f * Rml::Get(parameters, "radius", 0.0f);
	}
	else if (name == "drop-shadow")
	{
		filter.type = FilterType::DropShadow;
		filter.sigma = Rml::Get(parameters, "sigma", 0.f);
		filter.color = Rml::Get(parameters, "color", Rml::Colourb());
		filter.offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
	}
	else if (name == "opacity")
	{
		filter.type = FilterType::Passthrough;
		filter.blend_factor = Rml::Get(parameters, "value", 1.0f);
	}
	else if (name == "brightness")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
	}
	else if (name == "contrast")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float grayness = 0.5f - 0.5f * value;
		filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
		filter.color_matrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
	}
	else if (name == "invert")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
		const float inverted = 1.f - 2.f * value;
		filter.color_matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
		filter.color_matrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
	}
	else if (name == "grayscale")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{gray.x + rev_value, gray.y,             gray.z,             0.f},
			{gray.x,             gray.y + rev_value, gray.z,             0.f},
			{gray.x,             gray.y,             gray.z + rev_value, 0.f},
			{0.f,                0.f,                0.f,                1.f}
		);
		// clang-format on
	}
	else if (name == "sepia")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f r_mix = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
		const Rml::Vector3f g_mix = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
		const Rml::Vector3f b_mix = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{r_mix.x + rev_value, r_mix.y,             r_mix.z,             0.f},
			{g_mix.x,             g_mix.y + rev_value, g_mix.z,             0.f},
			{b_mix.x,             b_mix.y,             b_mix.z + rev_value, 0.f},
			{0.f,                 0.f,                 0.f,                 1.f}
		);
		// clang-format on
	}
	else if (name == "hue-rotate")
	{
		// Hue-rotation and saturation values based on: https://www.w3.org/TR/filter-effects-1/#attr-valuedef-type-huerotate
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float s = Rml::Math::Sin(value);
		const float c = Rml::Math::Cos(value);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * c - 0.213f * s,  0.715f - 0.715f * c - 0.715f * s,  0.072f - 0.072f * c + 0.928f * s,  0.f},
			{0.213f - 0.213f * c + 0.143f * s,  0.715f + 0.285f * c + 0.140f * s,  0.072f - 0.072f * c - 0.283f * s,  0.f},
			{0.213f - 0.213f * c - 0.787f * s,  0.715f - 0.715f * c + 0.715f * s,  0.072f + 0.928f * c + 0.072f * s,  0.f},
			{0.f,                               0.f,                               0.f,                               1.f}
		);
		// clang-format on
	}
	else if (name == "saturate")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * value,  0.715f - 0.715f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f + 0.285f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f - 0.715f * value,  0.072f + 0.928f * value,  0.f},
			{0.f,                      0.f,                      0.f,                      1.f}
		);
		// clang-format on
	}

	if (filter.type != FilterType::Invalid)
		return reinterpret_cast<Rml::CompiledFilterHandle>(new CompiledFilter(std::move(filter)));

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", name.c_str());
	return {};
}

void RenderInterface_GL3::ReleaseCompiledFilter(Rml::CompiledFilterHandle filter)
{
	delete reinterpret_cast<CompiledFilter*>(filter);
}

void RenderInterface_GL3::RenderFilters(const Rml::FilterHandleList& filter_list)
{
	for (const Rml::CompiledFilterHandle filter_handle : filter_list)
	{
		const CompiledFilter& filter = *reinterpret_cast<const CompiledFilter*>(filter_handle);
		const FilterType type = filter.type;

		switch (type)
		{
		case FilterType::Blur:
		{
			glDisable(GL_BLEND);

			const Gfx::FramebufferData& source_destination = render_state.GetPostprocessPrimary();
			const Gfx::FramebufferData& temp = render_state.GetPostprocessSecondary();

			const Rml::Rectanglei window_flipped = VerticallyFlipped(scissor_state, viewport_height);
			RenderBlur(filter.sigma, source_destination, temp, window_flipped);

			// Restore state
			glEnable(GL_BLEND);
		}
		break;
		case FilterType::DropShadow:
		{
			Gfx::UseProgram(ProgramId::DropShadow);
			glDisable(GL_BLEND);

			Rml::Colourf color = ToPremultipliedAlpha(filter.color);
			glUniform4fv(Gfx::uniforms.Get(ProgramId::DropShadow, UniformId::Color), 1, &color[0]);

			const Gfx::FramebufferData& primary = render_state.GetPostprocessPrimary();
			const Gfx::FramebufferData& secondary = render_state.GetPostprocessSecondary();
			Gfx::BindTexture(primary);
			glBindFramebuffer(GL_FRAMEBUFFER, secondary.framebuffer);

			const Rml::Rectanglei window_flipped = VerticallyFlipped(scissor_state, viewport_height);
			SetTexCoordLimits(ProgramId::DropShadow, window_flipped, {primary.width, primary.height});

			const Rml::Vector2f uv_offset = filter.offset / Rml::Vector2f(-(float)viewport_width, (float)viewport_height);
			DrawFullscreenQuad(uv_offset);

			if (filter.sigma >= 0.5f)
			{
				const Gfx::FramebufferData& tertiary = render_state.GetPostprocessTertiary();
				RenderBlur(filter.sigma, secondary, tertiary, window_flipped);
			}

			Gfx::UseProgram(ProgramId::Passthrough);
			Gfx::BindTexture(primary);
			glEnable(GL_BLEND);
			DrawFullscreenQuad();

			render_state.SwapPostprocessPrimarySecondary();
		}
		break;
		case FilterType::Passthrough:
		{
			Gfx::UseProgram(ProgramId::Passthrough);
			glBlendFunc(GL_CONSTANT_ALPHA, GL_ZERO);
			glBlendColor(0.0f, 0.0f, 0.0f, filter.blend_factor);

			const Gfx::FramebufferData& source = render_state.GetPostprocessPrimary();
			const Gfx::FramebufferData& destination = render_state.GetPostprocessSecondary();
			Gfx::BindTexture(source);
			glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);

			DrawFullscreenQuad();

			render_state.SwapPostprocessPrimarySecondary();
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		}
		break;
		case FilterType::ColorMatrix:
		{
			Gfx::UseProgram(ProgramId::ColorMatrix);
			glDisable(GL_BLEND);

			const GLint uniform_location = Gfx::uniforms.Get(ProgramId::ColorMatrix, UniformId::ColorMatrix);
			constexpr bool transpose = std::is_same<decltype(filter.color_matrix), Rml::RowMajorMatrix4f>::value;
			glUniformMatrix4fv(uniform_location, 1, transpose, filter.color_matrix.data());

			const Gfx::FramebufferData& source = render_state.GetPostprocessPrimary();
			const Gfx::FramebufferData& destination = render_state.GetPostprocessSecondary();
			Gfx::BindTexture(source);
			glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);

			DrawFullscreenQuad();

			render_state.SwapPostprocessPrimarySecondary();
			glEnable(GL_BLEND);
		}
		break;
		case FilterType::Invalid:
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render filter %d.", (int)type);
		}
		break;
		}
	}

	Gfx::CheckGLError("RenderFilter");
}

void RenderInterface_GL3::PopLayer(Rml::RenderTarget render_target, Rml::BlendMode blend_mode, const Rml::FilterHandleList& filter_list,
	Rml::TextureHandle render_texture_target)
{
	using Rml::BlendMode;
	using Rml::RenderTarget;
	RMLUI_ASSERT(!(has_mask && render_target == RenderTarget::MaskImage));

	{
		// Blit stack to filter rendering buffer. Do this regardless of whether we actually have any filters to be applied, because we need to resolve
		// the multi-sampled framebuffer in any case.
		// @performance If we have BlendMode::Replace and no filters or mask then we can just blit directly to the destination. This is particularly
		// common when compositing to the mask layer. Alternatively, make the mask layer into R8 texture, then we need to do this step first anyway.
		const Gfx::FramebufferData& source = render_state.GetTopLayer();
		const Gfx::FramebufferData& destination = render_state.GetPostprocessPrimary();
		glBindFramebuffer(GL_READ_FRAMEBUFFER, source.framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);

		// Any active scissor state will restrict the size of the blit region.
		glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, destination.width, destination.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	// Render the filters, the PostprocessPrimary framebuffer is used for both input and output.
	RenderFilters(filter_list);

	// Pop the active layer, thereby activating the beneath layer.
	render_state.PopLayer();

	switch (render_target)
	{
	case RenderTarget::Layer:
	case RenderTarget::MaskImage:
	{
		// Blit filter back to stack. Apply any mask if active.
		const Gfx::FramebufferData& source = render_state.GetPostprocessPrimary();
		const Gfx::FramebufferData& destination = (render_target == RenderTarget::Layer ? render_state.GetTopLayer() : render_state.GetMask());

		glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);
		Gfx::BindTexture(source);
		if (has_mask)
		{
			has_mask = false;
			Gfx::UseProgram(ProgramId::BlendMask);

			glActiveTexture(GL_TEXTURE1);
			Gfx::BindTexture(render_state.GetMask());
			glActiveTexture(GL_TEXTURE0);
		}
		else
		{
			Gfx::UseProgram(ProgramId::Passthrough);
		}

		if (blend_mode == BlendMode::Replace)
			glDisable(GL_BLEND);

		DrawFullscreenQuad();

		if (blend_mode == BlendMode::Replace)
			glEnable(GL_BLEND);

		if (render_target == RenderTarget::MaskImage)
			has_mask = true;
	}
	break;
	case RenderTarget::RenderTexture:
	{
		RMLUI_ASSERT(render_texture_target);
		RMLUI_ASSERT(scissor_state.Valid());
		const Rml::Rectanglei initial_scissor_state = scissor_state;
		DisableScissor();

		const Gfx::FramebufferData& source = render_state.GetPostprocessPrimary();
		const Gfx::FramebufferData& destination = render_state.GetPostprocessSecondary();
		glBindFramebuffer(GL_READ_FRAMEBUFFER, source.framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);

		Rml::Rectanglei bounds = initial_scissor_state;

		// Flip the image vertically, as that convention is used for textures, and move to origin.
		glBlitFramebuffer(                                  //
			bounds.Left(), source.height - bounds.Bottom(), // src0
			bounds.Right(), source.height - bounds.Top(),   // src1
			0, bounds.Height(),                             // dst0
			bounds.Width(), 0,                              // dst1
			GL_COLOR_BUFFER_BIT, GL_NEAREST                 //
		);

		if (render_texture_target)
		{
			glBindTexture(GL_TEXTURE_2D, (GLuint)render_texture_target);

			const Gfx::FramebufferData& texture_source = destination;
			glBindFramebuffer(GL_READ_FRAMEBUFFER, texture_source.framebuffer);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, bounds.Width(), bounds.Height());
		}

		SetScissor(initial_scissor_state);
	}
	break;
	default: break;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, render_state.GetTopLayer().framebuffer);

	Gfx::CheckGLError("PopLayer");
}

void RenderInterface_GL3::SubmitTransformUniform(Rml::Vector2f translation)
{
	static_assert((size_t)ProgramId::Count < MaxNumPrograms, "Maximum number of programs exceeded.");
	const size_t program_index = (size_t)Gfx::active_program;

	if (program_transform_dirty.test(program_index))
	{
		glUniformMatrix4fv(Gfx::uniforms.Get(Gfx::active_program, UniformId::Transform), 1, false, transform.data());
		program_transform_dirty.set(program_index, false);
	}

	glUniform2fv(Gfx::uniforms.Get(Gfx::active_program, UniformId::Translate), 1, &translation.x);

	Gfx::CheckGLError("SubmitTransformUniform");
}

bool RenderInterface_GL3::Initialize()
{
#if defined RMLUI_PLATFORM_EMSCRIPTEN
	Rml::Log::Message(Rml::Log::LT_INFO, "Initializing Emscripten WebGL renderer.");
#else
	const int gl_version = gladLoaderLoadGL();

	if (gl_version == 0)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to initialize OpenGL context.");
		return false;
	}

	Rml::Log::Message(Rml::Log::LT_INFO, "Loaded OpenGL %d.%d.", GLAD_VERSION_MAJOR(gl_version), GLAD_VERSION_MINOR(gl_version));
#endif

	if (!Gfx::CreateShaders())
		return false;

	return true;
}

void RenderInterface_GL3::Shutdown()
{
	render_state.Shutdown();

	Gfx::DestroyShaders();

#if !defined RMLUI_PLATFORM_EMSCRIPTEN
	gladLoaderUnloadGL();
#endif

	viewport_width = 0;
	viewport_height = 0;
}

void RenderInterface_GL3::SetViewport(int width, int height)
{
	viewport_width = width;
	viewport_height = height;
}

void RenderInterface_GL3::BeginFrame()
{
	RMLUI_ASSERT(viewport_width > 0 && viewport_height > 0);
	glViewport(0, 0, viewport_width, viewport_height);

	glDisable(GL_CULL_FACE);
	glActiveTexture(GL_TEXTURE0);

#ifndef RMLUI_PLATFORM_EMSCRIPTEN
	// We do blending in nonlinear sRGB space because everyone else does it like that.
	glDisable(GL_FRAMEBUFFER_SRGB);
#endif

	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);

#if RMLUI_PREMULTIPLIED_ALPHA
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#else
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif

	render_state.BeginFrame(viewport_width, viewport_height);
	glBindFramebuffer(GL_FRAMEBUFFER, render_state.GetTopLayer().framebuffer);

	glClearStencil(0);
	glStencilMask(GLuint(-1));
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	Gfx::projection = Rml::Matrix4f::ProjectOrtho(0, (float)viewport_width, (float)viewport_height, 0, -10000, 10000);

	Gfx::UseProgram(ProgramId::None);
	has_mask = false;
	program_transform_dirty.set();
	SetTransform(nullptr);
	scissor_state = Rml::Rectanglei::CreateInvalid();

	Gfx::CheckGLError("BeginFrame");
}

void RenderInterface_GL3::Render(Rml::RenderData& data)
{
	const Rml::CompiledGeometryHandle global_geometry_handle =
		CompileGeometry((Rml::Vertex*)data.vertices.data(), (int)data.vertices.size(), (int*)data.indices.data(), (int)data.indices.size(), {});
	const GLuint global_geometry_vao = reinterpret_cast<Gfx::CompiledGeometryData*>(global_geometry_handle)->vao;

	SetTransform(nullptr);
	DisableScissor();
	glDisable(GL_STENCIL_TEST);

	auto RenderGeometry = [this, &data, global_geometry_vao, previous_transform_offset = 0](const Rml::RenderCommandGeometry& geometry,
							  Rml::TextureHandle texture, bool use_program = true) mutable {
		if (geometry.transform_offset != previous_transform_offset)
		{
			if (geometry.transform_offset)
				SetTransform(&data.transforms[geometry.transform_offset]);
			else
				SetTransform(nullptr);

			previous_transform_offset = geometry.transform_offset;
		}

		const Rml::Vector2f translation = data.translations[geometry.translation_offset];
		if (texture == TexturePostprocess)
		{
			// Do nothing.
		}
		else if (texture)
		{
			if (use_program)
				Gfx::UseProgram(ProgramId::Texture);
			SubmitTransformUniform(translation);
			if (texture != TextureIgnoreBinding)
				glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
		}
		else
		{
			if (use_program)
				Gfx::UseProgram(ProgramId::Color);
			SubmitTransformUniform(translation);
		}

		glBindVertexArray(global_geometry_vao);
		glDrawElementsBaseVertex(GL_TRIANGLES, geometry.num_elements, GL_UNSIGNED_INT, (const GLvoid*)(sizeof(int) * geometry.indices_offset),
			(GLint)geometry.vertices_offset);
	};

	for (const Rml::RenderCommand& command : data.commands)
	{
		using Type = Rml::RenderCommandType;

		if (command.scissor_offset)
			SetScissor(data.scissor_regions[command.scissor_offset]);
		else
			DisableScissor();

		switch (command.type)
		{
		case Type::RenderGeometry:
		{
			RenderGeometry(command.render_geometry.geometry, command.render_geometry.texture);
		}
		break;
		case Type::RenderShader:
		{
			SetCustomShader(command.render_shader.handle);
			RenderGeometry(command.render_shader.geometry, command.render_shader.texture, false);
		}
		break;
		case Type::RenderClipMask:
		{
			using Rml::ClipMaskOperation;
			ClipMaskOperation mask_operation = command.render_clip_mask.operation;

			glEnable(GL_STENCIL_TEST);

			const bool clear_stencil = (mask_operation == ClipMaskOperation::Clip || mask_operation == ClipMaskOperation::ClipOut);
			if (clear_stencil)
			{
				// @performance We can be smarter about this and increment the reference value instead of clearing each time.
				glEnable(GL_STENCIL_TEST);
				glClear(GL_STENCIL_BUFFER_BIT);
			}

			GLint stencil_test_value = 0;
			glGetIntegerv(GL_STENCIL_REF, &stencil_test_value);

			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
			glStencilFunc(GL_ALWAYS, GLint(1), GLuint(-1));

			switch (mask_operation)
			{
			case ClipMaskOperation::Clip:
			{
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				stencil_test_value = 1;
			}
			break;
			case ClipMaskOperation::ClipIntersect:
			{
				glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
				stencil_test_value += 1;
			}
			break;
			case ClipMaskOperation::ClipOut:
			{
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				stencil_test_value = 0;
			}
			break;
			}

			RenderGeometry(command.render_clip_mask.geometry, command.render_clip_mask.texture);

			// Restore state
			// @performance Cache state so we don't toggle it unnecessarily.
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			glStencilFunc(GL_EQUAL, stencil_test_value, GLuint(-1));
		}
		break;
		case Type::DisableClipMask:
		{
			glDisable(GL_STENCIL_TEST);
		}
		break;
		case Type::PushLayer:
		{
			if (command.push_layer.clear_new_layer == Rml::RenderClear::Clone)
				render_state.PushLayerClone();
			else
				render_state.PushLayer();

			glBindFramebuffer(GL_FRAMEBUFFER, render_state.GetTopLayer().framebuffer);
			if (command.push_layer.clear_new_layer == Rml::RenderClear::Clear)
				glClear(GL_COLOR_BUFFER_BIT);
		}
		break;
		case Type::PopLayer:
		{
			PopLayer(command.pop_layer.render_target, command.pop_layer.blend_mode, data.filter_lists[command.pop_layer.filter_lists_offset],
				command.pop_layer.render_texture);
		}
		break;
		}
	}

	ReleaseCompiledGeometry(global_geometry_handle);
	SetTransform(nullptr);
	DisableScissor();
	glDisable(GL_STENCIL_TEST);
}

void RenderInterface_GL3::EndFrame()
{
	const Gfx::FramebufferData& fb_active = render_state.GetTopLayer();
	const Gfx::FramebufferData& fb_postprocess = render_state.GetPostprocessPrimary();

	// Resolve MSAA to postprocess framebuffer.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_active.framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_postprocess.framebuffer);

	glBlitFramebuffer(0, 0, fb_active.width, fb_active.height, 0, 0, fb_postprocess.width, fb_postprocess.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

	// Draw to backbuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	// Assuming we have an opaque background, we can just write to it with the premultiplied alpha blend mode and we'll get the correct result.
	// Instead, if we had a transparent destination that didn't use pre-multiplied alpha, we would have to perform a manual un-premultiplication step.
	glActiveTexture(GL_TEXTURE0);
	Gfx::BindTexture(fb_postprocess);
	Gfx::UseProgram(ProgramId::Passthrough);
	DrawFullscreenQuad();

	render_state.EndFrame();

	Gfx::CheckGLError("EndFrame");
}

void RenderInterface_GL3::Clear()
{
	glClearStencil(0);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}
