#include "pch.h"
#include ".\UI\Theme.h"
#include ".\UI\Option.h"
#include ".\Damage Meter\Damage Meter.h"
#include <dwmapi.h>
#include <shellapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

#define THEME_FILE_ROOT "SoulMeterTheme"
#define THEME_ELEMENT "Theme"

static ImVec4 Rgb(int r, int g, int b, float a = 1.0f)
{
	return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

static ImVec4 Mix(const ImVec4& a, const ImVec4& b, float f)
{
	return ImVec4(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, a.w + (b.w - a.w) * f);
}

static ImVec4 Alpha(ImVec4 c, float a)
{
	c.w = a;
	return c;
}

static const ImVec4 kJobColors[THEME_JOB_COUNT] = {
	Rgb(153, 153, 153),	// Unknown
	Rgb(247, 142, 59),	// haru
	Rgb(59, 147, 247),	// owin
	Rgb(247, 59, 156),	// lily
	Rgb(247, 190, 59),	// kin
	Rgb(161, 59, 247),	// stella
	Rgb(223, 1, 1),		// iris
	Rgb(138, 2, 4),		// chii
	Rgb(118, 206, 158),	// eph
	Rgb(128, 128, 64),	// nabi
	Rgb(65, 40, 154),	// dhana
};

// What option.xml stores: the file name with its real extension (.ttf or .ttc).
static std::string FontFileName(const ImFontObj& font)
{
	size_t slash = font.path.find_last_of('/');
	return slash == std::string::npos ? font.path : font.path.substr(slash + 1);
}

static void ColorToHex(const ImVec4& c, char* out, size_t len)
{
	auto b = [](float v) { return (int)(ImSaturate(v) * 255.0f + 0.5f); };
	sprintf_s(out, len, "#%02X%02X%02X%02X", b(c.x), b(c.y), b(c.z), b(c.w));
}

static bool HexToColor(const char* hex, ImVec4& out)
{
	if (hex == nullptr || hex[0] != '#')
		return false;

	unsigned int r, g, b, a = 255;
	size_t len = strlen(hex + 1);
	if (len == 8) {
		if (sscanf_s(hex + 1, "%2x%2x%2x%2x", &r, &g, &b, &a) != 4)
			return false;
	}
	else if (len == 6) {
		if (sscanf_s(hex + 1, "%2x%2x%2x", &r, &g, &b) != 3)
			return false;
	}
	else
		return false;

	out = ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
	return true;
}

// Keeps hand-edited or pasted themes from producing unusable values.
static void Sanitize(MeterTheme& t)
{
	t.name[THEME_NAME_LEN - 1] = 0;
	t.windowOpacity = ImClamp(t.windowOpacity, 0.2f, 1.0f);
	t.windowRounding = ImClamp(t.windowRounding, 0.0f, 16.0f);
	t.frameRounding = ImClamp(t.frameRounding, 0.0f, 12.0f);
	t.windowBorderSize = ImClamp(t.windowBorderSize, 0.0f, 1.0f);
	t.cellPadding.x = ImClamp(t.cellPadding.x, 0.0f, 20.0f);
	t.cellPadding.y = ImClamp(t.cellPadding.y, 0.0f, 20.0f);
	t.barStyle = ImClamp(t.barStyle, 0, THEME_BAR_COUNT - 1);
	t.barOpacity = ImClamp(t.barOpacity, 0.0f, 1.0f);
	t.barHeight = ImClamp(t.barHeight, 0.1f, 1.0f);
	t.barRounding = ImClamp(t.barRounding, 0.0f, 12.0f);
	t.textEffect = ImClamp(t.textEffect, 0, THEME_TEXT_COUNT - 1);
	t.outlineSize = ImClamp(t.outlineSize, 0.5f, 3.0f);
	t.fontScale = ImClamp(t.fontScale, 0.3f, 2.0f);
	t.columnFontScale = ImClamp(t.columnFontScale, 0.3f, 2.0f);
	t.tableFontScale = ImClamp(t.tableFontScale, 0.3f, 2.0f);
}

// Characters Windows refuses in file names.
static std::string ThemeFileStem(const char* name)
{
	std::string stem(name);
	for (char& c : stem) {
		if (strchr("<>:\"/\\|?*", c) != nullptr || (unsigned char)c < 32)
			c = '_';
	}
	while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
		stem.pop_back();
	return stem;
}

static std::filesystem::path ThemePath(const char* name)
{
	return std::filesystem::u8path(std::string(THEME_FOLDER) + ThemeFileStem(name) + ".xml");
}

ThemeManager::ThemeManager()
{
	MakeDefault(_theme);
	BuildPresets();
	RefreshSavedThemes();
}

ThemeManager::~ThemeManager()
{

}

void ThemeManager::MakeDefault(MeterTheme& t)
{
	t = MeterTheme();
	strcpy_s(t.name, "Default");

	ImGuiStyle style;
	ImGui::StyleColorsDark(&style);
	memcpy(t.colors, style.Colors, sizeof(t.colors));

	// Stock imgui is 94% here, which never showed before windows could be
	// see-through. Opaque keeps the default looking like it always has.
	t.colors[ImGuiCol_WindowBg].w = 1.0f;
	t.colors[ImGuiCol_PopupBg].w = 1.0f;

	t.activeColor = t.colors[ImGuiCol_TitleBgActive];
	t.inactiveColor = t.colors[ImGuiCol_TitleBg];
	t.outlineColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	t.aggroColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	t.aggroOwnerColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
	t.awakeningColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	t.barTrackColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

	for (int i = 0; i < THEME_JOB_COUNT; i++)
		t.jobColors[i] = kJobColors[i];
}

// A dark palette derived from three colors; the named presets only differ in these.
static void MakeAccent(MeterTheme& t, const char* name, ImVec4 bg, ImVec4 fg, ImVec4 accent, float bgAlpha)
{
	ThemeManager::MakeDefault(t);
	strcpy_s(t.name, name);

	const ImVec4 black(0.0f, 0.0f, 0.0f, 1.0f);
	const ImVec4 panel = Mix(bg, fg, 0.08f);
	const ImVec4 panelHi = Mix(bg, fg, 0.18f);
	ImVec4* c = t.colors;

	c[ImGuiCol_Text] = fg;
	c[ImGuiCol_TextDisabled] = Mix(fg, bg, 0.5f);
	c[ImGuiCol_WindowBg] = Alpha(bg, bgAlpha);
	c[ImGuiCol_ChildBg] = Alpha(bg, 0.0f);
	c[ImGuiCol_PopupBg] = Alpha(Mix(bg, fg, 0.04f), 0.97f);
	c[ImGuiCol_Border] = Alpha(Mix(bg, fg, 0.25f), 0.6f);
	c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_FrameBg] = Alpha(panel, 0.9f);
	c[ImGuiCol_FrameBgHovered] = Alpha(accent, 0.35f);
	c[ImGuiCol_FrameBgActive] = Alpha(accent, 0.6f);
	c[ImGuiCol_TitleBg] = Alpha(Mix(bg, black, 0.3f), ImMin(1.0f, bgAlpha + 0.2f));
	c[ImGuiCol_TitleBgActive] = Alpha(Mix(bg, accent, 0.45f), ImMin(1.0f, bgAlpha + 0.2f));
	c[ImGuiCol_TitleBgCollapsed] = Alpha(bg, 0.6f);
	c[ImGuiCol_MenuBarBg] = panel;
	c[ImGuiCol_ScrollbarBg] = Alpha(bg, 0.4f);
	c[ImGuiCol_ScrollbarGrab] = panelHi;
	c[ImGuiCol_ScrollbarGrabHovered] = Mix(panelHi, accent, 0.4f);
	c[ImGuiCol_ScrollbarGrabActive] = accent;
	c[ImGuiCol_CheckMark] = accent;
	c[ImGuiCol_SliderGrab] = accent;
	c[ImGuiCol_SliderGrabActive] = Mix(accent, fg, 0.3f);
	c[ImGuiCol_Button] = Alpha(accent, 0.35f);
	c[ImGuiCol_ButtonHovered] = Alpha(accent, 0.7f);
	c[ImGuiCol_ButtonActive] = accent;
	c[ImGuiCol_Header] = Alpha(accent, 0.3f);
	c[ImGuiCol_HeaderHovered] = Alpha(accent, 0.55f);
	c[ImGuiCol_HeaderActive] = Alpha(accent, 0.8f);
	c[ImGuiCol_Separator] = c[ImGuiCol_Border];
	c[ImGuiCol_SeparatorHovered] = Alpha(accent, 0.7f);
	c[ImGuiCol_SeparatorActive] = accent;
	c[ImGuiCol_ResizeGrip] = Alpha(accent, 0.2f);
	c[ImGuiCol_ResizeGripHovered] = Alpha(accent, 0.6f);
	c[ImGuiCol_ResizeGripActive] = Alpha(accent, 0.9f);
	c[ImGuiCol_Tab] = Mix(bg, accent, 0.2f);
	c[ImGuiCol_TabHovered] = Alpha(accent, 0.7f);
	c[ImGuiCol_TabSelected] = Mix(bg, accent, 0.5f);
	c[ImGuiCol_TabSelectedOverline] = accent;
	c[ImGuiCol_TabDimmed] = Mix(bg, accent, 0.1f);
	c[ImGuiCol_TabDimmedSelected] = Mix(bg, accent, 0.3f);
	c[ImGuiCol_TabDimmedSelectedOverline] = Alpha(accent, 0.0f);
	c[ImGuiCol_CheckboxSelectedBg] = Mix(c[ImGuiCol_FrameBg], c[ImGuiCol_FrameBgHovered], 0.65f);
	c[ImGuiCol_InputTextCursor] = fg;
	c[ImGuiCol_TextLink] = accent;
	c[ImGuiCol_TreeLines] = c[ImGuiCol_Border];
	c[ImGuiCol_UnsavedMarker] = fg;
	c[ImGuiCol_DockingPreview] = Alpha(accent, 0.7f);
	c[ImGuiCol_DockingEmptyBg] = bg;
	c[ImGuiCol_PlotLines] = accent;
	c[ImGuiCol_PlotLinesHovered] = fg;
	c[ImGuiCol_PlotHistogram] = accent;
	c[ImGuiCol_PlotHistogramHovered] = fg;
	c[ImGuiCol_TableHeaderBg] = Alpha(Mix(bg, fg, 0.1f), ImMin(1.0f, bgAlpha + 0.2f));
	c[ImGuiCol_TableBorderStrong] = Mix(bg, fg, 0.3f);
	c[ImGuiCol_TableBorderLight] = Mix(bg, fg, 0.18f);
	c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_TableRowBgAlt] = Alpha(fg, 0.05f);
	c[ImGuiCol_TextSelectedBg] = Alpha(accent, 0.35f);
	c[ImGuiCol_DragDropTarget] = accent;
	c[ImGuiCol_NavCursor] = accent;
	c[ImGuiCol_NavWindowingHighlight] = Alpha(fg, 0.7f);
	c[ImGuiCol_NavWindowingDimBg] = Alpha(black, 0.2f);
	c[ImGuiCol_ModalWindowDimBg] = Alpha(black, 0.35f);

	t.activeColor = c[ImGuiCol_TitleBgActive];
	t.inactiveColor = c[ImGuiCol_TitleBg];
	t.windowBorderSize = 0.0f;
}

