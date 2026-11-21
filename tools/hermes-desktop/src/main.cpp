/*
 * Hermes Agent for desktop (Linux and Windows): one Cell with a transcript, a prompt field and a key
 * dialog. Layout and colours come from the theme (theme.h / hermes-agent.theme), not from numbers
 * in the drawing code beyond the geometry rules below.
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "agent.h"
#include "theme.h"
#include "font_data.h"          /* generated at build time from Inter (OFL) */

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

using namespace hermes;

namespace {

enum class Role { You, Agent, Error };
struct Message { Role role; std::string text; };

struct App
{
	Theme theme;

	std::mutex            lock;
	std::vector<Message>  messages;
	std::atomic<bool>     busy { false };
	std::atomic<bool>     focused { true };
	bool                  scroll_down { false };

	KeyInfo key;
	char    prompt[MAX_PROMPT + 1] { };

	/* key dialog */
	bool    open_key_dialog { false };
	char    key_entry[320] { };
	std::string key_note;
	bool    key_note_error { false };

	GLFWwindow *glfw { nullptr };
};

App g;

std::string exe_dir()
{
#ifdef _WIN32
	char p[MAX_PATH]; DWORD n = GetModuleFileNameA(nullptr, p, MAX_PATH);
	std::string s(p, n); size_t k = s.find_last_of("\\/");
	return k == std::string::npos ? std::string(".") : s.substr(0, k);
#else
	char p[4096]; ssize_t n = readlink("/proc/self/exe", p, sizeof(p) - 1);
	if (n <= 0) return ".";
	p[n] = 0; std::string s(p); size_t k = s.rfind('/');
	return k == std::string::npos ? std::string(".") : s.substr(0, k);
#endif
}

ImU32 col(ImVec4 c, float a = 1.0f) { c.w *= a; return ImGui::ColorConvertFloat4ToU32(c); }

void push(Role r, std::string text)
{
	std::lock_guard<std::mutex> l(g.lock);
	g.messages.push_back({ r, std::move(text) });
	g.scroll_down = true;
}

void send_prompt()
{
	std::string prompt = g.prompt;
	size_t const b = prompt.find_first_not_of(" \t\r\n");
	if (b == std::string::npos || g.busy) return;
	prompt = prompt.substr(b, prompt.find_last_not_of(" \t\r\n") - b + 1);
	g.prompt[0] = 0;

	push(Role::You, prompt);
	g.busy = true;
	std::string const key = g.key.key;
	std::thread([prompt, key] {
		Reply const r = ask(prompt, key);
		push(r.ok ? Role::Agent : Role::Error, r.text);
		g.busy = false;
		glfwPostEmptyEvent();
	}).detach();
}

/* the Cell frame: a chamfered octagon (13.5 % of the shorter side by default), thin border, glow when focused */
struct Cell { ImVec2 pos, size; float chamfer; ImVec2 inner_pos, inner_size; };

Cell cell_geometry(ImVec2 display)
{
	float const margin = 14.0f;
	Cell c;
	c.pos  = { margin, margin };
	c.size = { display.x - 2 * margin, display.y - 2 * margin };
	c.chamfer = std::min(c.size.x, c.size.y) * (float)g.theme.chamfer / 1000.0f;
	/* the inscribed rectangle: strip height on top and bottom, side margin that keeps the corners clear */
	float const strip = 44.0f, border = 16.0f;
	float const side = std::max(border, c.chamfer + border + 6.0f - strip);
	c.inner_pos  = { c.pos.x + side, c.pos.y + strip };
	c.inner_size = { c.size.x - 2 * side, c.size.y - 2 * strip };
	return c;
}

void draw_frame(Cell const &c)
{
	ImDrawList *dl = ImGui::GetBackgroundDrawList();
	bool const on = g.focused;
	float const x = c.pos.x, y = c.pos.y, w = c.size.x, h = c.size.y, k = c.chamfer;
	ImVec2 const p[8] = { { x + k, y }, { x + w - k, y }, { x + w, y + k }, { x + w, y + h - k },
	                      { x + w - k, y + h }, { x + k, y + h }, { x, y + h - k }, { x, y + k } };

	if (on) {                                   /* glow: only the focused Cell has one */
		float const reach = std::min(c.size.x, c.size.y) * (float)g.theme.glow / 10000.0f * 1.2f;
		for (int i = 10; i >= 1; --i) {
			float const t = (float)i / 10.0f;
			float const a = (float)g.theme.glow_alpha / 100.0f * 0.11f * (1.0f - t + 0.15f);
			dl->AddPolyline(p, 8, col(g.theme.accent_deep, a), ImDrawFlags_Closed, reach * t * 2.0f);
		}
	}
	dl->AddConvexPolyFilled(p, 8, col(g.theme.panel));
	/* theme border: 1/10000 of the shorter side, never thinner than 1.5 px */
	float const bw = std::max(1.5f, (float)g.theme.border * std::min(c.size.x, c.size.y) / 10000.0f);
	dl->AddPolyline(p, 8, col(on ? g.theme.accent : g.theme.carbon_edge), ImDrawFlags_Closed, bw);
	if (on) dl->AddPolyline(p, 8, col(g.theme.accent_light, 0.35f), ImDrawFlags_Closed, 1.0f);
}

void draw_key_dialog();

void draw_strip(Cell const &c)
{
	ImGui::SetNextWindowPos(c.pos);
	ImGui::SetNextWindowSize({ c.size.x, 44.0f });
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
	ImGui::Begin("##strip", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                                  ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec4 const ink = g.focused ? g.theme.text : g.theme.carbon_text;
	float const left = c.pos.x + std::max(20.0f, c.chamfer * 0.55f);
	ImGui::SetCursorScreenPos({ left, c.pos.y + 11.0f });
	ImGui::PushStyleColor(ImGuiCol_Text, ink);
	ImGui::TextUnformatted("Hermes Agent");
	ImGui::PopStyleColor();

	/* status chip and the key button, right-aligned and kept clear of the diagonal */
	float const clear = std::max(24.0f, c.chamfer * 0.55f);
	std::string status = g.key.source == KeySource::None ? "no key" :
	                     g.key.source == KeySource::Environment ? "key: environment" :
	                     g.key.source == KeySource::File ? "key: file" : "key: entered";
	status += "  |  TLS verified";
	float const sw = ImGui::CalcTextSize(status.c_str()).x;
	float const bw = 56.0f;
	float const right = c.pos.x + c.size.x - clear;
	ImGui::SetCursorScreenPos({ right - bw, c.pos.y + 7.0f });
	ImGui::PushStyleColor(ImGuiCol_Button, g.theme.bg);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g.focused ? g.theme.accent_deep : g.theme.carbon_edge);
	ImGui::PushStyleColor(ImGuiCol_Text, g.focused ? g.theme.accent : g.theme.carbon);
	if (ImGui::Button("Key", { bw, 26 })) g.open_key_dialog = true;
	ImGui::PopStyleColor(3);
	ImVec4 st = g.key.source == KeySource::None ? g.theme.error : g.theme.muted;
	if (!g.focused) st = g.theme.carbon;
	dl->AddText({ right - bw - 14 - sw, c.pos.y + 13.0f }, col(st), status.c_str());
	draw_key_dialog();                      /* a popup needs a current container; this is the strip's */
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void draw_key_dialog()
{
	if (g.open_key_dialog) { ImGui::OpenPopup("Key"); g.open_key_dialog = false; g.key_note.clear(); g.key_entry[0] = 0; }
	ImVec2 const mid = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(mid, ImGuiCond_Appearing, { 0.5f, 0.5f });
	ImGui::SetNextWindowSize({ 460, 0 });
	if (!ImGui::BeginPopupModal("Key", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) return;

	if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
	ImGui::TextWrapped("The key for the AI service. It is kept in memory while the program runs. Saving it stores one file in your user folder, readable only by you.");
	ImGui::Spacing();
	std::string const where = g.key.source == KeySource::None ? "none" : g.key.where;
	ImGui::PushStyleColor(ImGuiCol_Text, g.theme.muted);
	ImGui::TextWrapped("Current: %s", where.c_str());
	ImGui::TextWrapped("Saved file: %s", default_key_path().c_str());
	ImGui::PopStyleColor();
	ImGui::Spacing();
	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##key", "paste the API key", g.key_entry, sizeof(g.key_entry), ImGuiInputTextFlags_Password);
	if (!g.key_note.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, g.key_note_error ? g.theme.error : g.theme.accent);
		ImGui::TextWrapped("%s", g.key_note.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Spacing();
	if (ImGui::Button("Use for this session")) {
		std::string k = g.key_entry;
		if (key_plausible(k)) { g.key = { k, KeySource::Entered, "entered" }; g.key_note = "Using the entered key."; g.key_note_error = false; g.key_entry[0] = 0; }
		else { g.key_note = "That is not an API key (one token of letters, digits, - _ .; 16 to 256 characters)."; g.key_note_error = true; }
	}
	ImGui::SameLine();
	if (ImGui::Button("Save")) {
		std::string err;
		if (save_key(g.key_entry, err)) { g.key = load_key(); if (g.key.source == KeySource::None) g.key = { g.key_entry, KeySource::Entered, "entered" };
			g.key_note = "Saved."; g.key_note_error = false; g.key_entry[0] = 0; }
		else { g.key_note = err; g.key_note_error = true; }
	}
	ImGui::SameLine();
	if (ImGui::Button("Forget saved key")) {
		std::string err;
		if (remove_key_file(err)) { g.key_note = "Saved key removed."; g.key_note_error = false; g.key = load_key(); }
		else { g.key_note = err; g.key_note_error = true; }
	}
	ImGui::SameLine();
	if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

void draw_content(Cell const &c)
{
	ImGui::SetNextWindowPos(c.inner_pos);
	ImGui::SetNextWindowSize(c.inner_size);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
	ImGui::Begin("##content", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

	float const input_h = 74.0f, gap = 10.0f;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, g.theme.bg);
	ImGui::BeginChild("##transcript", { 0, c.inner_size.y - input_h - gap }, false, ImGuiWindowFlags_AlwaysUseWindowPadding);
	{
		std::lock_guard<std::mutex> l(g.lock);
		if (g.messages.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, g.theme.muted);
			ImGui::TextWrapped("Ask anything. Each prompt is sent on its own to the AI service over a verified TLS connection; nothing is kept on disk.");
			ImGui::PopStyleColor();
		}
		for (Message const &m : g.messages) {
			ImVec4 label = m.role == Role::You ? g.theme.accent : m.role == Role::Error ? g.theme.error : g.theme.carbon_text;
			if (!g.focused && m.role != Role::Error) label = g.theme.carbon;
			ImGui::PushStyleColor(ImGuiCol_Text, label);
			ImGui::TextUnformatted(m.role == Role::You ? "You" : m.role == Role::Error ? "Problem" : "Hermes");
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_Text, g.focused ? g.theme.text : g.theme.carbon_text);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(m.text.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			if (m.role == Role::Agent) {
				ImGui::PushID(&m);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				if (ImGui::SmallButton("Copy")) glfwSetClipboardString(g.glfw, m.text.c_str());
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
			ImGui::Spacing(); ImGui::Spacing();
		}
		if (g.busy) {
			int const dots = 1 + (int)(ImGui::GetTime() * 3.0) % 3;
			ImGui::PushStyleColor(ImGuiCol_Text, g.theme.muted);
			ImGui::TextUnformatted(std::string("Hermes is thinking") .append(dots, '.').c_str());
			ImGui::PopStyleColor();
		}
		if (g.scroll_down || g.busy) { ImGui::SetScrollHereY(1.0f); g.scroll_down = false; }
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::Dummy({ 0, gap - 4 });
	float const send_w = 84.0f;
	ImGui::PushStyleColor(ImGuiCol_FrameBg, g.theme.bg);
	bool const enter = ImGui::InputTextMultiline("##prompt", g.prompt, sizeof(g.prompt),
		{ c.inner_size.x - send_w - 10, input_h - 6 },
		ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
	ImGui::PopStyleColor();
	if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, g.focused ? g.theme.accent_deep : g.theme.carbon_edge);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g.focused ? g.theme.accent : g.theme.carbon);
	ImGui::PushStyleColor(ImGuiCol_Text, g.theme.bg);
	ImGui::BeginDisabled(g.busy);
	bool const click = ImGui::Button("Send", { send_w, input_h - 6 });
	ImGui::EndDisabled();
	ImGui::PopStyleColor(3);
	if ((enter || click) && !g.busy) { send_prompt(); ImGui::SetKeyboardFocusHere(-2); }
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void apply_style()
{
	ImGuiStyle &s = ImGui::GetStyle();
	s.WindowRounding = 0; s.ChildRounding = 0; s.FrameRounding = 3; s.PopupRounding = 4; s.ScrollbarRounding = 3;
	s.FramePadding = { 8, 6 }; s.ItemSpacing = { 8, 6 }; s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.FrameBorderSize = 0;
	ImVec4 *c = s.Colors;
	c[ImGuiCol_Text] = g.theme.text;
	c[ImGuiCol_WindowBg] = g.theme.panel; c[ImGuiCol_PopupBg] = g.theme.panel;
	c[ImGuiCol_FrameBg] = g.theme.bg; c[ImGuiCol_FrameBgHovered] = g.theme.bg; c[ImGuiCol_FrameBgActive] = g.theme.bg;
	c[ImGuiCol_Button] = g.theme.bg; c[ImGuiCol_ButtonHovered] = g.theme.accent_deep; c[ImGuiCol_ButtonActive] = g.theme.accent;
	c[ImGuiCol_ScrollbarBg] = g.theme.bg; c[ImGuiCol_ScrollbarGrab] = g.theme.carbon_edge;
	c[ImGuiCol_ScrollbarGrabHovered] = g.theme.carbon; c[ImGuiCol_ScrollbarGrabActive] = g.theme.accent;
	c[ImGuiCol_TextSelectedBg] = ImVec4(g.theme.accent.x, g.theme.accent.y, g.theme.accent.z, 0.35f);
	c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
	c[ImGuiCol_Border] = g.theme.carbon_edge;
	c[ImGuiCol_TitleBg] = g.theme.bg; c[ImGuiCol_TitleBgActive] = g.theme.bg; c[ImGuiCol_TitleBgCollapsed] = g.theme.bg;
}

} /* anonymous namespace */

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#else
int main()
#endif
{
	if (!glfwInit()) return 1;
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
	g.glfw = glfwCreateWindow(940, 700, "Hermes Agent", nullptr, nullptr);
	if (!g.glfw) { glfwTerminate(); return 1; }
	glfwMakeContextCurrent(g.glfw);
	glfwSwapInterval(1);
	glfwSetWindowSizeLimits(g.glfw, 560, 420, GLFW_DONT_CARE, GLFW_DONT_CARE);
	glfwSetWindowFocusCallback(g.glfw, [] (GLFWwindow *, int f) { g.focused = f != 0; });
	g.focused = glfwGetWindowAttrib(g.glfw, GLFW_FOCUSED) != 0;

	/* theme: defaults, then the per-user file, then the one next to the program */
	{
		std::string const per_user = default_key_path();
		size_t const k = per_user.find_last_of("/\\");
		if (k != std::string::npos) load_theme_file(g.theme, per_user.substr(0, k + 1) + "hermes-agent.theme");
		load_theme_file(g.theme, exe_dir() + "/hermes-agent.theme");
	}
	g.key = load_key();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	float xs = 1.0f, ys = 1.0f;
	glfwGetWindowContentScale(g.glfw, &xs, &ys);
	ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
	io.Fonts->AddFontFromMemoryTTF((void *)font_inter_regular, (int)font_inter_regular_len, 17.0f * xs, &fc);
	apply_style();
	ImGui::GetStyle().ScaleAllSizes(xs);
	ImGui_ImplGlfw_InitForOpenGL(g.glfw, true);
	ImGui_ImplOpenGL3_Init("#version 130");

	while (!glfwWindowShouldClose(g.glfw)) {
		glfwWaitEventsTimeout(g.busy ? 0.05 : 0.25);
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImVec2 const display = io.DisplaySize;
		Cell const c = cell_geometry(display);
		draw_frame(c);
		draw_content(c);
		draw_strip(c);

		ImGui::Render();
		int w, h; glfwGetFramebufferSize(g.glfw, &w, &h);
		glViewport(0, 0, w, h);
		glClearColor(g.theme.bg.x, g.theme.bg.y, g.theme.bg.z, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(g.glfw);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(g.glfw);
	glfwTerminate();
	std::_Exit(0);          /* a request may still be in flight on a worker thread */
}
