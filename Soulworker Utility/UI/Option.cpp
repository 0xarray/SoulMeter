#include "pch.h"
#include <stdio.h>
#include <filesystem>
#include ".\UI\Option.h"
#include ".\UI\HotKey.h"
#include ".\UI\PlayerTable.h"
#include ".\UI\UiWindow.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Buff Meter\Buff Meter.h"
#include ".\Damage Meter\MySQLite.h"
#include ".\Soulworker Packet\HookCommand.h"
#include "SWConfig.h"
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

UiOption::UiOption()  :
	_open(0), _framerate(1),
	_is1K(0), _is1M(0), _is10K(0), _isSoloMode(0), _hideName(0), _isTopMost(true), _isUseImage(FALSE), _teamTA_LF(false), _isSoloRankMode(FALSE), _isUseSaveData(FALSE),
	_isDontSaveUnfinishedMaze(false),
	_unlockFps(FALSE), _fpsCap(144), _unlockFov(FALSE), _highDpi(FALSE),
	_windowWidth(800), _refreshTime((float)0.3), _oriIsUseSaveData(FALSE)
{
	strcpy_s(_selectedLang, LANGMANAGER.GetCurrentLang());

	THEME.RefreshFonts();
	if (THEME.GetFonts().size() > 0) {
		DAMAGEMETER.selectedFont = THEME.GetFonts()[0];
		DAMAGEMETER.shouldRebuildAtlas = true;
	}
	else
		LogInstance.WriteLog("No font found in Font/ folder");
}

UiOption::~UiOption() 
{
	
}