// What a fresh install and the Reset button get.
static void MakeModern(MeterTheme& t)
{
	MakeAccent(t, "SoulMeter", Rgb(17, 19, 26), Rgb(228, 231, 240), Rgb(122, 141, 255), 0.92f);
	t.colors[ImGuiCol_TableHeaderBg] = Rgb(27, 30, 40, 0.95f);
	t.colors[ImGuiCol_TableRowBgAlt] = Rgb(255, 255, 255, 0.025f);
	t.inactiveColor = Rgb(24, 27, 36, 0.98f);
	t.activeColor = Rgb(52, 62, 120, 0.98f);
	t.aggroColor = Rgb(255, 99, 99);
	t.aggroOwnerColor = Rgb(110, 190, 255);
	t.awakeningColor = Rgb(255, 214, 92);
	t.barTrackColor = Rgb(255, 255, 255, 0.03f);
	t.barStyle = THEME_BAR_GLASS;
	t.barOpacity = 0.9f;
	t.barHeight = 0.82f;
	t.barRounding = 3.0f;
	t.windowRounding = 6.0f;
	t.frameRounding = 4.0f;
	t.cellPadding = ImVec2(6.0f, 3.0f);
	t.rowStripes = true;
	t.textEffect = THEME_TEXT_SHADOW;
	t.outlineColor = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
}

