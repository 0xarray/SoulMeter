#pragma once
#include "pch.h"
#include <unordered_map>

#define THEME ThemeManager::getInstance()

#define THEME_FOLDER "Theme/"
#define THEME_NAME_LEN 64
#define THEME_JOB_COUNT 11

struct ImFontObj
{
	std::string path;
	std::string filename;
	bool selectable = false;
};

enum ThemeBarStyle {
	THEME_BAR_FLAT,
	THEME_BAR_GRADIENT,
	THEME_BAR_GLASS,
	THEME_BAR_OUTLINE,
	THEME_BAR_UNDERLINE,
	THEME_BAR_COUNT
};

enum ThemeTextEffect {
	THEME_TEXT_PLAIN,
	THEME_TEXT_OUTLINE,
	THEME_TEXT_SHADOW,
	THEME_TEXT_COUNT
};

// Everything that decides how the meter looks. A theme file is this struct
// written out as XML; option.xml keeps the active one in its <Theme> element.
struct MeterTheme {
	char name[THEME_NAME_LEN] = { 0 };

	// The full imgui palette, so every window (options, details, graph) follows it.
	ImVec4 colors[ImGuiCol_COUNT];

	// Title bar while a run is being measured / while idle.
	ImVec4 activeColor;
	ImVec4 inactiveColor;

	ImVec4 outlineColor;
	ImVec4 aggroColor;
	ImVec4 aggroOwnerColor;
	ImVec4 awakeningColor;
	ImVec4 barTrackColor;
	ImVec4 jobColors[THEME_JOB_COUNT];

	// Multiplies everything in the window, text included (layered window alpha).
	float windowOpacity = 1.0f;
	float windowRounding = 0.0f;
	float frameRounding = 0.0f;
	float windowBorderSize = 1.0f;
	ImVec2 cellPadding = ImVec2(0, 0);
	bool rowStripes = false;
	bool rowLines = false;

	int barStyle = THEME_BAR_FLAT;
	float barOpacity = 1.0f;
	float barHeight = 1.0f;
	float barRounding = 0.0f;

	int textEffect = THEME_TEXT_OUTLINE;
	float outlineSize = 1.0f;
	// Also outline the meter's title; needed once the title bar has no fill.
	bool titleEffect = false;

	// Empty means "whatever font is loaded now" - built-in presets leave it empty
	// because they cannot know which fonts are in the Font folder.
	char fontFile[MAX_PATH] = { 0 };
	float fontScale = 1.0f;
	float columnFontScale = 1.0f;
	float tableFontScale = 1.0f;
};

// SameLine when the next item of this width still fits, else a new line.
void SameLineIfFits(float nextWidth);

class ThemeManager : public Singleton<ThemeManager> {
private:
	MeterTheme _theme;
	std::vector<MeterTheme> _presets;
	std::vector<std::string> _savedThemes;

	std::vector<ImFontObj> _fonts;

	char _saveName[THEME_NAME_LEN] = { 0 };
	char _status[256] = { 0 };
	char _colorFilter[64] = { 0 };

	std::unordered_map<HWND, int> _windowAlpha;
	float _dpiScale = 1.0f;

	void BuildPresets();
	void RefreshSavedThemes();

	void ShowPresetBar();
	void ShowWindowSection();
	void ShowFontSection();
	void ShowBarSection();
	void ShowMeterColorSection();
	void ShowAllColorSection();

	void ApplyFont(bool force);
	bool SaveToFile(const char* name);
	bool LoadFromFile(const char* name);

public:
	ThemeManager();
	~ThemeManager();

	MeterTheme& Current() { return _theme; }
	static void MakeDefault(MeterTheme& theme);
	void Reset();

	// Copies a preset over the current theme. Keeps the current font when the
	// preset does not name one.
	void Use(const MeterTheme& theme);

	// Pushes the theme into ImGuiStyle; cheap enough to run every frame.
	void ApplyStyle();

	// Monitor DPI / 96 when running DPI-aware, else 1. Theme sizes are stored
	// in 96-DPI pixels and multiplied by this when applied.
	void SetDpiScale(float scale) { _dpiScale = scale; }
	float GetDpiScale() const { return _dpiScale; }
	// Per-pixel alpha + layered opacity for every OS window imgui owns.
	void ApplyWindowOpacity();

	void RefreshFonts();
	const std::vector<ImFontObj>& GetFonts() { return _fonts; }
	// Loads the font named in the theme (a file name like "NotoSansAll-Bold.ttf").
	bool SelectFont(const char* fileName);

	void WriteXml(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* ele);
	void ReadXml(const tinyxml2::XMLElement* ele);
	void ReadXml(const tinyxml2::XMLElement* ele, MeterTheme& theme);

	// Outline/shadow for the table text; false when the theme wants plain text.
	bool PushTextEffect();
	void PopTextEffect(bool pushed);

	// Around Begin() of the Options window: keeps it readable (at least 90%
	// opaque) even when the theme makes every window see-through.
	void PushReadableWindow();
	void PopReadableWindow();
	ImGuiTableFlags TableFlags();

	// Draws a damage bar from the cursor across width*percent, one row tall.
	void DrawBar(float width, float percent, ImU32 color);

	ImU32 GetJobColor(unsigned int index);

	void ShowEditor();
};