// Looks (colors, fonts, bars, opacity) live in the Theme tab.
bool UiOption::ShowTableOption() {

	ImGui::SliderInt(LANGMANAGER.GetText("STR_OPTION_TIMER_ACCURACY").data(), &DAMAGEMETER.mswideness, 1, 3);
	ImGui::DragFloat(LANGMANAGER.GetText("STR_OPTION_TABLE_REFRESH_TIME").data(), &_refreshTime, 0.005f, 0.1f, 1.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_HIGH_DPI").data(), (bool*)&_highDpi);
	ImGui::SetItemTooltip("%s", LANGMANAGER.GetText("STR_OPTION_HIGH_DPI_DESC").data());

	return TRUE;
}

static const char* GetHotkeyActionText(const char* name) {

	if (strcmp(name, u8"Clear") == 0)
		return LANGMANAGER.GetText("STR_OPTION_HOTKEY_ACTION_CLEAR").data();

	if (strcmp(name, u8"Toogle") == 0)
		return LANGMANAGER.GetText("STR_OPTION_HOTKEY_ACTION_TOGGLE").data();

	if (strcmp(name, u8"RestartMaze") == 0)
		return LANGMANAGER.GetText("STR_OPTION_HOTKEY_ACTION_RESTART_MAZE").data();

	if (strcmp(name, u8"ExitMaze") == 0)
		return LANGMANAGER.GetText("STR_OPTION_HOTKEY_ACTION_EXIT_MAZE").data();

	return name;
}

// Frame cap and FOV are applied inside the game by the capture hook, so they
// only do anything while it is attached.
void UiOption::ApplyGameTweaks() {
	uint32_t fps = HOOK_FPS_GAME;
	if (_unlockFps)
		fps = _fpsCap > 0 ? (uint32_t)_fpsCap : HOOK_FPS_UNCAPPED;

	HookCommandSetFpsCap(fps);
	HookCommandSetFovUnlock(_unlockFov != FALSE);
}

void UiOption::ShowGameTweaks() {

	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::SeparatorText(LANGMANAGER.GetText("STR_OPTION_GAME_TWEAKS").data());

	if (!HookCommandIsConnected()) {
		ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
		ImGui::TextWrapped("%s", LANGMANAGER.GetText("STR_OPTION_GAME_TWEAKS_NO_HOOK").data());
		ImGui::PopStyleColor();
	}

	const float boxWidth = ImGui::CalcTextSize("0000000000000").x;

	if (ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_UNLOCK_FPS").data(), (bool*)&_unlockFps))
		ApplyGameTweaks();

	if (_unlockFps) {
		ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
		ImGui::SetNextItemWidth(boxWidth);
		bool changed = ImGui::InputInt("##FpsCap", &_fpsCap, 10, 60);
		if (changed)
			_fpsCap = ImClamp(_fpsCap, 0, 1000);

		// The lower bound waits for the box to be left: raising a half-typed
		// number to 30 under the caret makes anything below it untypeable.
		// 0 stays 0 - that is the no-cap-at-all setting.
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			if (_fpsCap > 0 && _fpsCap < 30)
				_fpsCap = 30;
			changed = true;
		}

		if (changed)
			ApplyGameTweaks();
	}

	if (ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_UNLOCK_FOV").data(), (bool*)&_unlockFov))
		ApplyGameTweaks();
}

bool UiOption::ShowHotkeySetting() {

	ImGuiStyle& style = ImGui::GetStyle();

	// Column offsets are measured from the widest label so nothing overlaps in any language.
	float actionWidth = ImGui::CalcTextSize(LANGMANAGER.GetText("STR_OPTION_HOTKEY_COL_ACTION").data()).x;
	float bindWidth = ImMax(
		ImGui::CalcTextSize(LANGMANAGER.GetText("STR_OPTION_HOTKEY_PRESS_KEY").data()).x,
		ImGui::CalcTextSize(LANGMANAGER.GetText("STR_OPTION_HOTKEY_UNBOUND").data()).x);

	for (auto itr = HOTKEY.begin(); itr != HOTKEY.end(); itr++) {

		char combo[HOTKEY_COMBO_LEN] = { 0 };
		HotKey::GetComboName((*itr)->GetKey(), (*itr)->GetKeyCount(), combo, HOTKEY_COMBO_LEN);

		actionWidth = ImMax(actionWidth, ImGui::CalcTextSize(GetHotkeyActionText((*itr)->GetName())).x);
		bindWidth = ImMax(bindWidth, ImGui::CalcTextSize(combo).x);
	}

	const float bindOffset = actionWidth + style.ItemSpacing.x * 2.0f;
	const float bindButtonWidth = bindWidth + style.FramePadding.x * 4.0f;

	ImGui::TextWrapped("%s", LANGMANAGER.GetText("STR_OPTION_HOTKEY_HELP").data());
	ImGui::Separator();

	ImGui::TextDisabled("%s", LANGMANAGER.GetText("STR_OPTION_HOTKEY_COL_ACTION").data());
	ImGui::SameLine(bindOffset);
	ImGui::TextDisabled("%s", LANGMANAGER.GetText("STR_OPTION_HOTKEY_COL_KEY").data());

	int id = 0;

	for (auto itr = HOTKEY.begin(); itr != HOTKEY.end(); itr++) {

		AutoHotKey* hotkey = *itr;

		ImGui::PushID(id++);

		ImGui::AlignTextToFramePadding();
		ImGui::Text("%s", GetHotkeyActionText(hotkey->GetName()));
		ImGui::SameLine(bindOffset);

		char label[HOTKEY_COMBO_LEN + 32] = { 0 };

		if (HOTKEY.isCapturing(hotkey)) {
			sprintf_s(label, "%s###HotKeyBind", LANGMANAGER.GetText("STR_OPTION_HOTKEY_PRESS_KEY").data());
		}
		else if (hotkey->GetKeyCount() < 1) {
			sprintf_s(label, "%s###HotKeyBind", LANGMANAGER.GetText("STR_OPTION_HOTKEY_UNBOUND").data());
		}
		else {
			char combo[HOTKEY_COMBO_LEN] = { 0 };
			HotKey::GetComboName(hotkey->GetKey(), hotkey->GetKeyCount(), combo, HOTKEY_COMBO_LEN);
			sprintf_s(label, "%s###HotKeyBind", combo);
		}

		if (ImGui::Button(label, ImVec2(bindButtonWidth, 0.0f))) {
			if (HOTKEY.isCapturing(hotkey))
				HOTKEY.CancelCapture();
			else
				HOTKEY.BeginCapture(hotkey);
		}

		if (!hotkey->isDefaultKey()) {
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			if (ImGui::Button(LANGMANAGER.GetText("STR_OPTION_HOTKEY_RESET").data())) {
				hotkey->ResetKey();
				SaveOption(TRUE);
			}
		}

		ImGui::PopID();
	}

	if (HOTKEY.isCapturing())
		ImGui::TextWrapped("%s", LANGMANAGER.GetText("STR_OPTION_HOTKEY_CAPTURING").data());

	if (HOTKEY.ConsumeChanged())
		SaveOption(TRUE);

	ShowGameTweaks();

	ImGui::Separator();
	ImGui::Text(LANGMANAGER.GetText("STR_OPTION_HOTKEY_DESC_5").data());

	ImGui::TextAlignCenter::SetTextAlignCenter();
	{
		ImGui::Text("\n\n\n\nRainy");
	}
	ImGui::TextAlignCenter::UnSetTextAlignCenter();

	return TRUE;
}

void UiOption::Helper() {

	static uint32_t helper = 1;
	char name[128] = { 0 };

	unsigned int monster[4] = { 604, 605, 10000206, 10194613 };
	unsigned int skill[4] = { 72000233, 72000331, 72000433, 72000638 };
	unsigned int buff[4] = { 10001, 10111, 10222, 10333 };

	if (DAMAGEMETER.GetWorldID() == 0) {
		DAMAGEMETER.SetWorldID(20011);
	}

	DAMAGEMETER.InsertDB(0, monster[0]);
	DAMAGEMETER.InsertDB(1, monster[1]);
	DAMAGEMETER.InsertDB(2, monster[2]);
	DAMAGEMETER.InsertDB(3, monster[3]);
	DAMAGEMETER.InsertDB(4, monster[0]);
	DAMAGEMETER.InsertDB(5, monster[1]);
	DAMAGEMETER.InsertDB(6, monster[2]);
	DAMAGEMETER.InsertDB(7, monster[3]);

	for (int i = 0; i < 4; i++) {
		sprintf_s(name, 128, "%s %d", LANGMANAGER.GetText("STR_OPTION_TEST_VALUE_PLAYER").data(), helper);
		
		uint32_t id;
		if (helper == 3) {
			id = DAMAGEMETER.GetMyID();
		}
		else {
			id = helper;
			DAMAGEMETER.InsertPlayerMetadata(id, name, helper % 11);
		}

		//DAMAGEMETER.InsertPlayerMetadata(id, name, helper % 10);
		DAMAGEMETER.AddDamage(id, helper * 10000, helper * 5000, 4, helper * 2, i % 4, skill[i % 4]);
		DAMAGEMETER.AddDamage(id, helper * 20000, helper * 5000, 4, helper * 3, (i + 1) % 4, skill[(i + 1) % 4]);
		DAMAGEMETER.AddDamage(id, helper * 30000, helper * 5000, 4, helper * 4, (i + 2) % 4, skill[(i + 2) % 4]);
		DAMAGEMETER.AddDamage(id, helper * 40000, helper * 5000, 4, helper * 5, (i + 3) % 4, skill[(i + 3) % 4]);
		DAMAGEMETER.AddDamage(id, helper * 20000, helper * 5000, 4, helper * 3, (i + 4) % 4, skill[(i + 1) % 4]);
		DAMAGEMETER.AddDamage(id, helper * 30000, helper * 5000, 4, helper * 4, (i + 5) % 4, skill[(i + 2) % 4]);
		DAMAGEMETER.AddDamage(id, helper * 40000, helper * 5000, 4, helper * 5, (i + 6) % 4, skill[(i + 3) % 4]);
		DAMAGEMETER.AddDamage(id, helper * 40000, helper * 5000, 4, helper * 5, (i + 7) % 4, skill[(i + 3) % 4]);

		BUFFMETER.AddBuff(id, buff[id % 4], 1 + id);
		helper++;
	}

	DAMAGEMETER.SetTestMode();
}

// Sits at the right end of the current line, or of the next one when it does not fit.
void UiOption::ShowLangSelector() {
	const ImGuiStyle& style = ImGui::GetStyle();
	const char* langLabel = LANGMANAGER.GetText("STR_OPTION_COMBO_LANG").data();
	const char* comboPreview = LANGMANAGER.GetText("STR_LANG_NAME").data();

	const float comboWidth = ImGui::CalcTextSize(comboPreview).x + ImGui::GetFrameHeight() + style.FramePadding.x * 3.0f;
	const float width = ImGui::CalcTextSize(langLabel).x + style.ItemInnerSpacing.x + comboWidth;

	SameLineIfFits(width);
	ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width));
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%s", langLabel);
	ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
	ImGui::SetNextItemWidth(comboWidth);
	if (ImGui::BeginCombo(u8"###OptionLangSelector", comboPreview, ImGuiComboFlags_HeightLarge)) {

		int32_t i = 0;
		for (auto itr = _allLangList.begin(); itr != _allLangList.end(); itr++)
		{

			char label[MONSTER_NAME_LEN] = { 0 };
			sprintf_s(label, MONSTER_NAME_LEN, "%s##%d", itr->second.c_str(), i);

			if (ImGui::Selectable(label, strcmp(_selectedLang, itr->first.c_str()) == 0)) {
				strcpy_s(_selectedLang, itr->first.c_str());
				ChangeLang();
			}

			i++;
		}

		ImGui::EndCombo();
	}
}