void ThemeManager::BuildPresets()
{
	_presets.clear();
	MeterTheme t;

	MakeModern(t);
	_presets.push_back(t);

	// The look from before themes existed.
	MakeDefault(t);
	strcpy_s(t.name, "Legacy");
	_presets.push_back(t);

	{
		MakeDefault(t);
		strcpy_s(t.name, "Classic");
		ImGuiStyle style;
		ImGui::StyleColorsClassic(&style);
		memcpy(t.colors, style.Colors, sizeof(t.colors));
		t.colors[ImGuiCol_WindowBg].w = 1.0f;
		t.activeColor = t.colors[ImGuiCol_TitleBgActive];
		t.inactiveColor = t.colors[ImGuiCol_TitleBg];
		_presets.push_back(t);
	}

	{
		MakeDefault(t);
		strcpy_s(t.name, "Light");
		ImGuiStyle style;
		ImGui::StyleColorsLight(&style);
		memcpy(t.colors, style.Colors, sizeof(t.colors));
		t.colors[ImGuiCol_WindowBg].w = 1.0f;
		t.activeColor = Rgb(160, 190, 235);
		t.inactiveColor = t.colors[ImGuiCol_TitleBg];
		t.textEffect = THEME_TEXT_PLAIN;
		t.outlineColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		t.awakeningColor = Rgb(214, 140, 0);
		t.barOpacity = 0.75f;
		t.rowLines = true;
		t.frameRounding = 3.0f;
		_presets.push_back(t);
	}

	MakeAccent(t, "Glass", Rgb(16, 19, 26), Rgb(232, 236, 244), Rgb(91, 140, 255), 0.45f);
	t.barStyle = THEME_BAR_GRADIENT;
	t.barOpacity = 0.85f;
	t.windowRounding = 8.0f;
	t.frameRounding = 4.0f;
	t.textEffect = THEME_TEXT_SHADOW;
	t.outlineColor = ImVec4(0.0f, 0.0f, 0.0f, 0.85f);
	_presets.push_back(t);

	MakeAccent(t, "Midnight", Rgb(13, 17, 23), Rgb(201, 209, 217), Rgb(56, 139, 253), 0.92f);
	t.barStyle = THEME_BAR_GLASS;
	t.barHeight = 0.9f;
	t.barRounding = 2.0f;
	t.windowRounding = 4.0f;
	t.frameRounding = 3.0f;
	t.rowLines = true;
	_presets.push_back(t);

	MakeAccent(t, "Dracula", Rgb(40, 42, 54), Rgb(248, 248, 242), Rgb(189, 147, 249), 0.95f);
	t.aggroColor = Rgb(255, 85, 85);
	t.aggroOwnerColor = Rgb(139, 233, 253);
	t.awakeningColor = Rgb(241, 250, 140);
	t.barHeight = 0.85f;
	t.barRounding = 3.0f;
	t.frameRounding = 4.0f;
	_presets.push_back(t);

	MakeAccent(t, "Nord", Rgb(46, 52, 64), Rgb(236, 239, 244), Rgb(136, 192, 208), 0.95f);
	t.aggroColor = Rgb(191, 97, 106);
	t.aggroOwnerColor = Rgb(129, 161, 193);
	t.awakeningColor = Rgb(235, 203, 139);
	t.barStyle = THEME_BAR_GRADIENT;
	t.frameRounding = 2.0f;
	t.rowStripes = true;
	_presets.push_back(t);

	MakeAccent(t, "Solarized", Rgb(0, 43, 54), Rgb(147, 161, 161), Rgb(38, 139, 210), 0.97f);
	t.aggroColor = Rgb(220, 50, 47);
	t.aggroOwnerColor = Rgb(108, 113, 196);
	t.awakeningColor = Rgb(181, 137, 0);
	t.rowStripes = true;
	_presets.push_back(t);

	MakeAccent(t, "Crimson", Rgb(11, 11, 12), Rgb(242, 242, 242), Rgb(215, 38, 61), 0.9f);
	t.aggroOwnerColor = Rgb(90, 160, 255);
	t.barStyle = THEME_BAR_GLASS;
	t.windowBorderSize = 1.0f;
	_presets.push_back(t);

	MakeAccent(t, "Sakura", Rgb(42, 31, 43), Rgb(251, 239, 245), Rgb(255, 143, 199), 0.93f);
	t.windowRounding = 6.0f;
	t.frameRounding = 6.0f;
	t.barRounding = 4.0f;
	t.barHeight = 0.8f;
	t.textEffect = THEME_TEXT_SHADOW;
	_presets.push_back(t);

	MakeAccent(t, "Terminal", Rgb(0, 0, 0), Rgb(51, 255, 102), Rgb(51, 255, 102), 0.95f);
	t.barStyle = THEME_BAR_OUTLINE;
	t.barHeight = 0.8f;
	t.textEffect = THEME_TEXT_PLAIN;
	t.rowLines = true;
	t.windowBorderSize = 1.0f;
	_presets.push_back(t);

	MakeAccent(t, "Minimal", Rgb(0, 0, 0), Rgb(255, 255, 255), Rgb(154, 164, 178), 0.2f);
	t.colors[ImGuiCol_TitleBg].w = 0.35f;
	t.colors[ImGuiCol_TitleBgActive].w = 0.5f;
	t.colors[ImGuiCol_TableHeaderBg].w = 0.0f;
	t.activeColor = t.colors[ImGuiCol_TitleBgActive];
	t.inactiveColor = t.colors[ImGuiCol_TitleBg];
	t.barStyle = THEME_BAR_UNDERLINE;
	t.textEffect = THEME_TEXT_OUTLINE;
	_presets.push_back(t);

	// Nothing but outlined text floating over the game.
	MakeAccent(t, "Transparent", Rgb(0, 0, 0), Rgb(255, 255, 255), Rgb(200, 200, 200), 0.0f);
	t.colors[ImGuiCol_TitleBg].w = 0.0f;
	t.colors[ImGuiCol_TitleBgActive].w = 0.0f;
	t.colors[ImGuiCol_TitleBgCollapsed].w = 0.0f;
	t.colors[ImGuiCol_TableHeaderBg].w = 0.0f;
	t.colors[ImGuiCol_Border].w = 0.0f;
	t.colors[ImGuiCol_Header] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
	t.colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
	t.colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.18f);
	t.colors[ImGuiCol_ResizeGrip].w = 0.1f;
	t.colors[ImGuiCol_PopupBg] = Rgb(20, 20, 20, 0.95f);
	t.activeColor = t.colors[ImGuiCol_TitleBgActive];
	t.inactiveColor = t.colors[ImGuiCol_TitleBg];
	t.barOpacity = 0.0f;
	t.textEffect = THEME_TEXT_OUTLINE;
	t.outlineColor = ImVec4(0.0f, 0.0f, 0.0f, 0.9f);
	t.titleEffect = true;
	_presets.push_back(t);
}

void ThemeManager::RefreshSavedThemes()
{
	_savedThemes.clear();
	try {
		if (!std::filesystem::exists(THEME_FOLDER))
			return;
		for (auto& p : std::filesystem::directory_iterator(THEME_FOLDER)) {
			if (p.is_regular_file() && p.path().extension() == ".xml")
				_savedThemes.push_back(p.path().stem().u8string());
		}
	}
	catch (std::exception& e) {
		LogInstance.WriteLog("[ThemeManager] Listing themes failed: %s", e.what());
	}
	std::sort(_savedThemes.begin(), _savedThemes.end());
}

void ThemeManager::Reset()
{
	MeterTheme t;
	MakeModern(t);
	Use(t);
}

void ThemeManager::Use(const MeterTheme& theme)
{
	char fontFile[MAX_PATH];
	float fontScale = _theme.fontScale, columnScale = _theme.columnFontScale, tableScale = _theme.tableFontScale;
	strcpy_s(fontFile, _theme.fontFile);

	_theme = theme;
	Sanitize(_theme);

	// Presets carry no font: keep what the user picked.
	if (_theme.fontFile[0] == 0) {
		strcpy_s(_theme.fontFile, fontFile);
		_theme.fontScale = fontScale;
		_theme.columnFontScale = columnScale;
		_theme.tableFontScale = tableScale;
	}
	else
		ApplyFont(false);

	ApplyStyle();
}

