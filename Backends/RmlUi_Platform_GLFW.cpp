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

#include "RmlUi_Platform_GLFW.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/SystemInterface.h>
#include <GLFW/glfw3.h>

static GLFWwindow* window = nullptr;

static GLFWcursor* cursor_pointer = nullptr;
static GLFWcursor* cursor_cross = nullptr;
static GLFWcursor* cursor_text = nullptr;

static Rml::Context* context = nullptr;
static int window_width = 0;
static int window_height = 0;
static float window_dp_ratio = 1.f;

static constexpr size_t KEYMAP_SIZE = GLFW_KEY_LAST + 1;
static Rml::Input::KeyIdentifier key_identifier_map[KEYMAP_SIZE];
static int active_modifiers = 0;

static void SetupCallbacks();
static void InitializeKeyMap();

double SystemInterface_GLFW::GetElapsedTime()
{
	return glfwGetTime();
}

void SystemInterface_GLFW::SetMouseCursor(const Rml::String& cursor_name)
{
	GLFWcursor* cursor = nullptr;

	if (cursor_name.empty() || cursor_name == "arrow")
		cursor = nullptr;
	else if (cursor_name == "move")
		cursor = cursor_pointer;
	else if (cursor_name == "pointer")
		cursor = cursor_pointer;
	else if (cursor_name == "resize")
		cursor = cursor_pointer;
	else if (cursor_name == "cross")
		cursor = cursor_cross;
	else if (cursor_name == "text")
		cursor = cursor_text;
	else if (cursor_name == "unavailable")
		cursor = nullptr;

	glfwSetCursor(window, cursor);
}

void SystemInterface_GLFW::SetClipboardText(const Rml::String& text_utf8)
{
	glfwSetClipboardString(window, text_utf8.c_str());
}

void SystemInterface_GLFW::GetClipboardText(Rml::String& text)
{
	text = Rml::String(glfwGetClipboardString(window));
}

static void LogErrorFromGLFW(int error, const char* description)
{
	Rml::Log::Message(Rml::Log::LT_ERROR, "GLFW error (0x%x): %s", error, description);
}

bool RmlGLFW::Initialize()
{
	InitializeKeyMap();
	glfwSetErrorCallback(LogErrorFromGLFW);

	if (glfwInit() == GLFW_TRUE)
	{
		cursor_pointer = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
		cursor_cross = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
		cursor_text = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);

		return true;
	}

	return false;
}

void RmlGLFW::Shutdown()
{
	glfwDestroyCursor(cursor_pointer);
	glfwDestroyCursor(cursor_cross);
	glfwDestroyCursor(cursor_text);

	cursor_pointer = nullptr;
	cursor_cross = nullptr;
	cursor_text = nullptr;

	glfwTerminate();
}

void RmlGLFW::SetContext(Rml::Context* new_context)
{
	context = new_context;

	ProcessFramebufferSizeCallback(0, 0);
	ProcessContentScaleCallback(0.f);
}

bool RmlGLFW::ProcessKeyCallback(int key, int action, int mods)
{
	SetActiveModifiers(mods);
	if (!context)
		return true;

	bool result = true;

	switch (action)
	{
	case GLFW_PRESS:
	case GLFW_REPEAT:
		result = context->ProcessKeyDown(RmlGLFW::ConvertKey(key), RmlGLFW::ConvertKeyModifiers(mods));
		if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
			result &= context->ProcessTextInput('\n');
		break;
	case GLFW_RELEASE:
		result = context->ProcessKeyUp(RmlGLFW::ConvertKey(key), RmlGLFW::ConvertKeyModifiers(mods));
		break;
	}

	return result;
}
bool RmlGLFW::ProcessCharCallback(unsigned int codepoint)
{
	if (!context)
		return true;

	bool result = context->ProcessTextInput((Rml::Character)codepoint);
	return result;
}