void UiOption::ChangeLang()
{
	DAMAGEMETER.GetLock();
	{
		LANGMANAGER.SetCurrentLang(_selectedLang);
		// need reload sql command
		SWDB.Init();
	}
	DAMAGEMETER.FreeLock();
}



void UiOption::ShowTeamTALFSelector()
{
	ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_TEAMTA_LUNARFALL").data(), (bool*)&_teamTA_LF);
	ImGui::BeginDisabled(!_teamTA_LF);
	const char* comboPreview = nullptr;
	if (_teamTA_LF_Mode == 1)
		comboPreview = LANGMANAGER.GetText("STR_OPTION_TEAMTA_OPTION_1").data();
	else
		comboPreview = LANGMANAGER.GetText("STR_OPTION_TEAMTA_OPTION_2").data();
	if (ImGui::BeginCombo(u8"###OptionTALF", comboPreview, ImGuiComboFlags_HeightLargest))
	{

		char label[128] = { 0 };

		for (int32_t i = 1; i <= 2; i++)
		{
			if (i == 1)
				sprintf_s(label, 128, "%s##OptionTALF1", LANGMANAGER.GetText("STR_OPTION_TEAMTA_OPTION_1").data());
			else
				sprintf_s(label, 128, "%s##OptionTALF2", LANGMANAGER.GetText("STR_OPTION_TEAMTA_OPTION_2").data());
			if (ImGui::Selectable(label, _teamTA_LF_Mode == i)) {
				_teamTA_LF_Mode = i;
			}
		}

		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
}

void UiOption::ShowFeatures()
{
	ImGui::SeparatorText(LANGMANAGER.GetText("STR_OPTION_SECTION_UNITS").data());

	// At most one of these is set; none shows full numbers.
	int unit = _is1K ? 1 : _is10K ? 2 : _is1M ? 3 : 0;
	const char* units[] = {
		LANGMANAGER.GetText("STR_OPTION_UNIT_NONE").data(), LANGMANAGER.GetText("STR_OPTION_UNIT_1K").data(),
		LANGMANAGER.GetText("STR_OPTION_UNIT_10K").data(), LANGMANAGER.GetText("STR_OPTION_UNIT_1M").data()
	};
	for (int i = 0; i < IM_ARRAYSIZE(units); i++) {
		if (i > 0)
			SameLineIfFits(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(units[i]).x);
		ImGui::RadioButton(units[i], &unit, i);
	}
	_is1K = unit == 1;
	_is10K = unit == 2;
	_is1M = unit == 3;

	ImGui::SeparatorText(LANGMANAGER.GetText("STR_OPTION_SECTION_DISPLAY").data());
	ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_SOLO_MODE").data(), (bool*)&_isSoloMode);
	ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_HIDE_NAME").data(), (bool*)&_hideName);
	ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_USE_IMAGE").data(), (bool*)&_isUseImage);
	ImGui::Checkbox(LANGMANAGER.GetText("STR_MENU_VERTICAL").data(), (bool*)&_isVertical);

	ImGui::SeparatorText(LANGMANAGER.GetText("STR_OPTION_SECTION_RECORDS").data());
	ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_USE_SAVEDATA").data(), (bool*)&_isUseSaveData);
	ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_SOLO_RANK_MODE").data(), (bool*)&_isSoloRankMode);
	ImGui::Checkbox(LANGMANAGER.GetText("STR_OPTION_DONT_SAVE_UNFINISHED_MAZE").data(), (bool*)&_isDontSaveUnfinishedMaze);
	ShowTeamTALFSelector();
}

void UiOption::ShowDiscord()
{
	ImGui::Text("Discord");
	ImGui::Separator();

	ImGui::TextWrapped("%s", SOULMETER_DISCORD_INVITE);
	ImGui::Spacing();

	if (ImGui::Button("Join Discord")) {
		ShellExecuteA(NULL, "open", SOULMETER_DISCORD_URL, NULL, NULL, SW_SHOWNORMAL);
	}
}

void UiOption::OpenOption() {

	_open = TRUE;

	if (DAMAGEMETER.size() < 1) {
		Helper();
		PLAYERTABLE.ResizeTalbe();
	}

	char label[128] = { 0 };
	sprintf_s(label, "%s###Option", LANGMANAGER.GetText("STR_OPTION_WINDOWS_NAME").data());

	const float em = ImGui::GetFontSize();
	ImGui::SetNextWindowSize(ImVec2(em * 24.0f, em * 22.0f), ImGuiCond_FirstUseEver);
	// The background is drawn inside Begin, so the override can end right after.
	THEME.PushReadableWindow();
	ImGui::Begin(label, 0, ImGuiWindowFlags_None);
	THEME.PopReadableWindow();

		// The one button that matters, in the accent color.
		const ImGuiStyle& style = ImGui::GetStyle();
		ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonHovered]);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_ButtonActive]);
		bool saveAndExit = ImGui::Button(LANGMANAGER.GetText("STR_OPTION_SAVE_AND_EXIT").data());
		ImGui::PopStyleColor(2);

		if (saveAndExit) {
			SaveOption();
			if (DAMAGEMETER.GetWorldID() == 20011) {
				DAMAGEMETER.SetWorldID(0);
			}
			_open = FALSE;
		}

		ImGui::SameLine();

		if (ImGui::Button(LANGMANAGER.GetText("STR_OPTION_ADD_TEST_VALUE").data())) {
			Helper();
		}

		ShowLangSelector();