void ThemeManager::ApplyStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();

	memcpy(style.Colors, _theme.colors, sizeof(_theme.colors));

	const float dpi = _dpiScale;
	style.WindowRounding = _theme.windowRounding * dpi;
	style.ChildRounding = _theme.windowRounding * dpi;
	style.PopupRounding = ImMin(_theme.windowRounding, 6.0f) * dpi;
	style.FrameRounding = _theme.frameRounding * dpi;
	style.GrabRounding = _theme.frameRounding * dpi;
	style.TabRounding = _theme.frameRounding * dpi;
	style.ScrollbarRounding = _theme.frameRounding * dpi;
	style.WindowBorderSize = _theme.windowBorderSize;
	style.CellPadding = ImVec2(_theme.cellPadding.x * dpi, _theme.cellPadding.y * dpi);

	// Stock spacing is sized for a 13px font; ours is far bigger, so spacing
	// follows the font instead. The font size is only known inside a frame.
	const float em = ImGui::GetFontSize();
	if (em > 0.0f) {
		style.WindowPadding = ImVec2(em * 0.5f, em * 0.45f);
		style.FramePadding = ImVec2(em * 0.4f, em * 0.18f);
		style.ItemSpacing = ImVec2(em * 0.35f, em * 0.3f);
		style.ItemInnerSpacing = ImVec2(em * 0.25f, em * 0.2f);
		style.IndentSpacing = em * 0.8f;
		style.ScrollbarSize = em * 0.45f;
		style.GrabMinSize = em * 0.4f;
	}
	style.WindowMenuButtonPosition = ImGuiDir_None;
	style.SeparatorTextBorderSize = ImMax(1.0f, dpi * 2.0f);
	style.SeparatorTextPadding = ImVec2(0.0f, style.FramePadding.y);
}

void ThemeManager::ApplyWindowOpacity()
{
	const int alpha = (int)(ImClamp(_theme.windowOpacity, 0.2f, 1.0f) * 255.0f + 0.5f);
	ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
	std::unordered_map<HWND, int> seen;

	for (int i = 0; i < platformIO.Viewports.Size; i++) {
		HWND hwnd = (HWND)platformIO.Viewports[i]->PlatformHandle;
		if (hwnd == nullptr)
			continue;

		auto found = _windowAlpha.find(hwnd);
		const bool fresh = found == _windowAlpha.end();

		if (fresh) {
			// Makes DWM use the alpha channel of what we render, so a
			// translucent WindowBg really shows the desktop/game behind it.
			MARGINS margins = { -1, -1, -1, -1 };
			DwmExtendFrameIntoClientArea(hwnd, &margins);

			// Same trick GLFW uses: blur-behind over an empty region turns on
			// per-pixel alpha without actually blurring anything.
			HRGN region = CreateRectRgn(0, 0, -1, -1);
			DWM_BLURBEHIND blur = {};
			blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
			blur.hRgnBlur = region;
			blur.fEnable = TRUE;
			DwmEnableBlurBehindWindow(hwnd, &blur);
			DeleteObject(region);
		}

		// Layering only starts once someone lowers the opacity, so the default
		// look takes no new path. Checked every frame because the imgui backend
		// rewrites GWL_EXSTYLE whenever a viewport's flags change.
		LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
		const bool layered = (exStyle & WS_EX_LAYERED) != 0;
		if (!layered && alpha < 255)
			SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);

		if ((layered || alpha < 255) && (!layered || fresh || found->second != alpha))
			SetLayeredWindowAttributes(hwnd, 0, (BYTE)alpha, LWA_ALPHA);

		seen[hwnd] = alpha;
	}

	_windowAlpha.swap(seen);
}

void ThemeManager::RefreshFonts()
{
	_fonts.clear();
	try {
		for (auto& p : std::filesystem::recursive_directory_iterator(std::wstring(L"Font/"))) {
			if (p.path().extension() == ".ttf" || p.path().extension() == ".ttc") {
				ImFontObj font;
				font.path = p.path().generic_u8string();
				font.filename = p.path().filename().stem().generic_u8string();
				LogInstance.WriteLog("font path: %s", font.path.c_str());
				_fonts.emplace_back(font);
			}
		}
	}
	catch (std::exception& e) {
		LogInstance.WriteLog("Update font failed: %s", e.what());
	}
}

bool ThemeManager::SelectFont(const char* fileName)
{
	for (const ImFontObj& font : _fonts) {
		if (FontFileName(font) != fileName)
			continue;

		strcpy_s(_theme.fontFile, fileName);
		if (DAMAGEMETER.selectedFont.path != font.path) {
			DAMAGEMETER.selectedFont = font;
			DAMAGEMETER.selectedFont.selectable = true;
			DAMAGEMETER.shouldRebuildAtlas = true;
		}
		return true;
	}
	return false;
}

void ThemeManager::ApplyFont(bool force)
{
	if (_theme.fontFile[0] == 0)
		return;

	if (!SelectFont(_theme.fontFile)) {
		// A theme shared by someone with a font this install lacks.
		LogInstance.WriteLog("[ThemeManager] Font %s not found, keeping current font", _theme.fontFile);
		strcpy_s(_theme.fontFile, FontFileName(DAMAGEMETER.selectedFont).c_str());
	}
	else if (force)
		DAMAGEMETER.shouldRebuildAtlas = true;
}