bool RmlGLFW::ProcessCursorPosCallback(double xpos, double ypos)
{
	if (!context)
		return true;

	bool result = context->ProcessMouseMove(int(xpos), int(ypos), RmlGLFW::ConvertKeyModifiers(active_modifiers));
	return result;
}

bool RmlGLFW::ProcessMouseButtonCallback(int button, int action, int mods)
{
	SetActiveModifiers(mods);
	if (!context)
		return true;

	bool result = true;

	switch (action)
	{
	case GLFW_PRESS:
		result = context->ProcessMouseButtonDown(button, RmlGLFW::ConvertKeyModifiers(mods));
		break;
	case GLFW_RELEASE:
		result = context->ProcessMouseButtonUp(button, RmlGLFW::ConvertKeyModifiers(mods));
		break;
	}
	return result;
}

bool RmlGLFW::ProcessScrollCallback(double yoffset)
{
	if (!context)
		return true;

	bool result = context->ProcessMouseWheel(-float(yoffset), RmlGLFW::ConvertKeyModifiers(active_modifiers));
	return result;
}

void RmlGLFW::ProcessFramebufferSizeCallback(int width, int height)
{
	if (width > 0)
		window_width = width;
	if (height > 0)
		window_height = height;

	if (context)
		context->SetDimensions(Rml::Vector2i(window_width, window_height));
}

void RmlGLFW::ProcessContentScaleCallback(float xscale)
{
	if (xscale > 0.f)
		window_dp_ratio = xscale;

	if (context)
		context->SetDensityIndependentPixelRatio(window_dp_ratio);
}
void RmlGLFW::SetActiveModifiers(int mods)
{
	active_modifiers = mods;
}