#ifdef _DEBUG
		if (ImGui::Button("START TIMER")) {
			DAMAGEMETER.Start();
		}
		ImGui::SameLine();
		if (ImGui::Button("STOP TIMER")) {
			DAMAGEMETER.Suspend();
		}
#endif
		ImGui::Spacing();

		if (ImGui::BeginTabBar("##tabs")) {
			char label[128] = {0};
			sprintf_s(label, "%s###TabFeatures", LANGMANAGER.GetText("STR_OPTION_TAB_TABLE_FEATURES").data());
			if (ImGui::BeginTabItem(label)) {
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
				ShowFeatures();
				ImGui::PopItemWidth();
				ImGui::EndTabItem();
			}

			sprintf_s(label, "%s###TabTheme", LANGMANAGER.GetText("STR_OPTION_TAB_THEME").data());
			if (ImGui::BeginTabItem(label)) {
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
				THEME.ShowEditor();
				ImGui::PopItemWidth();
				ImGui::EndTabItem();
			}

			sprintf_s(label, "%s###TabTable", LANGMANAGER.GetText("STR_OPTION_TAB_TABLE_SETTING").data());
			if (ImGui::BeginTabItem(label)) {
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5f);
				ShowTableOption();
				ImGui::PopItemWidth();
				ImGui::EndTabItem();
			}


			sprintf_s(label, "%s###TabHotKey", LANGMANAGER.GetText("STR_OPTION_TAB_HOTKEY_SETTING").data());
			if (ImGui::BeginTabItem(label)) {
				ShowHotkeySetting();
				ImGui::EndTabItem();
			}
			else if (HOTKEY.isCapturing()) {
				HOTKEY.CancelCapture();
			}

			if (ImGui::BeginTabItem("Discord")) {
				ShowDiscord();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
}

