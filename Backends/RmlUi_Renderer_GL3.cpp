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

static int viewport_width = 0;
static int viewport_height = 0;

static const char* shader_main_vertex = RMLUI_SHADER_HEADER R"(
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
	vec4 outPos = _transform * vec4(translatedPos, 0, 1);

    gl_Position = outPos;
}
)";
static const char* shader_main_fragment_texture = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);

#if RMLUI_PREMULTIPLIED_ALPHA
	// Pre-multiply texure colors with their alpha.
	texColor.rgb = texColor.rgb * texColor.a;
#endif

	finalColor = fragColor * texColor;
}
)";
static const char* shader_main_fragment_color = RMLUI_SHADER_HEADER R"(
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	finalColor = fragColor;
}
)";

static const char* shader_postprocess_vertex = RMLUI_SHADER_HEADER R"(
in vec2 inPosition;
in vec2 inTexCoord0;

out vec2 fragTexCoord;

void main() {
	fragTexCoord = inTexCoord0;
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
)";
static const char* shader_postprocess_fragment = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	finalColor = texColor;
}
)";

namespace Gfx {

enum class ProgramUniform { Translate, Transform, Tex, Count };
static const char* const program_uniform_names[(size_t)ProgramUniform::Count] = {"_translate", "_transform", "_tex"};

enum class VertexAttribute { Position, Color0, TexCoord0, Count };
static const char* const vertex_attribute_names[(size_t)VertexAttribute::Count] = {"inPosition", "inColor0", "inTexCoord0"};

struct CompiledGeometryData {
	Rml::TextureHandle texture;
	GLuint vao;
	GLuint vbo;
	GLuint ibo;
	GLsizei draw_count;
};

struct ProgramData {
	GLuint id;
	GLint uniform_locations[(size_t)ProgramUniform::Count];
};

struct ShadersData {
	ProgramData program_color;
	ProgramData program_texture;
	ProgramData program_postprocess;
	GLuint shader_main_vertex;
	GLuint shader_main_fragment_color;
	GLuint shader_main_fragment_texture;
	GLuint shader_postprocess_vertex;
	GLuint shader_postprocess_fragment;
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

static ShadersData shaders_data = {};
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

// Create the shader, 'shader_type' is either GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
static bool CreateShader(GLuint& out_shader_id, GLenum shader_type, const char* code_string)
{
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
	constexpr GLenum color_format = GL_RGBA8;   // GL_RGBA8 GL_SRGB8_ALPHA8 GL_RGBA16F
	constexpr GLint min_mag_filter = GL_LINEAR; // GL_NEAREST
	constexpr GLint wrap_mode = GL_REPEAT;      // GL_MIRRORED_REPEAT GL_CLAMP_TO_EDGE
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

static bool CreateShaders(ShadersData& out_data)
{
	auto ReportError = [](const char* type, const char* name) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Could not create OpenGL %s: '%s'.", type, name);
		return false;
	};

	out_data = {};

	if (!CreateShader(out_data.shader_main_vertex, GL_VERTEX_SHADER, shader_main_vertex))
		return ReportError("shader", "main_vertex");

	if (!CreateShader(out_data.shader_main_fragment_color, GL_FRAGMENT_SHADER, shader_main_fragment_color))
		return ReportError("shader", "main_fragment_color");

	if (!CreateShader(out_data.shader_main_fragment_texture, GL_FRAGMENT_SHADER, shader_main_fragment_texture))
		return ReportError("shader", "main_fragment_texture");

	if (!CreateShader(out_data.shader_postprocess_vertex, GL_VERTEX_SHADER, shader_postprocess_vertex))
		return ReportError("shader", "postprocess_vertex");

	if (!CreateShader(out_data.shader_postprocess_fragment, GL_FRAGMENT_SHADER, shader_postprocess_fragment))
		return ReportError("shader", "postprocess_fragment");

	if (!CreateProgram(out_data.program_color, out_data.shader_main_vertex, out_data.shader_main_fragment_color))
		return ReportError("program", "color");

	if (!CreateProgram(out_data.program_texture, out_data.shader_main_vertex, out_data.shader_main_fragment_texture))
		return ReportError("program", "texture");

	if (!CreateProgram(out_data.program_postprocess, out_data.shader_postprocess_vertex, out_data.shader_postprocess_fragment))
		return ReportError("program", "postprocess");

	return true;
}

static void DrawFullscreenQuad()
{
	// Draw a fullscreen quad.
	Rml::Vertex vertices[4];
	int indices[6];
	Rml::GeometryUtilities::GenerateQuad(vertices, indices, Rml::Vector2f(-1), Rml::Vector2f(2), {});
	render_interface->RenderGeometry(vertices, 4, indices, 6, RenderInterface_GL3::TexturePostprocess, {});
}

static void DestroyShaders(ShadersData& shaders)
{
	glDeleteProgram(shaders.program_color.id);
	glDeleteProgram(shaders.program_texture.id);
	glDeleteProgram(shaders.program_postprocess.id);

	glDeleteShader(shaders.shader_main_vertex);
	glDeleteShader(shaders.shader_main_fragment_color);
	glDeleteShader(shaders.shader_main_fragment_texture);
	glDeleteShader(shaders.shader_postprocess_vertex);
	glDeleteShader(shaders.shader_postprocess_fragment);

	shaders = {};
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
		glUseProgram(Gfx::shaders_data.program_texture.id);

		if (geometry->texture != TextureIgnoreBinding)
			glBindTexture(GL_TEXTURE_2D, (GLuint)geometry->texture);

		SubmitTransformUniform(ProgramId::Texture, Gfx::shaders_data.program_texture.uniform_locations[(size_t)Gfx::ProgramUniform::Transform]);
		glUniform2fv(Gfx::shaders_data.program_texture.uniform_locations[(size_t)Gfx::ProgramUniform::Translate], 1, &translation.x);
	}
	else
	{
		glUseProgram(Gfx::shaders_data.program_color.id);
		glBindTexture(GL_TEXTURE_2D, 0);
		SubmitTransformUniform(ProgramId::Color, Gfx::shaders_data.program_color.uniform_locations[(size_t)Gfx::ProgramUniform::Transform]);
		glUniform2fv(Gfx::shaders_data.program_color.uniform_locations[(size_t)Gfx::ProgramUniform::Translate], 1, &translation.x);
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
			{
				const int alpha = image_src[read_index + 3];
#ifdef RMLUI_PREMULTIPLIED_ALPHA_COMPUTE
				image_dest[write_index + 0] = (image_dest[write_index + 0] * alpha) / 255;
				image_dest[write_index + 1] = (image_dest[write_index + 1] * alpha) / 255;
				image_dest[write_index + 2] = (image_dest[write_index + 2] * alpha) / 255;
#endif
				image_dest[write_index + 3] = (byte)alpha;
			}
			else
			{
				image_dest[write_index + 3] = 255;
			}

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

	const Gfx::FramebufferData& GetPostprocessMain() const { return fb_postprocess_main; }
	const Gfx::FramebufferData& GetPostprocessSecondary() const { return fb_postprocess_secondary; }

	void BeginFrame(int new_width, int new_height)
	{
		RMLUI_ASSERT(fb_stack_size == 0);

		if (new_width != width || new_height != height)
		{
			width = new_width;
			height = new_height;

			DestroyFramebuffers();
			Gfx::CreateFramebuffer(fb_postprocess_main, width, height, 0, Gfx::FramebufferAttachment::None, 0);
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

		Gfx::DestroyFramebuffer(fb_postprocess_main);
		Gfx::DestroyFramebuffer(fb_postprocess_secondary);
	}

	int width = 0, height = 0;

	int fb_stack_size = 0;
	Rml::Vector<Gfx::FramebufferData> fb_stack;

	Gfx::FramebufferData fb_postprocess_main = {};
	Gfx::FramebufferData fb_postprocess_secondary = {};
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
		const Gfx::FramebufferData& destination = render_state.GetPostprocessMain();
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
		EnableScissorRegion(true);
		SetScissorRegion(offset.x, offset.y, dimensions.x, dimensions.y);

		const Gfx::FramebufferData& source = render_state.GetActiveFramebuffer();
		const Gfx::FramebufferData& destination = render_state.GetPostprocessMain();
		glBindFramebuffer(GL_READ_FRAMEBUFFER, source.framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);
		glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, destination.width, destination.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	break;
	case Rml::RenderCommand::FilterToStack:
	{
		const Gfx::FramebufferData& source = render_state.GetPostprocessMain();
		const Gfx::FramebufferData& destination = render_state.GetActiveFramebuffer();

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(source.tex_color_target, source.tex_color_buffer);
		glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);
		glUseProgram(Gfx::shaders_data.program_postprocess.id);
		Gfx::DrawFullscreenQuad();

		EnableScissorRegion(pre_filter_scissor_state.enabled);
		SetScissorRegion(pre_filter_scissor_state.x, pre_filter_scissor_state.y, pre_filter_scissor_state.width, pre_filter_scissor_state.height);
	}
	break;
	case Rml::RenderCommand::None:
		break;
	}