void ThemeManager::WriteXml(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* ele)
{
	const MeterTheme& t = _theme;

	ele->SetAttribute("Name", t.name);
	ele->SetAttribute("Version", 1);

	tinyxml2::XMLElement* window = doc.NewElement("Window");
	ele->LinkEndChild(window);
	window->SetAttribute("Opacity", t.windowOpacity);
	window->SetAttribute("Rounding", t.windowRounding);
	window->SetAttribute("FrameRounding", t.frameRounding);
	window->SetAttribute("Border", t.windowBorderSize);
	window->SetAttribute("CellPaddingX", t.cellPadding.x);
	window->SetAttribute("CellPaddingY", t.cellPadding.y);
	window->SetAttribute("RowStripes", t.rowStripes);
	window->SetAttribute("RowLines", t.rowLines);

	tinyxml2::XMLElement* bar = doc.NewElement("Bar");
	ele->LinkEndChild(bar);
	bar->SetAttribute("Style", t.barStyle);
	bar->SetAttribute("Opacity", t.barOpacity);
	bar->SetAttribute("Height", t.barHeight);
	bar->SetAttribute("Rounding", t.barRounding);

	tinyxml2::XMLElement* text = doc.NewElement("Text");
	ele->LinkEndChild(text);
	text->SetAttribute("Effect", t.textEffect);
	text->SetAttribute("EffectSize", t.outlineSize);
	text->SetAttribute("Title", t.titleEffect);
	text->SetAttribute("Font", t.fontFile);
	text->SetAttribute("FontScale", t.fontScale);
	text->SetAttribute("ColumnScale", t.columnFontScale);
	text->SetAttribute("TableScale", t.tableFontScale);

	tinyxml2::XMLElement* colors = doc.NewElement("Colors");
	ele->LinkEndChild(colors);

	auto addColor = [&](const char* id, const ImVec4& c) {
		char hex[16];
		ColorToHex(c, hex, sizeof(hex));
		tinyxml2::XMLElement* color = doc.NewElement("Color");
		colors->LinkEndChild(color);
		color->SetAttribute("Id", id);
		color->SetAttribute("Value", hex);
	};

	addColor("Meter.Active", t.activeColor);
	addColor("Meter.Inactive", t.inactiveColor);
	addColor("Meter.Outline", t.outlineColor);
	addColor("Meter.Aggro", t.aggroColor);
	addColor("Meter.AggroOwner", t.aggroOwnerColor);
	addColor("Meter.Awakening", t.awakeningColor);
	addColor("Meter.BarTrack", t.barTrackColor);

	for (int i = 0; i < THEME_JOB_COUNT; i++) {
		char id[16];
		sprintf_s(id, "Job%d", i);
		addColor(id, t.jobColors[i]);
	}

	for (int i = 0; i < ImGuiCol_COUNT; i++)
		addColor(ImGui::GetStyleColorName(i), t.colors[i]);
}

void ThemeManager::ReadXml(const tinyxml2::XMLElement* ele)
{
	ReadXml(ele, _theme);
	Sanitize(_theme);
}

void ThemeManager::ReadXml(const tinyxml2::XMLElement* ele, MeterTheme& t)
{
	if (ele == nullptr)
		return;

	if (const char* name = ele->Attribute("Name"))
		strncpy_s(t.name, name, _TRUNCATE);

	if (const tinyxml2::XMLElement* window = ele->FirstChildElement("Window")) {
		window->QueryfloatAttribute("Opacity", &t.windowOpacity);
		window->QueryfloatAttribute("Rounding", &t.windowRounding);
		window->QueryfloatAttribute("FrameRounding", &t.frameRounding);
		window->QueryfloatAttribute("Border", &t.windowBorderSize);
		window->QueryfloatAttribute("CellPaddingX", &t.cellPadding.x);
		window->QueryfloatAttribute("CellPaddingY", &t.cellPadding.y);
		window->QueryboolAttribute("RowStripes", &t.rowStripes);
		window->QueryboolAttribute("RowLines", &t.rowLines);
	}

	if (const tinyxml2::XMLElement* bar = ele->FirstChildElement("Bar")) {
		bar->QueryIntAttribute("Style", &t.barStyle);
		bar->QueryfloatAttribute("Opacity", &t.barOpacity);
		bar->QueryfloatAttribute("Height", &t.barHeight);
		bar->QueryfloatAttribute("Rounding", &t.barRounding);
	}

	if (const tinyxml2::XMLElement* text = ele->FirstChildElement("Text")) {
		text->QueryIntAttribute("Effect", &t.textEffect);
		text->QueryfloatAttribute("EffectSize", &t.outlineSize);
		text->QueryboolAttribute("Title", &t.titleEffect);
		if (const char* font = text->Attribute("Font"))
			strncpy_s(t.fontFile, font, _TRUNCATE);
		text->QueryfloatAttribute("FontScale", &t.fontScale);
		text->QueryfloatAttribute("ColumnScale", &t.columnFontScale);
		text->QueryfloatAttribute("TableScale", &t.tableFontScale);
	}

	const tinyxml2::XMLElement* colors = ele->FirstChildElement("Colors");
	if (colors == nullptr)
		return;

	for (const tinyxml2::XMLElement* color = colors->FirstChildElement("Color"); color != nullptr; color = color->NextSiblingElement("Color")) {
		const char* id = color->Attribute("Id");
		ImVec4 value;
		if (id == nullptr || !HexToColor(color->Attribute("Value"), value))
			continue;

		if (strcmp(id, "Meter.Active") == 0) t.activeColor = value;
		else if (strcmp(id, "Meter.Inactive") == 0) t.inactiveColor = value;
		else if (strcmp(id, "Meter.Outline") == 0) t.outlineColor = value;
		else if (strcmp(id, "Meter.Aggro") == 0) t.aggroColor = value;
		else if (strcmp(id, "Meter.AggroOwner") == 0) t.aggroOwnerColor = value;
		else if (strcmp(id, "Meter.Awakening") == 0) t.awakeningColor = value;
		else if (strcmp(id, "Meter.BarTrack") == 0) t.barTrackColor = value;
		else if (strncmp(id, "Job", 3) == 0) {
			int job = atoi(id + 3);
			if (job >= 0 && job < THEME_JOB_COUNT)
				t.jobColors[job] = value;
		}
		else {
			// Names ImGui has since renamed, as written by builds on ImGui < 1.91.
			static const char* renamed[][2] = {
				{ "TabActive", "TabSelected" }, { "TabUnfocused", "TabDimmed" },
				{ "TabUnfocusedActive", "TabDimmedSelected" }, { "NavHighlight", "NavCursor" },
			};
			for (const auto& pair : renamed) {
				if (strcmp(id, pair[0]) == 0)
					id = pair[1];
			}

			for (int i = 0; i < ImGuiCol_COUNT; i++) {
				if (strcmp(id, ImGui::GetStyleColorName(i)) == 0) {
					t.colors[i] = value;
					break;
				}
			}
		}
	}
}

bool ThemeManager::SaveToFile(const char* name)
{
	if (ThemeFileStem(name).empty())
		return false;

	strncpy_s(_theme.name, name, _TRUNCATE);

	tinyxml2::XMLDocument doc;
	doc.LinkEndChild(doc.NewDeclaration());
	tinyxml2::XMLElement* root = doc.NewElement(THEME_FILE_ROOT);
	doc.LinkEndChild(root);
	tinyxml2::XMLElement* ele = doc.NewElement(THEME_ELEMENT);
	root->LinkEndChild(ele);
	WriteXml(doc, ele);

	try {
		std::filesystem::create_directories(THEME_FOLDER);
	}
	catch (std::exception& e) {
		LogInstance.WriteLog("[ThemeManager] Creating theme folder failed: %s", e.what());
		return false;
	}

	// _wfopen so theme names outside the ANSI codepage still work.
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, ThemePath(name).c_str(), L"wb") != 0 || fp == nullptr)
		return false;
	bool ok = doc.SaveFile(fp) == tinyxml2::XML_SUCCESS;
	fclose(fp);

	RefreshSavedThemes();
	return ok;
}