void UiOption::Init() {

	HOTKEY.Init();

	if (!GetOption()) {
		SetBasicOption();
	}
	_inited = true;

	// Queued rather than delivered: the hook connects later, and reconnecting
	// replays whatever was last pushed.
	ApplyGameTweaks();
}

bool UiOption::GetOption() {

	tinyxml2::XMLDocument doc;

	if (doc.LoadFile(OPTION_FILE_NAME))
		return FALSE;

	tinyxml2::XMLNode* node = doc.FirstChildElement("SDM");

	if (!node)
	{
		LogInstance.WriteLog("[UiOption::GetOption] Failed to get SDM element");
		return FALSE;
	}
	// Option
	tinyxml2::XMLElement* ele = node->FirstChildElement("Option");

	if (!ele)
	{
		LogInstance.WriteLog("[UiOption::GetOption] Failed to get Option element");
		return FALSE;
	}
	MeterTheme& theme = THEME.Current();

	// Pre-theme builds kept these on <Option>; a <Theme> element read below wins.
	auto attr = ele->FindAttribute("GlobalScale");

	if (attr != nullptr)
		attr->QueryfloatValue(&theme.fontScale);

	attr = ele->FindAttribute("TableScale");

	if (attr != nullptr)
		attr->QueryfloatValue(&theme.tableFontScale);

	attr = ele->FindAttribute("ColumnScale");

	if (attr != nullptr)
		attr->QueryfloatValue(&theme.columnFontScale);

	attr = ele->FindAttribute("K");

	if (attr != nullptr)
		attr->QueryIntValue(&_is1K);


#if DEBUG_READ_XML == 1
	LogInstance.WriteLog("Read 1K = %d", _is1K);
#endif

	attr = ele->FindAttribute("M");

	if (attr != nullptr)
		attr->QueryIntValue(&_is1M);

	

	attr = ele->FindAttribute("Man");

	if (attr != nullptr)
		attr->QueryIntValue(&_is10K);

	attr = ele->FindAttribute("IsSoloMode");
	if (attr != nullptr)
		attr->QueryIntValue(&_isSoloMode);

	attr = ele->FindAttribute("DoHideName");
	if (attr != nullptr)
		attr->QueryIntValue(&_hideName);

	attr = ele->FindAttribute("IsTopMost");
	if (attr != nullptr)
		attr->QueryIntValue(&_isTopMost);

	attr = ele->FindAttribute("IsUseImage");
	if (attr != nullptr)
		attr->QueryIntValue(&_isUseImage);

	attr = ele->FindAttribute("IsVertical");
	if (attr != nullptr)
		attr->QueryIntValue(&_isVertical);

	if (const char* rows = ele->Attribute("VerticalRows"))
		_verticalRows = rows;

	attr = ele->FindAttribute("TeamTA_LF");
	if (attr != nullptr)
		attr->QueryIntValue(&_teamTA_LF);

	attr = ele->FindAttribute("TeamTA_LF_Mode");
	if (attr != nullptr)
		attr->QueryIntValue(&_teamTA_LF_Mode);

	attr = ele->FindAttribute("LogFile");
	if (attr != nullptr) {
		attr->QueryboolValue(&LogInstance.shouldLog);
		// shouldLog alone never opens the file
		if (LogInstance.shouldLog)
			LogInstance.Enable();
	}

	attr = ele->FindAttribute("LogMonsterStats");
	if (attr != nullptr)
		attr->QueryboolValue(&DAMAGEMETER.shouldLogMonsterStats);

	attr = ele->FindAttribute("TimerAcc");
	if (attr != nullptr)
		attr->QueryIntValue(&DAMAGEMETER.mswideness);

	// Saved as attributes; older builds looked for child elements and never found them.
	const char* savedLang = ele->Attribute("UseLangFile");
	if (savedLang != nullptr && savedLang[0] != '\0' && strcmp(savedLang, _selectedLang) != 0) {
		strcpy_s(_selectedLang, savedLang);
		ChangeLang();
	}

	attr = ele->FindAttribute("IsSoloRankMode");
	if (attr != nullptr)
		attr->QueryIntValue(&_isSoloRankMode);

	attr = ele->FindAttribute("IsUseSaveData");
	if (attr != nullptr) {
		attr->QueryIntValue(&_isUseSaveData);
		attr->QueryIntValue(&_oriIsUseSaveData);
	}
	

	

	// A font that has since been removed would leave the atlas empty, so
	// SelectFont only takes names it finds in the Font folder.
	const char* savedFont = ele->Attribute("UseFontFile");
	if (savedFont != nullptr)
		THEME.SelectFont(savedFont);

	attr = ele->FindAttribute("IsDontSaveUnfinishedMaze");
	if (attr != nullptr)
		attr->QueryIntValue(&_isDontSaveUnfinishedMaze);

	attr = ele->FindAttribute("UnlockFps");
	if (attr != nullptr)
		attr->QueryIntValue(&_unlockFps);

	attr = ele->FindAttribute("FpsCap");
	if (attr != nullptr)
		attr->QueryIntValue(&_fpsCap);

	attr = ele->FindAttribute("UnlockFov");
	if (attr != nullptr)
		attr->QueryIntValue(&_unlockFov);

	attr = ele->FindAttribute("HighDpi");
	if (attr != nullptr)
		attr->QueryIntValue(&_highDpi);

#if DEBUG_READ_XML == 1
	LogInstance.WriteLog("Read 1M = %d", _is1M);
#endif

	attr = ele->FindAttribute("CellPaddingX");

	if (attr != nullptr)
		attr->QueryfloatValue(&theme.cellPadding.x);

	attr = ele->FindAttribute("CellPaddingY");

	if (attr != nullptr)
		attr->QueryfloatValue(&theme.cellPadding.y);

	attr = ele->FindAttribute("BorderSize");

	if (attr != nullptr)
		attr->QueryfloatValue(&theme.windowBorderSize);


	attr = ele->FindAttribute("WindowWidth");

	if (attr != nullptr)
		attr->QueryfloatValue(&_windowWidth);


#if DEBUG_READ_XML == 1
	LogInstance.WriteLog("Read WindowWidth = %f", _windowWidth);
#endif

	attr = ele->FindAttribute("RefreshTime");

	if (attr != nullptr)
		attr->QueryfloatValue(&_refreshTime);

#if DEBUG_READ_XML == 1
		LogInstance.WriteLog("Read RefreshTime = %f", _refreshTime);
#endif
		attr = ele->FindAttribute("WinPosX");

		if (attr != nullptr)
		{
			float winX, winY;

			attr->QueryfloatValue(&winX);

			attr = ele->FindAttribute("WinPosY");
			attr->QueryfloatValue(&winY);

			// Saved in 96-DPI pixels so switching DPI awareness keeps the spot.
			winX *= THEME.GetDpiScale();
			winY *= THEME.GetDpiScale();
			//SetWindowPos(UIWINDOW.GetHWND(), HWND_NOTOPMOST, winX, winY, 0, 0, SWP_NOSIZE);
			SetWindowPos(UIWINDOW.GetHWND(), HWND_TOPMOST, static_cast<int>(winX), static_cast<int>(winY), 0, 0, SWP_NOSIZE);
		}

#if DEBUG_READ_XML == 1
		LogInstance.WriteLog("Read WinPos(X,Y) = (%f, %f)", winX, winY);
#endif

	ReadLegacyColors(node);

	// Everything else about the look; absent in files from before themes.
	tinyxml2::XMLElement* themeElement = node->FirstChildElement("Theme");
	if (themeElement != nullptr)
		THEME.ReadXml(themeElement);
	else {
		// Old builds cleared the window to the background's own color, so its
		// alpha never showed. Keep that look now that alpha is real.
		theme.colors[ImGuiCol_WindowBg].w = 1.0f;
	}
	if (THEME.Current().fontFile[0] != 0)
		THEME.SelectFont(THEME.Current().fontFile);
	THEME.ApplyStyle();

	for (int hotkeyID = 0; ; hotkeyID++) {

		int key[3] = { -1 };
		char name2[AUTO_HOTKEY_NAME_LEN] = { 0 };
		sprintf_s(name2, AUTO_HOTKEY_NAME_LEN, "HOTKEY%d", hotkeyID);

		ele = node->FirstChildElement(name2);

		if (ele == nullptr)
			break;

		for (int i = 0; i < 3; i++) {

			char temp[12] = { 0 };
			sprintf_s(temp, 12, "key%d", i + 1);

			attr = ele->FindAttribute(temp);

			if (attr == nullptr)
				break;

			attr->QueryIntValue(&key[i]);
		}

		if (ele->GetText() != nullptr) {
			strcpy_s(name2, ele->GetText());
		}

#if DEBUG_READ_XML == 1
		LogInstance.WriteLog("Read Hotkey %s, key1 = %d, key2 = %d, key3 = %d", name2, key[0], key[1], key[2]);
#endif

		HOTKEY.SetKeyByName(name2, key[0], key[1], key[2]);
	}

	return TRUE;
}

