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
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/GeometryUtilities.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Platform.h>
#include <string.h>

#if defined(RMLUI_PLATFORM_WIN32) && !defined(__MINGW32__)
	// function call missing argument list
	#pragma warning(disable : 4551)
	// unreferenced local function has been removed
	#pragma warning(disable : 4505)
#endif

#if defined RMLUI_PLATFORM_EMSCRIPTEN
	#define RMLUI_SHADER_HEADER_PRE "#version 300 es\nprecision highp float;\n"
	#include <GLES3/gl3.h>
#else
	#define RMLUI_SHADER_HEADER_PRE "#version 330\n"
	#define GLAD_GL_IMPLEMENTATION
	#include "RmlUi_Include_GL3.h"
#endif

#define RMLUI_PREMULTIPLIED_ALPHA 1

#if RMLUI_PREMULTIPLIED_ALPHA
	#define RMLUI_SHADER_HEADER RMLUI_SHADER_HEADER_PRE "#define RMLUI_PREMULTIPLIED_ALPHA 1 "
#else
	#define RMLUI_SHADER_HEADER RMLUI_SHADER_HEADER_PRE "#define RMLUI_PREMULTIPLIED_ALPHA 0 "
#endif

#define STRINGIFY_MACRO_IMPL(x) #x
#define STRINGIFY_MACRO(x) STRINGIFY_MACRO_IMPL(x)

static int viewport_width = 0;
static int viewport_height = 0;

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
static const char* shader_frag_main_texture = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	finalColor = fragColor * texColor;
}
)";
static const char* shader_frag_main_color = RMLUI_SHADER_HEADER R"(
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	finalColor = fragColor;
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
static const char* shader_frag_effect_pre = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform float _value;
uniform vec4 _color;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
)";
static const char* shader_frag_effect_post = R"(
	finalColor = texColor;
}
)";
static const char* shader_frag_passthrough = "";
static const char* shader_frag_sepia = R"(
	vec3 r_mix = vec3(0.393, 0.769, 0.189);
	vec3 g_mix = vec3(0.349, 0.686, 0.168);
	vec3 b_mix = vec3(0.272, 0.534, 0.131);
	vec3 sepia = vec3(dot(texColor.rgb, r_mix), dot(texColor.rgb, g_mix), dot(texColor.rgb, b_mix));
	texColor.rgb = mix(texColor.rgb, sepia, _value);
)";
static const char* shader_frag_gray = R"(
	float gray = 0.2126 * texColor.r + 0.7152 * texColor.g + 0.0722 * texColor.b;
	texColor.rgb = mix(texColor.rgb, vec3(gray), _value);
)";
static const char* shader_frag_dropshadow = R"(
	texColor = texColor.a * _color;
)";
static const char* shader_frag_brightness = R"(
	texColor.rgb *= _value;
)";
static const char* shader_frag_contrast = R"(
	vec3 gray = vec3(0.5 * texColor.a);
	texColor.rgb = mix(gray, texColor.rgb, _value);
)";
static const char* shader_frag_invert = R"(
	vec3 inverted = vec3(texColor.a) - texColor.rgb;
	texColor.rgb = mix(texColor.rgb, inverted, _value);
)";
// Hue-rotation and saturation values based on: https://www.w3.org/TR/filter-effects-1/#attr-valuedef-type-huerotate
static const char* shader_frag_hue_rotate = R"(
	mat3 G = mat3(vec3(0.213), vec3(0.715), vec3(0.072));
	mat3 C = mat3(vec3(0.787, -0.213, -0.213), vec3(-0.715, 0.285, -0.715), vec3(-0.072, -0.072, 0.928));
	mat3 S = mat3(vec3(-0.213, 0.143, -0.787), vec3(-0.715, 0.140, 0.715), vec3(0.928, -0.283, 0.072));
	mat3 A = G + C * cos(_value) + S * sin(_value);
	texColor.rgb = A * texColor.rgb;
)";
static const char* shader_frag_saturate = R"(
	mat3 G = mat3(vec3(0.213), vec3(0.715), vec3(0.072));
	mat3 C = mat3(vec3(0.787, -0.213, -0.213), vec3(-0.715, 0.285, -0.715), vec3(-0.072, -0.072, 0.928));
	mat3 A = G + C * _value;
	texColor.rgb = A * texColor.rgb;
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

#define BLUR_SIZE 7
#define NUM_WEIGHTS ((BLUR_SIZE + 1) / 2)

static constexpr int blur_size = BLUR_SIZE;
static constexpr int num_weights = NUM_WEIGHTS;

#define RMLUI_SHADER_BLUR_HEADER \
	RMLUI_SHADER_HEADER "\n#define BLUR_SIZE " STRINGIFY_MACRO(BLUR_SIZE) "\n#define NUM_WEIGHTS " STRINGIFY_MACRO(NUM_WEIGHTS)

static const char* vert_blur = RMLUI_SHADER_BLUR_HEADER R"(
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
static const char* frag_blur = RMLUI_SHADER_BLUR_HEADER R"(
uniform sampler2D _tex;
uniform float _weights[NUM_WEIGHTS];
uniform float _value;

in vec2 fragTexCoord[BLUR_SIZE];
out vec4 finalColor;

void main() {    
	vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
	for(int i = 0; i < BLUR_SIZE; i++)
		color += texture(_tex, fragTexCoord[i]) * _weights[abs(i - NUM_WEIGHTS + 1)];
	finalColor = color * _value;
}
)";