bool RmlGLFW::CreateWindow(const char* name, int& inout_width, int& inout_height, bool allow_resize, GLFWwindow*& out_window)
{
	RMLUI_ASSERTMSG(!window, "Cannot create multiple windows.");

	glfwWindowHint(GLFW_RESIZABLE, allow_resize ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

	window = glfwCreateWindow(inout_width, inout_height, name, nullptr, nullptr);
	if (!window)
		return false;

	out_window = window;

	// The window size may have been scaled by dpi settings, get the actual pixel size and the dp-ratio.
	glfwGetFramebufferSize(window, &inout_width, &inout_height);
	ProcessFramebufferSizeCallback(inout_width, inout_height);

	float dp_ratio = 1.f;
	glfwGetWindowContentScale(window, &dp_ratio, nullptr);
	ProcessContentScaleCallback(dp_ratio);

	SetupCallbacks();

	return true;
}

void RmlGLFW::CloseWindow()
{
	glfwDestroyWindow(window);

	window = nullptr;

	window_dp_ratio = 1.f;
	window_width = 0;
	window_height = 0;
	active_modifiers = 0;
}

Rml::Input::KeyIdentifier RmlGLFW::ConvertKey(int glfw_key)
{
	if ((size_t)glfw_key < KEYMAP_SIZE)
		return key_identifier_map[glfw_key];

	return Rml::Input::KI_UNKNOWN;
}

int RmlGLFW::ConvertKeyModifiers(int glfw_mods)
{
	int key_modifier_state = 0;

	if (GLFW_MOD_SHIFT & glfw_mods)
		key_modifier_state |= Rml::Input::KM_SHIFT;

	if (GLFW_MOD_CONTROL & glfw_mods)
		key_modifier_state |= Rml::Input::KM_CTRL;

	if (GLFW_MOD_ALT & glfw_mods)
		key_modifier_state |= Rml::Input::KM_ALT;

	if (GLFW_MOD_CAPS_LOCK & glfw_mods)
		key_modifier_state |= Rml::Input::KM_SCROLLLOCK;

	if (GLFW_MOD_NUM_LOCK & glfw_mods)
		key_modifier_state |= Rml::Input::KM_NUMLOCK;

	return key_modifier_state;
}

static void SetupCallbacks()
{
	// Key input
	glfwSetKeyCallback(window,
		[](GLFWwindow* /*window*/, int key, int /*scancode*/, int action, int mods) { RmlGLFW::ProcessKeyCallback(key, action, mods); });

	glfwSetCharCallback(window, [](GLFWwindow* /*window*/, unsigned int codepoint) { RmlGLFW::ProcessCharCallback(codepoint); });

	// Mouse input
	glfwSetCursorPosCallback(window, [](GLFWwindow* /*window*/, double xpos, double ypos) { RmlGLFW::ProcessCursorPosCallback(xpos, ypos); });

	glfwSetMouseButtonCallback(window,
		[](GLFWwindow* /*window*/, int button, int action, int mods) { RmlGLFW::ProcessMouseButtonCallback(button, action, mods); });

	glfwSetScrollCallback(window, [](GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) { RmlGLFW::ProcessScrollCallback(yoffset); });

	// Window events
	glfwSetFramebufferSizeCallback(window,
		[](GLFWwindow* /*window*/, int width, int height) { RmlGLFW::ProcessFramebufferSizeCallback(width, height); });

	glfwSetWindowContentScaleCallback(window,
		[](GLFWwindow* /*window*/, float xscale, float /*yscale*/) { RmlGLFW::ProcessContentScaleCallback(xscale); });
}

static void InitializeKeyMap()
{
	memset(key_identifier_map, 0, sizeof(key_identifier_map));

	key_identifier_map[GLFW_KEY_A] = Rml::Input::KI_A;
	key_identifier_map[GLFW_KEY_B] = Rml::Input::KI_B;
	key_identifier_map[GLFW_KEY_C] = Rml::Input::KI_C;
	key_identifier_map[GLFW_KEY_D] = Rml::Input::KI_D;
	key_identifier_map[GLFW_KEY_E] = Rml::Input::KI_E;
	key_identifier_map[GLFW_KEY_F] = Rml::Input::KI_F;
	key_identifier_map[GLFW_KEY_G] = Rml::Input::KI_G;
	key_identifier_map[GLFW_KEY_H] = Rml::Input::KI_H;
	key_identifier_map[GLFW_KEY_I] = Rml::Input::KI_I;
	key_identifier_map[GLFW_KEY_J] = Rml::Input::KI_J;
	key_identifier_map[GLFW_KEY_K] = Rml::Input::KI_K;
	key_identifier_map[GLFW_KEY_L] = Rml::Input::KI_L;
	key_identifier_map[GLFW_KEY_M] = Rml::Input::KI_M;
	key_identifier_map[GLFW_KEY_N] = Rml::Input::KI_N;
	key_identifier_map[GLFW_KEY_O] = Rml::Input::KI_O;
	key_identifier_map[GLFW_KEY_P] = Rml::Input::KI_P;
	key_identifier_map[GLFW_KEY_Q] = Rml::Input::KI_Q;
	key_identifier_map[GLFW_KEY_R] = Rml::Input::KI_R;
	key_identifier_map[GLFW_KEY_S] = Rml::Input::KI_S;
	key_identifier_map[GLFW_KEY_T] = Rml::Input::KI_T;
	key_identifier_map[GLFW_KEY_U] = Rml::Input::KI_U;
	key_identifier_map[GLFW_KEY_V] = Rml::Input::KI_V;
	key_identifier_map[GLFW_KEY_W] = Rml::Input::KI_W;
	key_identifier_map[GLFW_KEY_X] = Rml::Input::KI_X;
	key_identifier_map[GLFW_KEY_Y] = Rml::Input::KI_Y;
	key_identifier_map[GLFW_KEY_Z] = Rml::Input::KI_Z;

	key_identifier_map[GLFW_KEY_0] = Rml::Input::KI_0;
	key_identifier_map[GLFW_KEY_1] = Rml::Input::KI_1;
	key_identifier_map[GLFW_KEY_2] = Rml::Input::KI_2;
	key_identifier_map[GLFW_KEY_3] = Rml::Input::KI_3;
	key_identifier_map[GLFW_KEY_4] = Rml::Input::KI_4;
	key_identifier_map[GLFW_KEY_5] = Rml::Input::KI_5;
	key_identifier_map[GLFW_KEY_6] = Rml::Input::KI_6;
	key_identifier_map[GLFW_KEY_7] = Rml::Input::KI_7;
	key_identifier_map[GLFW_KEY_8] = Rml::Input::KI_8;
	key_identifier_map[GLFW_KEY_9] = Rml::Input::KI_9;

	key_identifier_map[GLFW_KEY_BACKSPACE] = Rml::Input::KI_BACK;
	key_identifier_map[GLFW_KEY_TAB] = Rml::Input::KI_TAB;

	key_identifier_map[GLFW_KEY_ENTER] = Rml::Input::KI_RETURN;

	key_identifier_map[GLFW_KEY_PAUSE] = Rml::Input::KI_PAUSE;
	key_identifier_map[GLFW_KEY_CAPS_LOCK] = Rml::Input::KI_CAPITAL;

	key_identifier_map[GLFW_KEY_ESCAPE] = Rml::Input::KI_ESCAPE;

	key_identifier_map[GLFW_KEY_SPACE] = Rml::Input::KI_SPACE;
	key_identifier_map[GLFW_KEY_PAGE_UP] = Rml::Input::KI_PRIOR;
	key_identifier_map[GLFW_KEY_PAGE_DOWN] = Rml::Input::KI_NEXT;
	key_identifier_map[GLFW_KEY_END] = Rml::Input::KI_END;
	key_identifier_map[GLFW_KEY_HOME] = Rml::Input::KI_HOME;
	key_identifier_map[GLFW_KEY_LEFT] = Rml::Input::KI_LEFT;
	key_identifier_map[GLFW_KEY_UP] = Rml::Input::KI_UP;
	key_identifier_map[GLFW_KEY_RIGHT] = Rml::Input::KI_RIGHT;
	key_identifier_map[GLFW_KEY_DOWN] = Rml::Input::KI_DOWN;
	key_identifier_map[GLFW_KEY_PRINT_SCREEN] = Rml::Input::KI_SNAPSHOT;
	key_identifier_map[GLFW_KEY_INSERT] = Rml::Input::KI_INSERT;
	key_identifier_map[GLFW_KEY_DELETE] = Rml::Input::KI_DELETE;

	key_identifier_map[GLFW_KEY_LEFT_SUPER] = Rml::Input::KI_LWIN;
	key_identifier_map[GLFW_KEY_RIGHT_SUPER] = Rml::Input::KI_RWIN;

	key_identifier_map[GLFW_KEY_KP_0] = Rml::Input::KI_NUMPAD0;
	key_identifier_map[GLFW_KEY_KP_1] = Rml::Input::KI_NUMPAD1;
	key_identifier_map[GLFW_KEY_KP_2] = Rml::Input::KI_NUMPAD2;
	key_identifier_map[GLFW_KEY_KP_3] = Rml::Input::KI_NUMPAD3;
	key_identifier_map[GLFW_KEY_KP_4] = Rml::Input::KI_NUMPAD4;
	key_identifier_map[GLFW_KEY_KP_5] = Rml::Input::KI_NUMPAD5;
	key_identifier_map[GLFW_KEY_KP_6] = Rml::Input::KI_NUMPAD6;
	key_identifier_map[GLFW_KEY_KP_7] = Rml::Input::KI_NUMPAD7;
	key_identifier_map[GLFW_KEY_KP_8] = Rml::Input::KI_NUMPAD8;
	key_identifier_map[GLFW_KEY_KP_9] = Rml::Input::KI_NUMPAD9;
	key_identifier_map[GLFW_KEY_KP_ENTER] = Rml::Input::KI_NUMPADENTER;
	key_identifier_map[GLFW_KEY_KP_MULTIPLY] = Rml::Input::KI_MULTIPLY;
	key_identifier_map[GLFW_KEY_KP_ADD] = Rml::Input::KI_ADD;
	key_identifier_map[GLFW_KEY_KP_SUBTRACT] = Rml::Input::KI_SUBTRACT;
	key_identifier_map[GLFW_KEY_KP_DECIMAL] = Rml::Input::KI_DECIMAL;
	key_identifier_map[GLFW_KEY_KP_DIVIDE] = Rml::Input::KI_DIVIDE;

	key_identifier_map[GLFW_KEY_F1] = Rml::Input::KI_F1;
	key_identifier_map[GLFW_KEY_F2] = Rml::Input::KI_F2;
	key_identifier_map[GLFW_KEY_F3] = Rml::Input::KI_F3;
	key_identifier_map[GLFW_KEY_F4] = Rml::Input::KI_F4;
	key_identifier_map[GLFW_KEY_F5] = Rml::Input::KI_F5;
	key_identifier_map[GLFW_KEY_F6] = Rml::Input::KI_F6;
	key_identifier_map[GLFW_KEY_F7] = Rml::Input::KI_F7;
	key_identifier_map[GLFW_KEY_F8] = Rml::Input::KI_F8;
	key_identifier_map[GLFW_KEY_F9] = Rml::Input::KI_F9;
	key_identifier_map[GLFW_KEY_F10] = Rml::Input::KI_F10;
	key_identifier_map[GLFW_KEY_F11] = Rml::Input::KI_F11;
	key_identifier_map[GLFW_KEY_F12] = Rml::Input::KI_F12;
	key_identifier_map[GLFW_KEY_F13] = Rml::Input::KI_F13;
	key_identifier_map[GLFW_KEY_F14] = Rml::Input::KI_F14;
	key_identifier_map[GLFW_KEY_F15] = Rml::Input::KI_F15;
	key_identifier_map[GLFW_KEY_F16] = Rml::Input::KI_F16;
	key_identifier_map[GLFW_KEY_F17] = Rml::Input::KI_F17;
	key_identifier_map[GLFW_KEY_F18] = Rml::Input::KI_F18;
	key_identifier_map[GLFW_KEY_F19] = Rml::Input::KI_F19;
	key_identifier_map[GLFW_KEY_F20] = Rml::Input::KI_F20;
	key_identifier_map[GLFW_KEY_F21] = Rml::Input::KI_F21;
	key_identifier_map[GLFW_KEY_F22] = Rml::Input::KI_F22;
	key_identifier_map[GLFW_KEY_F23] = Rml::Input::KI_F23;
	key_identifier_map[GLFW_KEY_F24] = Rml::Input::KI_F24;

	key_identifier_map[GLFW_KEY_NUM_LOCK] = Rml::Input::KI_NUMLOCK;
	key_identifier_map[GLFW_KEY_SCROLL_LOCK] = Rml::Input::KI_SCROLL;

	key_identifier_map[GLFW_KEY_LEFT_SHIFT] = Rml::Input::KI_LSHIFT;
	key_identifier_map[GLFW_KEY_LEFT_CONTROL] = Rml::Input::KI_LCONTROL;
	key_identifier_map[GLFW_KEY_RIGHT_SHIFT] = Rml::Input::KI_RSHIFT;
	key_identifier_map[GLFW_KEY_RIGHT_CONTROL] = Rml::Input::KI_RCONTROL;
	key_identifier_map[GLFW_KEY_MENU] = Rml::Input::KI_LMENU;

	key_identifier_map[GLFW_KEY_KP_EQUAL] = Rml::Input::KI_OEM_NEC_EQUAL;
}