// Colors the way pre-theme builds saved them, one element each. All optional:
// newer files only keep writing them so an older build can still read the file.
void UiOption::ReadLegacyColors(tinyxml2::XMLNode* node) {

	MeterTheme& theme = THEME.Current();

	auto read = [node](const char* name, ImVec4& color) {
		tinyxml2::XMLElement* ele = node->FirstChildElement(name);
		if (ele == nullptr)
			return;
		ele->QueryfloatAttribute("r", &color.x);
		ele->QueryfloatAttribute("g", &color.y);
		ele->QueryfloatAttribute("b", &color.z);
		ele->QueryfloatAttribute("a", &color.w);
	};

	read("TextColor", theme.colors[ImGuiCol_Text]);
	read("WindowBgColor", theme.colors[ImGuiCol_WindowBg]);
	read("OutlineColor", theme.outlineColor);
	read("ActiveColor", theme.activeColor);
	read("InActiveColor", theme.inactiveColor);

	for (int i = 0; i < THEME_JOB_COUNT; i++) {
		char name[32] = { 0 };
		sprintf_s(name, "JobColor%d", i);
		read(name, theme.jobColors[i]);
	}
}

bool UiOption::SaveOption(bool skipWarning) {

	if (!_inited)
		return false;

	tinyxml2::XMLDocument doc;

	tinyxml2::XMLDeclaration* dec = doc.NewDeclaration();
	doc.LinkEndChild(dec);

	tinyxml2::XMLElement* root = doc.NewElement("SDM");
	doc.LinkEndChild(root);

	tinyxml2::XMLElement* option = doc.NewElement("Option");
	root->LinkEndChild(option);

	const MeterTheme& theme = THEME.Current();

	option->SetAttribute("IsTopMost", _isTopMost);
	option->SetAttribute("IsUseImage", _isUseImage);
	option->SetAttribute("IsVertical", _isVertical);
	option->SetAttribute("VerticalRows", _verticalRows.c_str());
	option->SetAttribute("GlobalScale", theme.fontScale);
	option->SetAttribute("TableScale", theme.tableFontScale);
	option->SetAttribute("ColumnScale", theme.columnFontScale);
	option->SetAttribute("K", _is1K);
	option->SetAttribute("M", _is1M);
	option->SetAttribute("Man", _is10K);
	option->SetAttribute("IsSoloMode", _isSoloMode);
	option->SetAttribute("DoHideName", _hideName);
	option->SetAttribute("TeamTA_LF", _teamTA_LF);
	option->SetAttribute("TeamTA_LF_Mode", _teamTA_LF_Mode);
	option->SetAttribute("IsSoloRankMode", _isSoloRankMode);
	option->SetAttribute("IsUseSaveData", _isUseSaveData);

	option->SetAttribute("CellPaddingX", theme.cellPadding.x);
	option->SetAttribute("CellPaddingY", theme.cellPadding.y);
	option->SetAttribute("BorderSize", theme.windowBorderSize);
	option->SetAttribute("WindowWidth", _windowWidth);
	option->SetAttribute("RefreshTime", _refreshTime);
	option->SetAttribute("LogFile", LogInstance.shouldLog);
	
	option->SetAttribute("LogMonsterStats", DAMAGEMETER.shouldLogMonsterStats);
	option->SetAttribute("TimerAcc", DAMAGEMETER.mswideness);

	option->SetAttribute("UseLangFile",_selectedLang);


	option->SetAttribute("UseFontFile", theme.fontFile);

	option->SetAttribute("IsDontSaveUnfinishedMaze", _isDontSaveUnfinishedMaze);

	option->SetAttribute("UnlockFps", _unlockFps);
	option->SetAttribute("FpsCap", _fpsCap);
	option->SetAttribute("UnlockFov", _unlockFov);

	RECT rect;
	GetWindowRect(UIWINDOW.GetHWND(), &rect);
	option->SetAttribute("WinPosX", (float)rect.left / THEME.GetDpiScale());
	option->SetAttribute("WinPosY", (float)rect.top / THEME.GetDpiScale());
	option->SetAttribute("HighDpi", _highDpi);

	// Older builds need every one of these, in this order, or they reset
	// all options; the <Theme> element below is what this build reads.
	auto writeColor = [&](const char* name, const ImVec4& color) {
		tinyxml2::XMLElement* ele = doc.NewElement(name);
		root->LinkEndChild(ele);
		ele->SetAttribute("r", color.x);
		ele->SetAttribute("g", color.y);
		ele->SetAttribute("b", color.z);
		ele->SetAttribute("a", color.w);
	};

	writeColor("TextColor", theme.colors[ImGuiCol_Text]);
	writeColor("WindowBgColor", theme.colors[ImGuiCol_WindowBg]);
	writeColor("OutlineColor", theme.outlineColor);
	writeColor("ActiveColor", theme.activeColor);
	writeColor("InActiveColor", theme.inactiveColor);

	for (int i = 0; i < THEME_JOB_COUNT; i++) {
		char buffer[32] = { 0 };
		sprintf_s(buffer, 32, "JobColor%d", i);
		writeColor(buffer, theme.jobColors[i]);
	}
	
	int hotkeyid = 0;

	for (auto itr = HOTKEY.begin(); itr != HOTKEY.end(); itr++) {

		char buffer[32] = { 0 };
		sprintf_s(buffer, 32, "HOTKEY%d", hotkeyid++);
		tinyxml2::XMLElement* hotkey = doc.NewElement(buffer);
		root->LinkEndChild(hotkey);

		hotkey->SetAttribute("key1", (*itr)->GetKey()[0]);
		hotkey->SetAttribute("key2", (*itr)->GetKey()[1]);
		hotkey->SetAttribute("key3", (*itr)->GetKey()[2]);
		hotkey->SetText((*itr)->GetName());
	}

	tinyxml2::XMLElement* themeElement = doc.NewElement("Theme");
	root->LinkEndChild(themeElement);
	THEME.WriteXml(doc, themeElement);

	doc.SaveFile(OPTION_FILE_NAME);
	return TRUE;
}