namespace Gfx {

enum class ProgramUniform { Translate, Transform, Tex, Value, Color, TexelOffset, Weights, TexMask, Count };
static const char* const program_uniform_names[(size_t)ProgramUniform::Count] = {"_translate", "_transform", "_tex", "_value", "_color",
	"_texelOffset", "_weights[0]", "_texMask"};

enum class VertexAttribute { Position, Color0, TexCoord0, Count };
static const char* const vertex_attribute_names[(size_t)VertexAttribute::Count] = {"inPosition", "inColor0", "inTexCoord0"};

struct CompiledGeometryData {
	Rml::TextureHandle texture;
	GLuint vao;
	GLuint vbo;
	GLuint ibo;
	GLsizei draw_count;
};

struct Shaders {
	GLuint vert_main;
	GLuint frag_main_color;
	GLuint frag_main_texture;

	GLuint vert_passthrough;
	GLuint frag_passthrough;
	GLuint frag_brightness;
	GLuint frag_contrast;
	GLuint frag_invert;
	GLuint frag_sepia;
	GLuint frag_gray;
	GLuint frag_dropshadow;
	GLuint frag_hue_rotate;
	GLuint frag_saturate;

	GLuint frag_blend_mask;

	GLuint vert_blur;
	GLuint frag_blur;
};

struct ProgramData {
	GLuint id;
	GLint uniform_locations[(size_t)ProgramUniform::Count];
};
struct Programs {
	ProgramData main_color;
	ProgramData main_texture;

	ProgramData passthrough;
	ProgramData brightness;
	ProgramData contrast;
	ProgramData invert;
	ProgramData sepia;
	ProgramData gray;
	ProgramData dropshadow;
	ProgramData hue_rotate;
	ProgramData saturate;

	ProgramData blend_mask;

	ProgramData blur;
};

struct FramebufferData {
	int width, height;
	GLuint framebuffer;
	GLuint tex_color_buffer;
	GLenum tex_color_target;
	GLuint depth_stencil_buffer;
	bool owns_depth_stencil_buffer;
};

enum class FramebufferAttachment { None, Depth, DepthStencil };

static Shaders shaders = {};
static Programs programs = {};
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

static bool CreateShader(GLuint& out_shader_id, GLenum shader_type, const char* code_string, const char* code_pre = nullptr,
	const char* code_post = nullptr)
{
	RMLUI_ASSERT(shader_type == GL_VERTEX_SHADER || shader_type == GL_FRAGMENT_SHADER);

	Rml::String combined_code;
	if (code_pre && code_post)
	{
		combined_code = Rml::String(code_pre) + Rml::String(code_string) + Rml::String(code_post);
		code_string = combined_code.c_str();
	}

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

static bool CreateProgram(ProgramData& out_program, GLuint vertex_shader, GLuint fragment_shader)
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

	out_program = {};
	out_program.id = id;

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
		ProgramUniform program_uniform = ProgramUniform::Count;
		for (int i = 0; i < (int)ProgramUniform::Count; i++)
		{
			const char* uniform_name = program_uniform_names[i];
			if (strcmp(name_buf, uniform_name) == 0)
			{
				program_uniform = (ProgramUniform)i;
				break;
			}
		}

		if ((size_t)program_uniform < (size_t)ProgramUniform::Count)
		{
			out_program.uniform_locations[(size_t)program_uniform] = location;
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
	constexpr GLenum color_format = GL_RGBA8;       // GL_RGBA8 GL_SRGB8_ALPHA8 GL_RGBA16F
	constexpr GLint min_mag_filter = GL_LINEAR;     // GL_NEAREST
	constexpr GLint wrap_mode = GL_CLAMP_TO_BORDER; // GL_REPEAT GL_MIRRORED_REPEAT GL_CLAMP_TO_EDGE
	const Rml::Colourf border_color(0.f, 0.f);
	const GLenum tex_color_target = samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

	GLuint framebuffer = 0;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	GLuint tex_color_buffer = 0;
	{
		glGenTextures(1, &tex_color_buffer);
		glBindTexture(tex_color_target, tex_color_buffer);

		if (samples > 0)
			glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, color_format, width, height, GL_TRUE);
		else
			glTexImage2D(GL_TEXTURE_2D, 0, color_format, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_mag_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, min_mag_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &border_color[0]);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex_color_target, tex_color_buffer, 0);
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

			const GLenum internal_format = (attachment == FramebufferAttachment::DepthStencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT32);
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
	glBindTexture(tex_color_target, 0);

	CheckGLError("CreateFramebuffer");

	out_fb = {};
	out_fb.width = width;
	out_fb.height = height;
	out_fb.framebuffer = framebuffer;
	out_fb.tex_color_buffer = tex_color_buffer;
	out_fb.tex_color_target = tex_color_target;
	out_fb.depth_stencil_buffer = depth_stencil_buffer;
	out_fb.owns_depth_stencil_buffer = !shared_depth_stencil_buffer;

	return true;
}

void DestroyFramebuffer(FramebufferData& fb)
{
	glDeleteFramebuffers(1, &fb.framebuffer);
	if (fb.tex_color_buffer)
		glDeleteTextures(1, &fb.tex_color_buffer);
	if (fb.owns_depth_stencil_buffer && fb.depth_stencil_buffer)
		glDeleteRenderbuffers(1, &fb.depth_stencil_buffer);
	fb = {};
}

static bool CreateShaders(Shaders& out_shaders, Programs& out_programs)
{
	auto ReportError = [](const char* type, const char* name) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Could not create OpenGL %s: '%s'.", type, name);
		return false;
	};

	out_shaders = {};

	// Main shaders
	if (!CreateShader(out_shaders.vert_main, GL_VERTEX_SHADER, shader_vert_main))
		return ReportError("shader", "vert_main");
	if (!CreateShader(out_shaders.frag_main_color, GL_FRAGMENT_SHADER, shader_frag_main_color))
		return ReportError("shader", "frag_main_color");
	if (!CreateShader(out_shaders.frag_main_texture, GL_FRAGMENT_SHADER, shader_frag_main_texture))
		return ReportError("shader", "frag_main_texture");