// Accepts either a theme file (<SoulMeterTheme><Theme>) or a bare <Theme>.
static const tinyxml2::XMLElement* FindThemeElement(const tinyxml2::XMLDocument& doc)
{
	const tinyxml2::XMLElement* root = doc.FirstChildElement(THEME_FILE_ROOT);
	if (root != nullptr)
		return root->FirstChildElement(THEME_ELEMENT);
	return doc.FirstChildElement(THEME_ELEMENT);
}

bool ThemeManager::LoadFromFile(const char* name)
{
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, ThemePath(name).c_str(), L"rb") != 0 || fp == nullptr)
		return false;

	tinyxml2::XMLDocument doc;
	bool ok = doc.LoadFile(fp) == tinyxml2::XML_SUCCESS;
	fclose(fp);

	const tinyxml2::XMLElement* ele = ok ? FindThemeElement(doc) : nullptr;
	if (ele == nullptr)
		return false;

	MeterTheme t;
	MakeDefault(t);
	t.fontFile[0] = 0;
	ReadXml(ele, t);
	strncpy_s(t.name, name, _TRUNCATE);
	Use(t);
	return true;
}

bool ThemeManager::PushTextEffect()
{
	if (_theme.textEffect == THEME_TEXT_PLAIN || _theme.outlineSize <= 0.0f)
		return false;

	ImGui::OutlineText::PushOutlineText(ImGui::IMGUIOUTLINETEXT(
		ImGui::ColorConvertFloat4ToU32(_theme.outlineColor),
		_theme.outlineSize * _dpiScale,
		_theme.textEffect == THEME_TEXT_SHADOW));
	return true;
}

void ThemeManager::PopTextEffect(bool pushed)
{
	if (pushed)
		ImGui::OutlineText::PopOutlineText();
}

void ThemeManager::PushReadableWindow()
{
	const ImGuiCol cols[] = { ImGuiCol_WindowBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed };
	for (ImGuiCol col : cols) {
		ImVec4 c = _theme.colors[col];
		c.w = ImMax(c.w, 0.9f);
		ImGui::PushStyleColor(col, c);
	}
}

void ThemeManager::PopReadableWindow()
{
	ImGui::PopStyleColor(4);
}

ImGuiTableFlags ThemeManager::TableFlags()
{
	// Outer padding keeps the first and last cell off the window edge.
	// Resizable tables always get column dividers; in the body they would
	// cut through every bar, so they only show there while one is dragged.
	ImGuiTableFlags flags = ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoBordersInBodyUntilResize;
	if (_theme.rowStripes)
		flags |= ImGuiTableFlags_RowBg;
	if (_theme.rowLines)
		flags |= ImGuiTableFlags_BordersInnerH;
	return flags;
}

void ThemeManager::DrawBar(float width, float percent, ImU32 color)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	const float rowHeight = ImGui::GetFontSize();
	ImVec2 pos = ImGui::GetCursorScreenPos();
	pos.x = ImFloor(pos.x);
	// Bars start at the padded first cell; keep the same gap on the right.
	width -= ImMax(0.0f, pos.x - ImGui::GetWindowPos().x) * 2.0f;

	float height = rowHeight * _theme.barHeight;
	float top = pos.y + (rowHeight - height) * 0.5f;

	if (_theme.barStyle == THEME_BAR_UNDERLINE) {
		height = ImMax(2.0f * _dpiScale, ImFloor(rowHeight * 0.15f));
		top = pos.y + rowHeight - height;
	}

	const ImVec2 barMin(pos.x, top);
	const ImVec2 barMax(pos.x + ImFloor(width * percent), top + height);
	const float rounding = ImMin(_theme.barRounding * _dpiScale, height * 0.5f);

	// Channel 0 holds the table's row backgrounds and is drawn before every
	// cell, so the bar stays under all text however columns clip or merge.
	ImGuiTable* table = ImGui::GetCurrentTable();
	ImDrawListSplitter* splitter = (table != nullptr && table->DrawSplitter->_Count > 1) ? table->DrawSplitter : nullptr;
	const int prevChannel = splitter != nullptr ? splitter->_Current : 0;
	if (splitter != nullptr)
		splitter->SetCurrentChannel(drawList, 0);

	// Bars belong to the whole row, not the first cell, but stay in the window.
	ImRect clip(ImGui::GetWindowPos().x, pos.y, ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), pos.y + rowHeight);
	clip.ClipWith(ImGui::GetCurrentWindow()->InnerClipRect);
	drawList->PushClipRect(clip.Min, clip.Max, false);

	if (_theme.barTrackColor.w > 0.0f)
		drawList->AddRectFilled(barMin, ImVec2(pos.x + width, barMax.y), ImGui::ColorConvertFloat4ToU32(_theme.barTrackColor), rounding);

	ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
	c.w *= _theme.barOpacity;
	const ImU32 col = ImGui::ColorConvertFloat4ToU32(c);

	if (barMax.x - barMin.x < 1.0f || c.w <= 0.0f)
		; // nothing to fill
	else if (_theme.barStyle == THEME_BAR_GRADIENT) {
		const ImU32 dark = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x * 0.35f, c.y * 0.35f, c.z * 0.35f, c.w));
		drawList->AddRectFilledMultiColor(barMin, barMax, dark, col, col, dark);
	}
	else if (_theme.barStyle == THEME_BAR_GLASS) {
		drawList->AddRectFilled(barMin, barMax, col, rounding);
		const int shine = (int)(45.0f * c.w);
		drawList->AddRectFilled(barMin, ImVec2(barMax.x, barMin.y + height * 0.5f), IM_COL32(255, 255, 255, shine), rounding, ImDrawFlags_RoundCornersTop);
		drawList->AddLine(ImVec2(barMin.x + rounding, barMin.y + 0.5f), ImVec2(barMax.x - rounding, barMin.y + 0.5f), IM_COL32(255, 255, 255, shine * 2));
	}
	else if (_theme.barStyle == THEME_BAR_OUTLINE) {
		const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * 0.25f));
		drawList->AddRectFilled(barMin, barMax, fill, rounding);
		drawList->AddRect(ImVec2(barMin.x + 0.5f, barMin.y + 0.5f), ImVec2(barMax.x - 0.5f, barMax.y - 0.5f), col, rounding);
	}
	else
		drawList->AddRectFilled(barMin, barMax, col, rounding);

	drawList->PopClipRect();
	if (splitter != nullptr)
		splitter->SetCurrentChannel(drawList, prevChannel);
}

ImU32 ThemeManager::GetJobColor(unsigned int index)
{
	if (index >= THEME_JOB_COUNT)
		index = 0;
	return ImGui::ColorConvertFloat4ToU32(_theme.jobColors[index]);
}

//
// Editor
//

static const char* T(const char* key)
{
	return LANGMANAGER.GetText(key).data();
}

// SameLine when the next item of this width still fits, so a row of short
// items wraps instead of running off the window in long languages.
void SameLineIfFits(float nextWidth)
{
	const float spacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
	// Right after an item the cursor waits at the start of the next line.
	const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
	if (ImGui::GetItemRectMax().x + spacing + nextWidth <= right)
		ImGui::SameLine(0.0f, spacing);
}