	return texture_handle;
}

Rml::CompiledEffectHandle RenderInterface_GL3::CompileEffect(const Rml::String& name, const Rml::Dictionary& parameters)
{
	return Rml::CompiledEffectHandle();
}

Rml::TextureHandle RenderInterface_GL3::RenderEffect(Rml::CompiledEffectHandle effect, Rml::CompiledGeometryHandle geometry,
	Rml::Vector2f translation)
{
	return Rml::TextureHandle();
}

void RenderInterface_GL3::ReleaseCompiledEffect(Rml::CompiledEffectHandle effect) {}

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

	if (!Gfx::CreateShaders(Gfx::shaders_data))
		return false;

	return true;
}

void RmlGL3::Shutdown()
{
	render_state.Shutdown();

	Gfx::DestroyShaders(Gfx::shaders_data);

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
	const Gfx::FramebufferData& fb_postprocess_main = render_state.GetPostprocessMain();

	// Resolve MSAA to postprocess framebuffer.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_active.framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_postprocess_main.framebuffer);

	glBlitFramebuffer(0, 0, fb_active.width, fb_active.height, 0, 0, fb_postprocess_main.width, fb_postprocess_main.height, GL_COLOR_BUFFER_BIT,
		GL_NEAREST);

	// Draw to backbuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	// Assuming we have an opaque background, we can just write to it with the premultiplied alpha blend mode and we'll get the correct result.
	// Instead, if we had a transparent destination that didn't use pre-multiplied alpha, we would have to perform a manual un-premultiplication step.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(fb_postprocess_main.tex_color_target, fb_postprocess_main.tex_color_buffer);
	glUseProgram(Gfx::shaders_data.program_postprocess.id);
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