	if (!CreateProgram(out_programs.main_color, out_shaders.vert_main, out_shaders.frag_main_color))
		return ReportError("program", "main_color");
	if (!CreateProgram(out_programs.main_texture, out_shaders.vert_main, out_shaders.frag_main_texture))
		return ReportError("program", "main_texture");

	// Effects
	if (!CreateShader(out_shaders.vert_passthrough, GL_VERTEX_SHADER, shader_vert_passthrough))
		return ReportError("shader", "vert_passthrough");
	if (!CreateShader(out_shaders.frag_passthrough, GL_FRAGMENT_SHADER, shader_frag_passthrough, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_passthrough");
	if (!CreateShader(out_shaders.frag_brightness, GL_FRAGMENT_SHADER, shader_frag_brightness, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_brightness");
	if (!CreateShader(out_shaders.frag_contrast, GL_FRAGMENT_SHADER, shader_frag_contrast, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_contrast");
	if (!CreateShader(out_shaders.frag_invert, GL_FRAGMENT_SHADER, shader_frag_invert, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_invert");
	if (!CreateShader(out_shaders.frag_sepia, GL_FRAGMENT_SHADER, shader_frag_sepia, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_sepia");
	if (!CreateShader(out_shaders.frag_gray, GL_FRAGMENT_SHADER, shader_frag_gray, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_gray");
	if (!CreateShader(out_shaders.frag_dropshadow, GL_FRAGMENT_SHADER, shader_frag_dropshadow, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_dropshadow");
	if (!CreateShader(out_shaders.frag_hue_rotate, GL_FRAGMENT_SHADER, shader_frag_hue_rotate, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_hue_rotate");
	if (!CreateShader(out_shaders.frag_saturate, GL_FRAGMENT_SHADER, shader_frag_saturate, shader_frag_effect_pre, shader_frag_effect_post))
		return ReportError("shader", "frag_saturate");

	if (!CreateProgram(out_programs.passthrough, out_shaders.vert_passthrough, out_shaders.frag_passthrough))
		return ReportError("program", "passthrough");
	if (!CreateProgram(out_programs.brightness, out_shaders.vert_passthrough, out_shaders.frag_brightness))
		return ReportError("program", "brightness");
	if (!CreateProgram(out_programs.contrast, out_shaders.vert_passthrough, out_shaders.frag_contrast))
		return ReportError("program", "contrast");
	if (!CreateProgram(out_programs.invert, out_shaders.vert_passthrough, out_shaders.frag_invert))
		return ReportError("program", "invert");
	if (!CreateProgram(out_programs.sepia, out_shaders.vert_passthrough, out_shaders.frag_sepia))
		return ReportError("program", "sepia");
	if (!CreateProgram(out_programs.gray, out_shaders.vert_passthrough, out_shaders.frag_gray))
		return ReportError("program", "gray");
	if (!CreateProgram(out_programs.dropshadow, out_shaders.vert_passthrough, out_shaders.frag_dropshadow))
		return ReportError("program", "dropshadow");
	if (!CreateProgram(out_programs.hue_rotate, out_shaders.vert_passthrough, out_shaders.frag_hue_rotate))
		return ReportError("program", "hue_rotate");
	if (!CreateProgram(out_programs.saturate, out_shaders.vert_passthrough, out_shaders.frag_saturate))
		return ReportError("program", "saturate");

	// Blend mask
	if (!CreateShader(out_shaders.frag_blend_mask, GL_FRAGMENT_SHADER, shader_frag_blend_mask))
		return ReportError("shader", "frag_blend_mask");
	if (!CreateProgram(out_programs.blend_mask, out_shaders.vert_passthrough, out_shaders.frag_blend_mask))
		return ReportError("program", "blend_mask");

	// Blur
	if (!CreateShader(out_shaders.vert_blur, GL_VERTEX_SHADER, vert_blur))
		return ReportError("shader", "blur_vertex");
	if (!CreateShader(out_shaders.frag_blur, GL_FRAGMENT_SHADER, frag_blur))
		return ReportError("shader", "blur_fragment");

	if (!CreateProgram(out_programs.blur, out_shaders.vert_blur, out_shaders.frag_blur))
		return ReportError("program", "blur");

	return true;
}

static void DestroyShaders()
{
	glDeleteProgram(programs.main_color.id);
	glDeleteProgram(programs.main_texture.id);
	glDeleteShader(shaders.vert_main);
	glDeleteShader(shaders.frag_main_color);
	glDeleteShader(shaders.frag_main_texture);

	glDeleteProgram(programs.passthrough.id);
	glDeleteProgram(programs.brightness.id);
	glDeleteProgram(programs.contrast.id);
	glDeleteProgram(programs.invert.id);
	glDeleteProgram(programs.sepia.id);
	glDeleteProgram(programs.gray.id);
	glDeleteShader(shaders.vert_passthrough);
	glDeleteShader(shaders.frag_passthrough);
	glDeleteShader(shaders.frag_brightness);
	glDeleteShader(shaders.frag_contrast);
	glDeleteShader(shaders.frag_invert);
	glDeleteShader(shaders.frag_sepia);
	glDeleteShader(shaders.frag_gray);

	glDeleteShader(shaders.vert_blur);
	glDeleteShader(shaders.frag_blur);
	glDeleteProgram(programs.blur.id);

	shaders = {};
	programs = {};
}

static void DrawFullscreenQuad(Rml::Vector2f uv_offset = {}, Rml::Vector2f uv_scaling = Rml::Vector2f(1.f))
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

	render_interface->RenderGeometry(vertices, 4, indices, 6, RenderInterface_GL3::TexturePostprocess, {});
}

} // namespace Gfx

RenderInterface_GL3::RenderInterface_GL3()
{
	RMLUI_ASSERT(!Gfx::render_interface);
	Gfx::render_interface = this;
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
		glUseProgram(Gfx::programs.main_texture.id);

		if (geometry->texture != TextureIgnoreBinding)
			glBindTexture(GL_TEXTURE_2D, (GLuint)geometry->texture);

		SubmitTransformUniform(ProgramId::Texture, Gfx::programs.main_texture.uniform_locations[(size_t)Gfx::ProgramUniform::Transform]);
		glUniform2fv(Gfx::programs.main_texture.uniform_locations[(size_t)Gfx::ProgramUniform::Translate], 1, &translation.x);
	}
	else
	{
		glUseProgram(Gfx::programs.main_color.id);
		glBindTexture(GL_TEXTURE_2D, 0);
		SubmitTransformUniform(ProgramId::Color, Gfx::programs.main_color.uniform_locations[(size_t)Gfx::ProgramUniform::Transform]);
		glUniform2fv(Gfx::programs.main_color.uniform_locations[(size_t)Gfx::ProgramUniform::Translate], 1, &translation.x);
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

void RenderInterface_GL3::EnableScissorRegion(bool enable)
{
	if (enable)
		glEnable(GL_SCISSOR_TEST);
	else
		glDisable(GL_SCISSOR_TEST);
	scissor_state.enabled = enable;
}

void RenderInterface_GL3::SetScissorRegion(int x, int y, int width, int height)
{
	glScissor(x, viewport_height - (y + height), width, height);
	scissor_state.x = x;
	scissor_state.y = y;
	scissor_state.width = width;
	scissor_state.height = height;
}

bool RenderInterface_GL3::ExecuteStencilCommand(Rml::StencilCommand command, int value, int mask)
{
	RMLUI_ASSERT(value >= 0 && value <= 255 && mask >= 0 && mask <= 255);
	using Rml::StencilCommand;

	switch (command)
	{
	case StencilCommand::Clear:
	{
		RMLUI_ASSERT(value == 0);
		glEnable(GL_STENCIL_TEST);
		glStencilMask(GLuint(mask));
		glClear(GL_STENCIL_BUFFER_BIT);
	}
	break;
	case StencilCommand::WriteValue:
	{
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glStencilFunc(GL_ALWAYS, GLint(value), GLuint(-1));
		glStencilMask(GLuint(mask));
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	}
	break;
	case StencilCommand::WriteIncrement:
	{
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glStencilMask(GLuint(mask));
		glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
	}
	break;
	case StencilCommand::WriteDisable:
	{
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glStencilMask(0);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	}
	break;
	case StencilCommand::TestEqual:
	{
		glStencilFunc(GL_EQUAL, GLint(value), GLuint(mask));
	}
	break;
	case StencilCommand::TestDisable:
	{
		glStencilFunc(GL_ALWAYS, GLint(value), GLuint(mask));
	}
	break;
	case StencilCommand::None:
		break;
	}

	return true;
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
	Rml::UniquePtr<byte[]> source_premultiplied;
	if (source)
	{
		using Rml::byte;
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

void RenderInterface_GL3::ReleaseTexture(Rml::TextureHandle texture_handle)
{
	glDeleteTextures(1, (GLuint*)&texture_handle);
}

void RenderInterface_GL3::SetTransform(const Rml::Matrix4f* new_transform)
{
	transform = Gfx::projection * (new_transform ? *new_transform : Rml::Matrix4f::Identity());
	transform_dirty_state = ProgramId::All;
}

class RenderState {
public:
	void PushStack()
	{
		if (fb_stack_size == (int)fb_stack.size())
		{
			constexpr int num_samples = 2;
			Gfx::FramebufferData fb = {};

			// All framebuffers should share a single stencil buffer.
			GLuint shared_depth_stencil = (fb_stack_size >= 1 ? fb_stack[0].depth_stencil_buffer : 0);
			Gfx::CreateFramebuffer(fb, width, height, num_samples, Gfx::FramebufferAttachment::DepthStencil, shared_depth_stencil);
			fb_stack.push_back(std::move(fb));

			RMLUI_ASSERT(fb_stack_size + 1 == (int)fb_stack.size());
		}

		const Gfx::FramebufferData& fb_new = fb_stack[fb_stack_size];
		fb_stack_size += 1;

		glBindFramebuffer(GL_FRAMEBUFFER, fb_new.framebuffer);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	void PopStack()
	{
		RMLUI_ASSERT(fb_stack_size > 0);
		fb_stack_size -= 1;
	}

	const Gfx::FramebufferData& GetActiveFramebuffer() const
	{
		RMLUI_ASSERT(fb_stack_size > 0);
		return fb_stack[fb_stack_size - 1];
	}

	const Gfx::FramebufferData& GetPostprocessPrimary() const { return fb_postprocess_primary; }
	const Gfx::FramebufferData& GetPostprocessSecondary() const { return fb_postprocess_secondary; }
	const Gfx::FramebufferData& GetPostprocessTertiary()
	{
		// This one is more rarily used, so we create it on demand.
		if (!fb_postprocess_tertiary.framebuffer)
			Gfx::CreateFramebuffer(fb_postprocess_tertiary, width, height, 0, Gfx::FramebufferAttachment::None, 0);
		return fb_postprocess_tertiary;
	}
	void SwapPostprocessPrimarySecondary() { std::swap(fb_postprocess_primary, fb_postprocess_secondary); }

	void BeginFrame(int new_width, int new_height)
	{
		RMLUI_ASSERT(fb_stack_size == 0);

		if (new_width != width || new_height != height)
		{
			width = new_width;
			height = new_height;

			DestroyFramebuffers();
			Gfx::CreateFramebuffer(fb_postprocess_primary, width, height, 0, Gfx::FramebufferAttachment::None, 0);
			Gfx::CreateFramebuffer(fb_postprocess_secondary, width, height, 0, Gfx::FramebufferAttachment::None, 0);
		}

		PushStack();
	}

	void EndFrame()
	{
		RMLUI_ASSERT(fb_stack_size == 1);
		PopStack();
	}

	void Shutdown() { DestroyFramebuffers(); }

private:
	void DestroyFramebuffers()
	{
		RMLUI_ASSERTMSG(fb_stack_size == 0, "Do not call this during frame rendering, that is, between BeginFrame() and EndFrame().");

		for (Gfx::FramebufferData& fb : fb_stack)
			Gfx::DestroyFramebuffer(fb);

		fb_stack.clear();

		Gfx::DestroyFramebuffer(fb_postprocess_primary);
		Gfx::DestroyFramebuffer(fb_postprocess_secondary);
		Gfx::DestroyFramebuffer(fb_postprocess_tertiary);
	}

	int width = 0, height = 0;

	int fb_stack_size = 0;
	Rml::Vector<Gfx::FramebufferData> fb_stack;

	Gfx::FramebufferData fb_postprocess_primary = {};
	Gfx::FramebufferData fb_postprocess_secondary = {};
	Gfx::FramebufferData fb_postprocess_tertiary = {};
};

static RenderState render_state;

Rml::TextureHandle RenderInterface_GL3::ExecuteRenderCommand(Rml::RenderCommand command, Rml::Vector2i offset, Rml::Vector2i dimensions)
{
	Rml::TextureHandle texture_handle = {};

	switch (command)
	{
	case Rml::RenderCommand::StackPush:
	{
		render_state.PushStack();
	}
	break;
	case Rml::RenderCommand::StackPop:
	{
		render_state.PopStack();
		glBindFramebuffer(GL_FRAMEBUFFER, render_state.GetActiveFramebuffer().framebuffer);
	}
	break;
	case Rml::RenderCommand::StackToTexture:
	{
		const bool scissor_initially_enabled = scissor_state.enabled;
		EnableScissorRegion(false);

		const Gfx::FramebufferData& source = render_state.GetActiveFramebuffer();
		const Gfx::FramebufferData& destination = render_state.GetPostprocessPrimary();
		glBindFramebuffer(GL_READ_FRAMEBUFFER, source.framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);

		// Blit the desired stack region to the postprocess framebuffer to resolve MSAA. Also flip the image vertically since that convention is used
		// for textures.
		glBlitFramebuffer(offset.x, source.height - offset.y, offset.x + dimensions.x, source.height - (offset.y + dimensions.y), 0, 0, dimensions.x,
			dimensions.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		if (GenerateTexture(texture_handle, nullptr, dimensions))
		{
			glBindTexture(GL_TEXTURE_2D, (GLuint)texture_handle);

			glBindFramebuffer(GL_READ_FRAMEBUFFER, destination.framebuffer);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, dimensions.x, dimensions.y);
		}

		Gfx::CheckGLError("StackToTexture");

		EnableScissorRegion(scissor_initially_enabled);
	}
	break;
	case Rml::RenderCommand::StackToFilter:
	{
		pre_filter_scissor_state = scissor_state;
		const Rml::Vector2i scissor_size = (dimensions == Rml::Vector2i(0) ? Rml::Vector2i(viewport_width, viewport_height) : dimensions);
		EnableScissorRegion(true);
		SetScissorRegion(offset.x, offset.y, scissor_size.x, scissor_size.y);

		const Gfx::FramebufferData& source = render_state.GetActiveFramebuffer();
		const Gfx::FramebufferData& destination = render_state.GetPostprocessPrimary();
		glBindFramebuffer(GL_READ_FRAMEBUFFER, source.framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);

#if 0
		// Debug clear
		EnableScissorRegion(false);
		glClearColor(0, 1, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glClearColor(0, 0, 0, 0);
		EnableScissorRegion(true);
#endif

		glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, destination.width, destination.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		has_mask = false;
	}
	break;
	case Rml::RenderCommand::FilterToStack:
	{
		const Gfx::FramebufferData& source = render_state.GetPostprocessPrimary();
		const Gfx::FramebufferData& destination = render_state.GetActiveFramebuffer();

		glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);
		glBindTexture(source.tex_color_target, source.tex_color_buffer);

		if (!has_mask)
		{
			glUseProgram(Gfx::programs.passthrough.id);
		}
		else
		{
			const Gfx::FramebufferData& mask = render_state.GetPostprocessSecondary();
			glUseProgram(Gfx::programs.blend_mask.id);
			glUniform1i(Gfx::programs.blend_mask.uniform_locations[(int)Gfx::ProgramUniform::TexMask], 1);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(mask.tex_color_target, mask.tex_color_buffer);
			glActiveTexture(GL_TEXTURE0);
		}
		Gfx::DrawFullscreenQuad();

		EnableScissorRegion(pre_filter_scissor_state.enabled);
		SetScissorRegion(pre_filter_scissor_state.x, pre_filter_scissor_state.y, pre_filter_scissor_state.width, pre_filter_scissor_state.height);
		pre_filter_scissor_state = {};
		has_mask = false;
	}
	break;
	case Rml::RenderCommand::StackToMask:
	{
		ScissorState scissor_state_initial = scissor_state;
		const Rml::Vector2i scissor_size = (dimensions == Rml::Vector2i(0) ? Rml::Vector2i(viewport_width, viewport_height) : dimensions);
		EnableScissorRegion(true);
		SetScissorRegion(offset.x, offset.y, scissor_size.x, scissor_size.y);

		const Gfx::FramebufferData& source = render_state.GetActiveFramebuffer();
		const Gfx::FramebufferData& destination = render_state.GetPostprocessSecondary();
		glBindFramebuffer(GL_READ_FRAMEBUFFER, source.framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);

		glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, destination.width, destination.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		EnableScissorRegion(scissor_state_initial.enabled);
		SetScissorRegion(scissor_state_initial.x, scissor_state_initial.y, scissor_state_initial.width, scissor_state_initial.height);
		has_mask = true;
	}
	break;
	case Rml::RenderCommand::None:
		break;
	}

	return texture_handle;
}

enum class EffectType { Invalid = 0, Basic, Blur, DropShadow, LinearGradient };
struct CompiledEffect {
	EffectType type;

	// Basic
	Gfx::ProgramData* program;
	bool has_value_uniform;
	float value;
	float blend_factor = -1.f;

	// Blur
	float sigma;

	// Drop shadow
	Rml::Vector2f offset;
	Rml::Colourb color;

	// Linear gradient
	float angle;
	Rml::Vector2f p0, p1;
	Rml::ColorStopList color_stop_list;
};

Rml::CompiledEffectHandle RenderInterface_GL3::CompileEffect(const Rml::String& name, const Rml::Dictionary& parameters)
{
	if (name == "blur")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Blur;
		effect.sigma = 0.5f * Rml::Get(parameters, "radius", 0.0f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(effect));
	}
	else if (name == "drop-shadow")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::DropShadow;
		effect.sigma = Rml::Get(parameters, "sigma", 0.f);
		effect.color = Rml::Get(parameters, "color", Rml::Colourb());
		effect.offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "linear-gradient")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::LinearGradient;
		effect.angle = Rml::Get(parameters, "angle", 0.f);
		effect.p0 = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
		effect.p1 = Rml::Get(parameters, "p1", Rml::Vector2f(0.f));
		effect.color_stop_list = Rml::Get<Rml::ColorStopList>(parameters, "color_stop_list", {});
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "opacity")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Basic;
		effect.program = &Gfx::programs.passthrough;
		effect.blend_factor = Rml::Get(parameters, "value", 1.0f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "brightness")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Basic;
		effect.program = &Gfx::programs.brightness;
		effect.has_value_uniform = true;
		effect.value = Rml::Get(parameters, "value", 1.0f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "contrast")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Basic;
		effect.program = &Gfx::programs.contrast;
		effect.has_value_uniform = true;
		effect.value = Rml::Get(parameters, "value", 1.0f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "invert")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Basic;
		effect.program = &Gfx::programs.invert;
		effect.has_value_uniform = true;
		effect.value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "grayscale")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Basic;
		effect.program = &Gfx::programs.gray;
		effect.has_value_uniform = true;
		effect.value = Rml::Get(parameters, "value", 1.0f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "sepia")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Basic;
		effect.program = &Gfx::programs.sepia;
		effect.has_value_uniform = true;
		effect.value = Rml::Get(parameters, "value", 1.0f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "hue-rotate")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Basic;
		effect.program = &Gfx::programs.hue_rotate;
		effect.has_value_uniform = true;
		effect.value = Rml::Get(parameters, "value", 1.0f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}
	else if (name == "saturate")
	{
		CompiledEffect effect = {};
		effect.type = EffectType::Basic;
		effect.program = &Gfx::programs.saturate;
		effect.has_value_uniform = true;
		effect.value = Rml::Get(parameters, "value", 1.0f);
		return reinterpret_cast<Rml::CompiledEffectHandle>(new CompiledEffect(std::move(effect)));
	}

	return Rml::CompiledEffectHandle(0);
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

	glUniform1fv(Gfx::programs.blur.uniform_locations[(int)Gfx::ProgramUniform::Weights], (GLsizei)num_weights, &weights[0]);
}

static void RenderBlurPass(const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp)
{
	auto SetTexelOffset = [](Rml::Vector2f blur_direction, int texture_dimension) {
		const Rml::Vector2f texel_offset = blur_direction * (1.0f / float(texture_dimension));
		glUniform2f(Gfx::programs.blur.uniform_locations[(int)Gfx::ProgramUniform::TexelOffset], texel_offset.x, texel_offset.y);
	};

	// Vertical
	glBindTexture(source_destination.tex_color_target, source_destination.tex_color_buffer);
	glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);

	Gfx::render_interface->EnableScissorRegion(false);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	Gfx::render_interface->EnableScissorRegion(true);

	SetTexelOffset({0.f, 1.f}, source_destination.height);
	Gfx::DrawFullscreenQuad();

	// Horizontal
	glBindTexture(temp.tex_color_target, temp.tex_color_buffer);
	glBindFramebuffer(GL_FRAMEBUFFER, source_destination.framebuffer);

	Gfx::render_interface->EnableScissorRegion(false);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	Gfx::render_interface->EnableScissorRegion(true);

	SetTexelOffset({1.f, 0.f}, temp.width);
	Gfx::DrawFullscreenQuad();
}

static void RenderBlur(float sigma, const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp, Rml::Vector2i position,
	Rml::Vector2i size)
{
	RMLUI_ASSERT(&source_destination != &temp && source_destination.width == temp.width && source_destination.height == temp.height);

	int pass_level = 0;
	SigmaToParameters(sigma, pass_level, sigma);

#if 0
	// Debug coloring
	Gfx::render_interface->EnableScissorRegion(false);
	glClearColor(1, 0, 0, 1);
	glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0, 0, 0, 0);
#endif

	// Intersect the blur area with the framebuffer dimensions.
	{
		const Rml::Vector2i min = Rml::Math::Max(position, Rml::Vector2i(0));
		const Rml::Vector2i max = Rml::Math::Min(position + size, Rml::Vector2i(source_destination.width, source_destination.height));
		position = min;
		size = Rml::Math::Max(max - min, Rml::Vector2i(0));
	}

	// Begin by downscale so that the blur pass can be done at a reduced resolution for large sigma.
	Rml::Vector2i scissor_min = position;
	Rml::Vector2i scissor_size = size;

	auto SubmitScissor = [&scissor_min, &scissor_size]() { glScissor(scissor_min.x, scissor_min.y, scissor_size.x, scissor_size.y); };

	glUseProgram(Gfx::programs.passthrough.id);
	Gfx::render_interface->EnableScissorRegion(true);
	SubmitScissor();

	// Downscale by iterative half-scaling with bilinear filtering, to reduce aliasing.
	glViewport(0, 0, source_destination.width / 2, source_destination.height / 2);

	// Scale UVs if we have even dimensions, such that texture fetches align perfectly between texels, thereby producing a 50% blend of
	// neighboring texels.
	const Rml::Vector2f uv_scaling = {(source_destination.width % 2 == 1) ? (1.f - 1.f / float(source_destination.width)) : 1.f,
		(source_destination.height % 2 == 1) ? (1.f - 1.f / float(source_destination.height)) : 1.f};

	auto ClearWithMargin = [&]() {
		constexpr int radius = (blur_size + 1) / 2;
		const Rml::Vector2i min = scissor_min - Rml::Vector2i(radius);
		const Rml::Vector2i size = Rml::Math::Max(scissor_size + Rml::Vector2i(2 * radius), Rml::Vector2i(0));
		glScissor(min.x, min.y, size.x, size.y);
		glClear(GL_COLOR_BUFFER_BIT);
		SubmitScissor();
	};

	// Move the texture data to the temp buffer if the last downscaling end up at the source_destination buffer.
	const bool transfer_to_temp_buffer = (pass_level % 2 == 0);

	for (int i = 0; i < pass_level; i++)
	{
		scissor_min = (scissor_min + Rml::Vector2i(1)) / 2;
		scissor_size = scissor_size / 2;
		const bool from_source = (i % 2 == 0);
		glBindTexture(GL_TEXTURE_2D, (from_source ? source_destination : temp).tex_color_buffer);
		glBindFramebuffer(GL_FRAMEBUFFER, (from_source ? temp : source_destination).framebuffer);
		SubmitScissor();

		if (i == pass_level - 1 && !transfer_to_temp_buffer)
			ClearWithMargin();

		Gfx::DrawFullscreenQuad({}, uv_scaling);
	}

	glViewport(0, 0, source_destination.width, source_destination.height);

	if (transfer_to_temp_buffer)
	{
		glBindTexture(GL_TEXTURE_2D, source_destination.tex_color_buffer);
		glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);
		ClearWithMargin();
		Gfx::DrawFullscreenQuad();
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

	// Zero out the region around the blur.
	glBindFramebuffer(GL_FRAMEBUFFER, source_destination.framebuffer);
	ClearWithMargin();

	// Set up uniforms.
	glUseProgram(Gfx::programs.blur.id);
	SetBlurWeights(sigma);
	const float blending_magnitude = 1.f;
	glUniform1f(Gfx::programs.blur.uniform_locations[(int)Gfx::ProgramUniform::Value], blending_magnitude);

	// Now do the actual render pass.
	RenderBlurPass(temp, source_destination);

	// Blit with a one pixel black border so that edges fade to zero.
	const Rml::Vector2i blit_min = scissor_min - Rml::Vector2i(1);
	const Rml::Vector2i blit_max = scissor_min + scissor_size + Rml::Vector2i(1);
	const Rml::Vector2i blit_target_min = blit_min * (1 << pass_level);
	const Rml::Vector2i blit_target_max = blit_max * (1 << pass_level);

	glScissor(position.x, position.y, size.x, size.y);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, temp.framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, source_destination.framebuffer);
	glClear(GL_COLOR_BUFFER_BIT);
	glBlitFramebuffer(blit_min.x, blit_min.y, blit_max.x, blit_max.y, blit_target_min.x, blit_target_min.y, blit_target_max.x, blit_target_max.y,
		GL_COLOR_BUFFER_BIT, GL_LINEAR);

	Gfx::render_interface->EnableScissorRegion(false);
}

Rml::TextureHandle RenderInterface_GL3::RenderEffect(Rml::CompiledEffectHandle effect_handle, Rml::CompiledGeometryHandle geometry,
	Rml::Vector2f translation)
{
	const CompiledEffect& effect = *reinterpret_cast<CompiledEffect*>(effect_handle);

	const EffectType type = effect.type;
	Rml::TextureHandle texture_handle_result = {};

	switch (type)
	{
	case EffectType::Blur:
	{
		ScissorState original_scissor_state = scissor_state;
		glDisable(GL_BLEND);

		const Gfx::FramebufferData& source_destination = render_state.GetPostprocessPrimary();
		const Gfx::FramebufferData& temp = render_state.GetPostprocessSecondary();

		// Draw the blur
		const Rml::Vector2i position = {scissor_state.x, source_destination.height - (scissor_state.y + scissor_state.height)};
		const Rml::Vector2i size = {scissor_state.width, scissor_state.height};

		RenderBlur(effect.sigma, source_destination, temp, position, size);

		// Restore state
		glEnable(GL_BLEND);
		glViewport(0, 0, viewport_width, viewport_height);
		EnableScissorRegion(original_scissor_state.enabled);
		SetScissorRegion(original_scissor_state.x, original_scissor_state.y, original_scissor_state.width, original_scissor_state.height);
	}
	break;
	case EffectType::LinearGradient:
	{
	}
	break;
	case EffectType::DropShadow:
	{
		ScissorState original_scissor_state = scissor_state;
		glUseProgram(Gfx::programs.dropshadow.id);
		glDisable(GL_BLEND);

		Rml::Colourf color;
		color.red = (1.f / 255.f) * (float)effect.color.red;
		color.green = (1.f / 255.f) * (float)effect.color.green;
		color.blue = (1.f / 255.f) * (float)effect.color.blue;
		color.alpha = (1.f / 255.f) * (float)effect.color.alpha;
		glUniform4fv(Gfx::programs.dropshadow.uniform_locations[(int)Gfx::ProgramUniform::Color], 1, &color[0]);

		const Gfx::FramebufferData& primary = render_state.GetPostprocessPrimary();
		const Gfx::FramebufferData& secondary = render_state.GetPostprocessSecondary();
		const Gfx::FramebufferData& tertiary = render_state.GetPostprocessTertiary();
		glBindTexture(primary.tex_color_target, primary.tex_color_buffer);
		glBindFramebuffer(GL_FRAMEBUFFER, secondary.framebuffer);
		glClear(GL_COLOR_BUFFER_BIT);

		ScissorState new_scissor_state = scissor_state;
		new_scissor_state.x += Rml::Math::Max((int)effect.offset.x, 0);
		new_scissor_state.y += Rml::Math::Max((int)effect.offset.y, 0);
		new_scissor_state.width -= std::abs((int)effect.offset.x);
		new_scissor_state.height -= std::abs((int)effect.offset.y);
		SetScissorRegion(new_scissor_state.x, new_scissor_state.y, new_scissor_state.width, new_scissor_state.height);

		const Rml::Vector2f uv_offset = effect.offset / Rml::Vector2f(-(float)viewport_width, (float)viewport_height);
		Gfx::DrawFullscreenQuad(uv_offset);

		if (effect.sigma >= 0.5f)
		{
			const Rml::Vector2i position = {scissor_state.x, primary.height - (scissor_state.y + scissor_state.height)};
			const Rml::Vector2i size = {scissor_state.width, scissor_state.height};
			RenderBlur(effect.sigma, secondary, tertiary, position, size);
		}

		EnableScissorRegion(original_scissor_state.enabled);
		SetScissorRegion(original_scissor_state.x, original_scissor_state.y, original_scissor_state.width, original_scissor_state.height);
		glUseProgram(Gfx::programs.passthrough.id);
		glBindTexture(primary.tex_color_target, primary.tex_color_buffer);
		glEnable(GL_BLEND);
		Gfx::DrawFullscreenQuad();

		render_state.SwapPostprocessPrimarySecondary();
	}
	break;
	case EffectType::Basic:
	{
		if (!effect.program)
			break;

		glUseProgram(effect.program->id);

		if (effect.has_value_uniform)
			glUniform1fv(effect.program->uniform_locations[(int)Gfx::ProgramUniform::Value], 1, &effect.value);

		if (effect.blend_factor >= 0.f)
		{
			glBlendFunc(GL_CONSTANT_ALPHA, GL_ZERO);
			glBlendColor(0.0f, 0.0f, 0.0f, effect.blend_factor);
		}
		else
		{
			glDisable(GL_BLEND);
		}

		const Gfx::FramebufferData& source = render_state.GetPostprocessPrimary();
		const Gfx::FramebufferData& destination = render_state.GetPostprocessSecondary();
		glBindTexture(source.tex_color_target, source.tex_color_buffer);
		glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);

		Gfx::DrawFullscreenQuad();

		render_state.SwapPostprocessPrimarySecondary();

		// Restore state
		if (effect.blend_factor >= 0.f)
		{
#if RMLUI_PREMULTIPLIED_ALPHA
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#else
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
		}
		else
		{
			glEnable(GL_BLEND);
		}

		Gfx::CheckGLError("RenderEffectBasic");
	}
	break;
	case EffectType::Invalid:
	default:
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render effect %d.", (int)type);
	}
	break;
	}

	return texture_handle_result;
}

void RenderInterface_GL3::ReleaseCompiledEffect(Rml::CompiledEffectHandle effect_handle)
{
	delete reinterpret_cast<CompiledEffect*>(effect_handle);
}

void RenderInterface_GL3::SubmitTransformUniform(ProgramId program_id, int uniform_location)
{
	if ((int)program_id & (int)transform_dirty_state)
	{
		glUniformMatrix4fv(uniform_location, 1, false, transform.data());
		transform_dirty_state = ProgramId((int)transform_dirty_state & ~(int)program_id);
	}
}

bool RmlGL3::Initialize()
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

	if (!Gfx::CreateShaders(Gfx::shaders, Gfx::programs))
		return false;

	return true;
}

void RmlGL3::Shutdown()
{
	render_state.Shutdown();

	Gfx::DestroyShaders();

#if !defined RMLUI_PLATFORM_EMSCRIPTEN
	gladLoaderUnloadGL();
#endif

	viewport_width = 0;
	viewport_height = 0;
}

void RmlGL3::SetViewport(int width, int height)
{
	viewport_width = width;
	viewport_height = height;
}

void RmlGL3::BeginFrame()
{
	RMLUI_ASSERT(viewport_width > 0 && viewport_height > 0);
	glViewport(0, 0, viewport_width, viewport_height);

	glDisable(GL_CULL_FACE);
	glActiveTexture(GL_TEXTURE0);

	// We do blending in nonlinear sRGB space because everyone else does it like that.
	glDisable(GL_FRAMEBUFFER_SRGB);

	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);

#if RMLUI_PREMULTIPLIED_ALPHA
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#else
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif

	render_state.BeginFrame(viewport_width, viewport_height);

	glClearStencil(0);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	Gfx::projection = Rml::Matrix4f::ProjectOrtho(0, (float)viewport_width, (float)viewport_height, 0, -10000, 10000);

	if (Gfx::render_interface)
		Gfx::render_interface->SetTransform(nullptr);

	Gfx::CheckGLError("BeginFrame");
}

void RmlGL3::EndFrame()
{
	const Gfx::FramebufferData& fb_active = render_state.GetActiveFramebuffer();
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
	glBindTexture(fb_postprocess.tex_color_target, fb_postprocess.tex_color_buffer);
	glUseProgram(Gfx::programs.passthrough.id);
	Gfx::DrawFullscreenQuad();

	render_state.EndFrame();

	Gfx::CheckGLError("EndFrame");
}

void RmlGL3::Clear()
{
	glClearStencil(0);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}