static bool ColorRow(const char* id, ImVec4* color, const char* label)
{
	bool changed = ImGui::ColorEdit4(id, (float*)color, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
	ImGui::SameLine();
	ImGui::TextUnformatted(label);
	return changed;
}

static bool Header(const char* key, const char* id, bool open)
{
	char label[256];
	sprintf_s(label, "%s###%s", T(key), id);
	return ImGui::CollapsingHeader(label, open ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
}

void ThemeManager::ShowPresetBar()
{
	ImGui::TextWrapped("%s", T("STR_THEME_DESC"));

	ImGui::TextUnformatted(T("STR_THEME_PRESET"));
	if (ImGui::BeginCombo("##ThemePreset", _theme.name, ImGuiComboFlags_HeightLarge)) {
		ImGui::TextDisabled("%s", T("STR_THEME_BUILTIN"));
		for (size_t i = 0; i < _presets.size(); i++) {
			ImGui::PushID((int)i);
			if (ImGui::Selectable(_presets[i].name, strcmp(_presets[i].name, _theme.name) == 0)) {
				Use(_presets[i]);
				_status[0] = 0;
			}
			ImGui::PopID();
		}

		if (!_savedThemes.empty()) {
			ImGui::Separator();
			ImGui::TextDisabled("%s", T("STR_THEME_SAVED"));
			for (size_t i = 0; i < _savedThemes.size(); i++) {
				ImGui::PushID((int)(i + _presets.size()));
				if (ImGui::Selectable(_savedThemes[i].c_str(), _savedThemes[i] == _theme.name)) {
					if (LoadFromFile(_savedThemes[i].c_str())) {
						strncpy_s(_saveName, _savedThemes[i].c_str(), _TRUNCATE);
						sprintf_s(_status, "%s %s", T("STR_THEME_STATUS_LOADED"), _savedThemes[i].c_str());
					}
					else
						sprintf_s(_status, "%s %s", T("STR_THEME_STATUS_LOAD_FAILED"), _savedThemes[i].c_str());
				}
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button(T("STR_THEME_RESET"))) {
		Reset();
		_status[0] = 0;
	}

	ImGui::InputTextWithHint("##ThemeName", T("STR_THEME_NAME"), _saveName, sizeof(_saveName));
	ImGui::SameLine();
	if (ImGui::Button(T("STR_THEME_SAVE"))) {
		if (SaveToFile(_saveName))
			sprintf_s(_status, "%s %s", T("STR_THEME_STATUS_SAVED"), _saveName);
		else
			sprintf_s(_status, "%s", T("STR_THEME_STATUS_SAVE_FAILED"));
	}

	bool exists = std::find(_savedThemes.begin(), _savedThemes.end(), std::string(_saveName)) != _savedThemes.end();
	if (exists) {
		ImGui::SameLine();
		if (ImGui::Button(T("STR_THEME_DELETE"))) {
			std::error_code ec;
			std::filesystem::remove(ThemePath(_saveName), ec);
			sprintf_s(_status, "%s %s", T("STR_THEME_STATUS_DELETED"), _saveName);
			RefreshSavedThemes();
		}
	}

	if (ImGui::Button(T("STR_THEME_COPY"))) {
		tinyxml2::XMLDocument doc;
		tinyxml2::XMLElement* ele = doc.NewElement(THEME_ELEMENT);
		doc.LinkEndChild(ele);
		WriteXml(doc, ele);
		tinyxml2::XMLPrinter printer;
		doc.Print(&printer);
		ImGui::SetClipboardText(printer.CStr());
		sprintf_s(_status, "%s", T("STR_THEME_STATUS_COPIED"));
	}

	SameLineIfFits(ImGui::CalcTextSize(T("STR_THEME_PASTE")).x + ImGui::GetStyle().FramePadding.x * 2.0f);
	if (ImGui::Button(T("STR_THEME_PASTE"))) {
		const char* clip = ImGui::GetClipboardText();
		tinyxml2::XMLDocument doc;
		const tinyxml2::XMLElement* ele = nullptr;
		if (clip != nullptr && doc.Parse(clip) == tinyxml2::XML_SUCCESS)
			ele = FindThemeElement(doc);

		if (ele != nullptr) {
			MeterTheme t;
			MakeDefault(t);
			t.fontFile[0] = 0;
			ReadXml(ele, t);
			Use(t);
			strncpy_s(_saveName, _theme.name, _TRUNCATE);
			sprintf_s(_status, "%s", T("STR_THEME_STATUS_PASTED"));
		}
		else
			sprintf_s(_status, "%s", T("STR_THEME_STATUS_BAD_PASTE"));
	}

	SameLineIfFits(ImGui::CalcTextSize(T("STR_THEME_OPEN_FOLDER")).x + ImGui::GetStyle().FramePadding.x * 2.0f);
	if (ImGui::Button(T("STR_THEME_OPEN_FOLDER"))) {
		std::error_code ec;
		std::filesystem::create_directories(THEME_FOLDER, ec);
		ShellExecuteA(NULL, "open", THEME_FOLDER, NULL, NULL, SW_SHOWNORMAL);
		RefreshSavedThemes();
	}

	if (_status[0] != 0)
		ImGui::TextDisabled("%s", _status);
}

void ThemeManager::ShowWindowSection()
{
	ImGui::SliderFloat(T("STR_THEME_BG_OPACITY"), &_theme.colors[ImGuiCol_WindowBg].w, 0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat(T("STR_THEME_WINDOW_OPACITY"), &_theme.windowOpacity, 0.2f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SliderFloat(T("STR_THEME_WINDOW_ROUNDING"), &_theme.windowRounding, 0.0f, 16.0f, "%.0f");
	ImGui::SliderFloat(T("STR_THEME_FRAME_ROUNDING"), &_theme.frameRounding, 0.0f, 12.0f, "%.0f");
	ImGui::SliderFloat(T("STR_OPTION_WINDOW_BORDER_SIZE"), &_theme.windowBorderSize, 0.0f, 1.0f, "%.0f");
	ImGui::SliderFloat2(T("STR_OPTION_CELL_PADDING"), (float*)&_theme.cellPadding, 0.0f, 20.0f, "%.0f");
	ImGui::Checkbox(T("STR_THEME_ROW_STRIPES"), &_theme.rowStripes);
	ImGui::SameLine();
	ImGui::Checkbox(T("STR_THEME_ROW_LINES"), &_theme.rowLines);
}

void ThemeManager::ShowFontSection()
{
	if (ImGui::BeginListBox(T("STR_OPTION_FONT"), ImVec2(0.0f, 5.25f * ImGui::GetTextLineHeightWithSpacing()))) {
		for (size_t i = 0; i < _fonts.size(); i++) {
			ImGui::PushID((int)i);
			if (ImGui::Selectable(_fonts[i].filename.c_str(), _fonts[i].path == DAMAGEMETER.selectedFont.path))
				SelectFont(FontFileName(_fonts[i]).c_str());
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}

	if (ImGui::Button(T("STR_OPTION_REFRESH_FONTS")))
		RefreshFonts();

	ImGui::DragFloat(T("STR_OPTION_FONTSCALE"), &_theme.fontScale, 0.005f, 0.3f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat(T("STR_OPTION_COLUMN_FONT_SCALE"), &_theme.columnFontScale, 0.005f, 0.3f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::DragFloat(T("STR_OPTION_TABLE_FONT_SCALE"), &_theme.tableFontScale, 0.005f, 0.3f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

	const char* effects[THEME_TEXT_COUNT] = { T("STR_THEME_TEXT_PLAIN"), T("STR_THEME_TEXT_OUTLINE"), T("STR_THEME_TEXT_SHADOW") };
	ImGui::Combo(T("STR_THEME_TEXT_EFFECT"), &_theme.textEffect, effects, THEME_TEXT_COUNT);

	if (_theme.textEffect != THEME_TEXT_PLAIN) {
		ImGui::SliderFloat(T("STR_THEME_EFFECT_SIZE"), &_theme.outlineSize, 0.5f, 3.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		ColorRow("##ThemeOutline", &_theme.outlineColor, T("STR_OPTION_TEXT_OUTLINE_COLOR"));
		ImGui::Checkbox(T("STR_THEME_TITLE_EFFECT"), &_theme.titleEffect);
	}
}

void ThemeManager::ShowBarSection()
{
	const char* styles[THEME_BAR_COUNT] = {
		T("STR_THEME_BAR_FLAT"), T("STR_THEME_BAR_GRADIENT"), T("STR_THEME_BAR_GLASS"), T("STR_THEME_BAR_OUTLINE"), T("STR_THEME_BAR_UNDERLINE")
	};
	ImGui::Combo(T("STR_THEME_BAR_STYLE"), &_theme.barStyle, styles, THEME_BAR_COUNT);
	ImGui::SliderFloat(T("STR_THEME_BAR_OPACITY"), &_theme.barOpacity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (_theme.barStyle != THEME_BAR_UNDERLINE)
		ImGui::SliderFloat(T("STR_THEME_BAR_HEIGHT"), &_theme.barHeight, 0.1f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	if (_theme.barStyle != THEME_BAR_GRADIENT)
		ImGui::SliderFloat(T("STR_THEME_BAR_ROUNDING"), &_theme.barRounding, 0.0f, 12.0f, "%.0f");
	ColorRow("##ThemeBarTrack", &_theme.barTrackColor, T("STR_THEME_BAR_TRACK"));

	ImGui::SeparatorText(T("STR_THEME_JOB_COLORS"));

	const char* jobs[THEME_JOB_COUNT] = {
		T("STR_CHAR_UNKNOWN"), T("STR_CHAR_HARU"), T("STR_CHAR_ERWIN"), T("STR_CHAR_LILY"), T("STR_CHAR_JIN"), T("STR_CHAR_STELLA"),
		T("STR_CHAR_IRIS"), T("STR_CHAR_CHII"), T("STR_CHAR_EPHNEL"), T("STR_CHAR_NABI"), T("STR_CHAR_DHANA")
	};

	for (int i = 0; i < THEME_JOB_COUNT; i++) {
		ImGui::PushID(i);
		ColorRow("##ThemeJob", &_theme.jobColors[i], jobs[i]);

		if (memcmp(&_theme.jobColors[i], &kJobColors[i], sizeof(ImVec4)) != 0) {
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (ImGui::Button(T("STR_OPTION_RESTORE_DEFAULT_COLOR")))
				_theme.jobColors[i] = kJobColors[i];
		}
		ImGui::PopID();
	}
}

void ThemeManager::ShowMeterColorSection()
{
	ColorRow("##ThemeText", &_theme.colors[ImGuiCol_Text], T("STR_THEME_COLOR_TEXT"));
	ColorRow("##ThemeBg", &_theme.colors[ImGuiCol_WindowBg], T("STR_THEME_COLOR_BG"));
	ColorRow("##ThemeBorder", &_theme.colors[ImGuiCol_Border], T("STR_THEME_COLOR_BORDER"));
	ColorRow("##ThemeActive", &_theme.activeColor, T("STR_OPTION_ACTIVE_COLOR"));
	ColorRow("##ThemeInactive", &_theme.inactiveColor, T("STR_OPTION_INACTIVE_COLOR"));
	ColorRow("##ThemeHeader", &_theme.colors[ImGuiCol_TableHeaderBg], T("STR_THEME_COLOR_HEADER"));
	ColorRow("##ThemeRowHover", &_theme.colors[ImGuiCol_HeaderHovered], T("STR_THEME_COLOR_ROW_HOVER"));
	ColorRow("##ThemeRowAlt", &_theme.colors[ImGuiCol_TableRowBgAlt], T("STR_THEME_COLOR_ROW_ALT"));
	ColorRow("##ThemeRowLine", &_theme.colors[ImGuiCol_TableBorderLight], T("STR_THEME_COLOR_ROW_LINE"));
	ColorRow("##ThemeAggro", &_theme.aggroColor, T("STR_THEME_COLOR_AGGRO"));
	ColorRow("##ThemeAggroOwner", &_theme.aggroOwnerColor, T("STR_THEME_COLOR_AGGRO_OWNER"));
	ColorRow("##ThemeAwakening", &_theme.awakeningColor, T("STR_THEME_COLOR_AWAKENING"));
}

void ThemeManager::ShowAllColorSection()
{
	ImGui::InputTextWithHint("##ThemeColorFilter", T("STR_THEME_FILTER"), _colorFilter, sizeof(_colorFilter));

	for (int i = 0; i < ImGuiCol_COUNT; i++) {
		const char* name = ImGui::GetStyleColorName(i);

		if (_colorFilter[0] != 0) {
			std::string lowerName(name), lowerFilter(_colorFilter);
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
			std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
			if (lowerName.find(lowerFilter) == std::string::npos)
				continue;
		}

		ImGui::PushID(i);
		ColorRow("##ThemeColor", &_theme.colors[i], name);
		ImGui::PopID();
	}
}

void ThemeManager::ShowEditor()
{
	ShowPresetBar();
	ImGui::Separator();

	if (Header("STR_THEME_SECTION_WINDOW", "ThemeWindow", true))
		ShowWindowSection();

	if (Header("STR_THEME_SECTION_TEXT", "ThemeText", true))
		ShowFontSection();

	if (Header("STR_THEME_SECTION_BARS", "ThemeBars", true))
		ShowBarSection();

	if (Header("STR_THEME_SECTION_COLORS", "ThemeColors", false))
		ShowMeterColorSection();

	if (Header("STR_THEME_SECTION_ALL", "ThemeAllColors", false))
		ShowAllColorSection();

	// Edits made above show on the next frame; this keeps them in the same one.
	Sanitize(_theme);
	ApplyStyle();
}