bool UiOption::SetBasicOption() {

	THEME.Reset();

	Helper();
	PLAYERTABLE.ResizeTalbe();
	PLAYERTABLE.FitColumns();
	_open = TRUE;

	return TRUE;
}



bool UiOption::ToggleTopMost() {

	_isTopMost = _isTopMost ? false : true;
	
	return SaveOption();
}

const ImU32 UiOption::GetJobColor(unsigned int index) {
	return THEME.GetJobColor(index);
}

const ImU32 UiOption::GetOutlineColor() {
	return ImGui::ColorConvertFloat4ToU32(THEME.Current().outlineColor);
}

const float& UiOption::GetFontScale() {
	return THEME.Current().fontScale;
}

const float& UiOption::GetColumnFontScale() {
	return THEME.Current().columnFontScale;
}

const float& UiOption::GetTableFontScale() {
	return THEME.Current().tableFontScale;
}

const ImVec4& UiOption::GetActiveColor() {
	return THEME.Current().activeColor;
}

const ImVec4& UiOption::GetInActiveColor() {
	return THEME.Current().inactiveColor;
}

bool UiOption::is1K() {
	return _is1K;
}

bool UiOption::is1M() {
	return _is1M;
}

bool UiOption::is10K() {
	return _is10K;
}

bool UiOption::isSoloMode(){
	return _isSoloMode;
}

bool UiOption::doHideName()
{
	return _hideName;
}

bool UiOption::isTopMost()
{
	return _isTopMost;
}
bool UiOption::isUseImage()
{
	return _isUseImage;
}
bool UiOption::isTeamTALF()
{
	return _teamTA_LF;
}

const int32_t& UiOption::TeamTALFMode()
{
	return _teamTA_LF_Mode;
}

bool UiOption::isSoloRankMode() {
	return _isSoloRankMode;
}

bool UiOption::isUseSaveData()
{
	if (_oriIsUseSaveData != _isUseSaveData)
		return _oriIsUseSaveData;
	return _isUseSaveData;
}



bool UiOption::isDontSaveUnfinishedMaze()
{
	return _isDontSaveUnfinishedMaze;
}

void UiOption::Update() {

	// Every frame: PlayerTable borrows a few style colors and puts them back,
	// and anything else that pokes the style gets undone here.
	THEME.ApplyStyle();

	ImFont* font = ImGui::GetFont();
	font->Scale = THEME.Current().fontScale;

	if (_open)
		OpenOption();
	else if (HOTKEY.isCapturing())
		HOTKEY.CancelCapture();

#if DEBUG_COLUMN_WIDTH == 1
	for (int i = 0; i < 8; i++)
		LogInstance.WriteLog("[DEBUG] [Column Width] [%d] [%f]", i, UIOPTION[i]);
#endif
}

const bool& UiOption::isOption() {
	return _open;
}

const float& UiOption::GetFramerate() {
	return _framerate;
}

void UiOption::SetFramerate(float i) {

	if (i < 0)
		i = 0;
	else if (i > 4)
		i = 4;

	_framerate = i;
}

const ImVec4& UiOption::GetWindowBGColor() {
	return THEME.Current().colors[ImGuiCol_WindowBg];
}

const float& UiOption::GetWindowWidth() {
	return _windowWidth;
}

void UiOption::SetWindowWidth(const float& width) {
	_windowWidth = width;
}

const float& UiOption::GetRefreshTime() {
	return _refreshTime;
}



bool UiOption::WantsHighDpi() {

	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(OPTION_FILE_NAME) != tinyxml2::XML_SUCCESS)
		return false;

	tinyxml2::XMLElement* root = doc.FirstChildElement("SDM");
	tinyxml2::XMLElement* option = root ? root->FirstChildElement("Option") : nullptr;
	return option != nullptr && option->IntAttribute("HighDpi", 0) != 0;
}

const char* UiOption::GetFontFile() {
	return THEME.Current().fontFile;
}
