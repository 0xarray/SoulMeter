#include "pch.h"
#include ".\UI\PlayerTable.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Damage Meter\History.h"
#include ".\Damage Meter\MySQLite.h"
#include ".\UI\Option.h"
#include ".\UI\UiWindow.h"
#include ".\UI\UtillWindow.h"
#include ".\UI\PlotWindow.h"
#include ".\Soulworker Packet\PacketInfo.h"
#include ".\Soulworker Packet\SWPacketMaker.h"
#include ".\Soulworker Packet\PipeReceiver.h"
#include "SWConfig.h"
#include ".\UI\DX11.h"

// Every meter column in its fixed order. The rest of a row is drawn in this
// order too, and vertical mode turns these into rows. Only the basics show
// until the header menu (or the vertical row menu) saves a choice.
struct MeterColumn { const char* key; bool shown; };
static const MeterColumn kColumns[] = {
	{ "STR_TABLE_NAME", true },
	{ "STR_TABLE_DPS", true },
	{ "STR_TABLE_DAMAGE_PERCENT", true },
	{ "STR_TABLE_TOTAL_DAMAGE", true },
	{ "STR_TABLE_TOTAL_HIT", true },
	{ "STR_TABLE_CRIT_RATE", true },
	{ "STR_TABLE_HIT_PER_SECOND", false },
	{ "STR_TABLE_CRIT_HIT_PER_SECOND", false },
	{ "STR_TABLE_SKILL_PER_SECOND", false },
	{ "STR_TABLE_MAX_COMBO", true },
	{ "STR_TABLE_ATTACK_CDMG_SUM", false },
	{ "STR_TABLE_SOUL_GAUGE", false },
	{ "STR_TABLE_ATTACK_SPEED", false },
	{ "STR_TABLE_ARMOR_BREAK", false },
	{ "STR_TABLE_BOSS_DAMAGE", false },
	{ "STR_TABLE_STAMINA", false },
	{ "STR_TABLE_SOUL_VAPOR", false },
	{ "STR_TABLE_SOULSTONE_PERCENT", false },
	{ "STR_TABLE_SOULSTONE_PROC", false },
	{ "STR_TABLE_SOULSTONE_DAMAGE", false },
	{ "STR_TABLE_AVERAGE_AB", false },
	{ "STR_TABLE_AVERAGE_AB_U", false },
	{ "STR_TABLE_AVERAGE_BD", false },
	{ "STR_TABLE_MISS", false },
	{ "STR_TABLE_MISS_RATE", false },
	{ "STR_TABLE_PARTIAL", false },
	{ "STR_TABLE_GET_HIT_INCLUDE_ZERO_DAMAGE", false },
	{ "STR_TABLE_GET_HIT", false },
	{ "STR_TABLE_GET_HIT_BS", false },
	{ "STR_TABLE_EVADE_RATE_A", false },
	{ "STR_TABLE_EVADE_RATE_B", false },
	{ "STR_TABLE_GIGA_ENLIGHTEN", false },
	{ "STR_TABLE_TERA_ENLIGHTEN", false },
	{ "STR_TABLE_TERA_FEVER", false },
	{ "STR_TABLE_TERA_FURY", false },
	{ "STR_TABLE_TERA_BACKSTEP", false },
	{ "STR_TABLE_TERA_TECHNIC", false },
	{ "STR_TABLE_LOSED_HP", false },
	{ "STR_TABLE_DODGE_COUNT", false },
	{ "STR_TABLE_DEATH", false },
	{ "STR_TABLE_FULL_AB_TIME", false },
	{ "STR_TABLE_FULL_AB_PERCENT", false },
	{ "STR_TABLE_GIGA_ENLIGHTEN_SKILL_PERCENT", false },
	{ "STR_TABLE_TERA_ENLIGHTEN_SKILL_PERCENT", false },
	{ "STR_TABLE_AGGRO_TIME_PERCENT", false },
	{ "STR_TABLE_FULL_AS_TIME", false },
	{ "STR_TABLE_FULL_AS_PERCENT", false },
	{ "STR_TABLE_AVG_AS_PERCENT", false },
};

static const char* T(const char* key)
{
	return LANGMANAGER.GetText(key).data();
}

// Expands $name / ${name} from vars; $$ is a literal $ and unknown names are
// left as typed. Runs of '#' collapse to one: "##" would hide the rest of the
// title and "###" would change the window's ID, losing its position.
static std::string ExpandTitle(const char* fmt, const std::vector<std::pair<const char*, std::string>>& vars)
{
	std::string out;
	for (const char* p = fmt; *p;) {
		if (*p == '#') {
			if (out.empty() || out.back() != '#')
				out += '#';
			p++;
			continue;
		}
		if (*p != '$') {
			out += *p++;
			continue;
		}
		if (p[1] == '$') {
			out += '$';
			p += 2;
			continue;
		}

		bool braced = p[1] == '{';
		const char* start = p + (braced ? 2 : 1);
		const char* end = start;
		while (isalpha((unsigned char)*end))
			end++;
		if (end == start || (braced && *end != '}')) {
			out += *p++;
			continue;
		}

		std::string name(start, end);
		auto var = std::find_if(vars.begin(), vars.end(), [&](const auto& v) { return _stricmp(v.first, name.c_str()) == 0; });
		const char* next = end + (braced ? 1 : 0);
		if (var == vars.end())
			out.append(p, next);
		else
			out += var->second;
		p = next;
	}
	return out;
}

PlayerTable::PlayerTable() : _tableResize(0), _globalFontScale(0), _columnFontScale(0), _tableFontScale(0), _curWindowSize(0), _tableTime(0), _accumulatedTime(0), _nextWindowIndex(0)
{

}

PlayerTable::~PlayerTable() {
	ClearSelectInfo(TRUE);
}

void PlayerTable::ClearTable() {
	ClearSelectInfo(FALSE);
}

void PlayerTable::ClearSelectInfo(bool all) {

	for (auto itr = _selectInfo.begin(); itr != _selectInfo.end();) {

		// An open detail window for the local player survives a reset: its id
		// is refreshed by FollowMyID, so reopening it every run is just noise.
		if (!all && (*itr)->_isMe && (*itr)->_isSelected) {
			(*itr)->_specificInfo->ResetMonsterSelection();
			itr++;
			continue;
		}

		delete (*itr)->_specificInfo;
		delete (*itr);

		itr = _selectInfo.erase(itr);
	}

	_curWindowSize = 0;
}

void PlayerTable::SetupFontScale() {

	ImFont* font = ImGui::GetFont();

	_globalFontScale = font->Scale;
	_columnFontScale = _globalFontScale * UIOPTION.GetColumnFontScale();
	_tableFontScale = _globalFontScale * UIOPTION.GetTableFontScale();
}

void PlayerTable::ResizeTalbe() {
	_tableResize = TRUE;
}

void PlayerTable::Update() {

	DAMAGEMETER.GetLock();
	{
		ImGuiStyle& style = ImGui::GetStyle();

		ImVec2 prevWindowPadding = style.WindowPadding;

		style.WindowPadding.x = 0;
		style.WindowPadding.y = 0;

		ImVec4 prevInActiveColor = style.Colors[ImGuiCol_TitleBg];
		ImVec4 prevActiveColor = style.Colors[ImGuiCol_TitleBgActive];

		if (DAMAGEMETER.isRun()) {
			style.Colors[ImGuiCol_TitleBg] = UIOPTION.GetActiveColor();
			style.Colors[ImGuiCol_TitleBgActive] = UIOPTION.GetActiveColor();
		}
		else {
			style.Colors[ImGuiCol_TitleBg] = UIOPTION.GetInActiveColor();
			style.Colors[ImGuiCol_TitleBgActive] = UIOPTION.GetInActiveColor();
		}

		_accumulatedTime += UIWINDOW.GetDeltaTime();

		if (_accumulatedTime > UIOPTION.GetRefreshTime()) {
			_tableTime = static_cast<float>(((double)DAMAGEMETER.GetTime()) / 1000);
			_accumulatedTime = 0;
		}

		SetupFontScale();

		// The other layout needs a different height; start over and regrow.
		if (UIOPTION.isVertical() != _wasVertical) {
			_wasVertical = UIOPTION.isVertical();
			_curWindowSize = 0;
			_tableResize = TRUE;
		}

		// Before the table is drawn, so a click on the YOU row this frame hits
		// the refreshed entry instead of opening a second window for it.
		FollowMyID();

		ImGuiWindowFlags windowFlag = ImGuiWindowFlags_None;
		windowFlag |= (ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);

		if (!UIOPTION.isOption())
			windowFlag = windowFlag | ImGuiWindowFlags_NoResize;

		char title[1024] = { 0 };
		// Timer accuracy picks 1-3 fraction digits; they must keep their leading
		// zeros or 1.045s reads as "01.4".
		int msDigits = ImClamp(DAMAGEMETER.mswideness, 1, 3);
		unsigned int miliseconds = ((unsigned int)DAMAGEMETER.GetTime() % 1000);
		if (msDigits == 1)
			miliseconds /= 100;
		else if (msDigits == 2)
			miliseconds /= 10;
		char milisecondsstring[4] = { 0 };
		sprintf_s(milisecondsstring, "%0*u", msDigits, miliseconds);

		if (!PipeReceiverIsConnected()) {
			// game not hooked yet - the player launches it themselves
			sprintf_s(title, 1024, "%s [v%s]  %s ###DamageMeter",
				LANGMANAGER.GetText("STR_WAITING_FOR_GAME").data(),
				APP_VERSION,
				SOULMETER_DISCORD_INVITE
			);
		}
		else if (*UIOPTION.GetTitleFormat()) {
			char time[32] = { 0 };
			sprintf_s(time, "%02u:%02u.%s",
				(unsigned int)DAMAGEMETER.GetTime() / (60 * 1000), (unsigned int)(DAMAGEMETER.GetTime() / 1000) % 60, milisecondsstring);

			std::string text = ExpandTitle(UIOPTION.GetTitleFormat(), {
				{ "map", DAMAGEMETER.GetWorldName() },
				{ "time", time },
				{ "version", APP_VERSION "@Rainy" },
				{ "ping", std::to_string(DAMAGEMETER.GetPing()) },
			});
			sprintf_s(title, 1024, "%.990s ###DamageMeter", text.c_str());
		}
		else {
			sprintf_s(title, 1024, "%s - %02d:%02d.%s [v%s_@Rainy] %s: %ums ###DamageMeter",
				DAMAGEMETER.GetWorldName(),
				(unsigned int)DAMAGEMETER.GetTime() / (60 * 1000), (unsigned int)(DAMAGEMETER.GetTime() / 1000) % 60, milisecondsstring,
				APP_VERSION,
				LANGMANAGER.GetText("STR_MENU_PING").data(),
				DAMAGEMETER.GetPing()
			);
		}

		// The title is drawn inside Begin, so the effect only has to span it.
		bool titleEffect = THEME.Current().titleEffect && THEME.PushTextEffect();
		ImGui::Begin(title, 0, windowFlag);
		THEME.PopTextEffect(titleEffect);
		// The meter took its zero padding in Begin; popups opened from it
		// (the menu, the column menu) need the normal one.
		style.WindowPadding = prevWindowPadding;
		{
			if (!UIOPTION.isOption() || _tableResize)
				SetWindowSize();

			if (UIOPTION.isOption())
				StoreWindowWidth();

			SetMainWindowSize();

			BeginPopupMenu();

			bool textEffect = THEME.PushTextEffect();
			ImGui::TextAlignCenter::SetTextAlignCenter();
			{
				SetupTable();
			}
			ImGui::TextAlignCenter::UnSetTextAlignCenter();
			THEME.PopTextEffect(textEffect);
		}
		ImGui::End();

		ShowSelectedTable();

		style.Colors[ImGuiCol_TitleBg] = prevInActiveColor;
		style.Colors[ImGuiCol_TitleBgActive] = prevActiveColor;
	}
	DAMAGEMETER.FreeLock();
}

void PlayerTable::SetWindowSize() {

	// Keeps going until the content fits, so a resize requested while the
	// options are open (and this is not called every frame) still finishes.
	if (ImGui::GetScrollMaxY() > 0)
		_curWindowSize += ImGui::GetScrollMaxY();
	else if (_curWindowSize > 0)
		_tableResize = FALSE;

	ImGui::SetWindowSize(ImVec2(UIOPTION.GetWindowWidth() * THEME.GetDpiScale(), FLOOR(_curWindowSize)));
}

void PlayerTable::SetMainWindowSize() {

	auto pos = ImGui::GetWindowPos();
	auto size = ImGui::GetWindowSize();


	if (UIOPTION.isTopMost()) {
		SetWindowPos(UIWINDOW.GetHWND(), HWND_TOPMOST, static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(size.x + 1), static_cast<int>(size.y + 1), SWP_NOACTIVATE);
	}
	else {
		SetWindowPos(UIWINDOW.GetHWND(), HWND_NOTOPMOST, static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(size.x + 1), static_cast<int>(size.y + 1), SWP_NOACTIVATE);
	}

	//SetWindowPos(UIWINDOW.GetHWND(), HWND_NOTOPMOST, pos.x, pos.y, size.x + 1, size.y + 1, SWP_NOACTIVATE);
	
}

void PlayerTable::StoreWindowWidth() {
	UIOPTION.SetWindowWidth(ImGui::GetWindowSize().x / THEME.GetDpiScale());
}

void PlayerTable::BeginPopupMenu() {

	if (ImGui::BeginPopupContextItem()) {
		//if (ImGui::MenuItem(STR_MENU_RESUME)) {
		//	//
		//	//DAMAGEMETER.Toggle();
		//}

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_RESET").data())) {
			DAMAGEMETER.Clear();
			PLAYERTABLE.ClearTable();
		}

		ImGui::Separator();

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_TOPMOST").data(), nullptr, UIOPTION.isTopMost())) {
			UIOPTION.ToggleTopMost();
		}

		if (UIOPTION.isVertical() && ImGui::BeginMenu(T("STR_MENU_VERTICAL_ROWS"))) {
			// Stays open so several rows can be toggled in one go.
			ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
			for (int i = 1; i < IM_ARRAYSIZE(kColumns); i++) {
				if (ImGui::MenuItem(T(kColumns[i].key), nullptr, VerticalRowShown(i)))
					ToggleVerticalRow(i);
			}
			ImGui::PopItemFlag();
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_UTILL").data())) {
			UTILLWINDOW.OpenWindow();
		}

		bool history_open = false;
		if (HISTORY.size() > 0)
			history_open = true;

		if (ImGui::BeginMenu(LANGMANAGER.GetText("STR_MENU_HISTORY").data(), history_open)) {
			HISTORY.GetLock();
			{
				int32_t i = static_cast<int32_t>(HISTORY.size()), iSelectedID = 0;
				bool bChangeHistory = false;
				HISTORY_INFO* pSelectedHI = nullptr;
				for (auto itr = HISTORY.rbegin(); itr != HISTORY.rend(); itr++)
				{
					HISTORY_INFO* pHI = (HISTORY_INFO*)*itr;

					char label[512] = { 0 };
					char mapName[MAX_MAP_LEN] = { 0 };
					SWDB.GetMapName(pHI->_worldID, mapName, MAX_MAP_LEN);

					sprintf_s(label, "%d.[%02d:%02d:%02d] %s - %02d:%02d.%01d###history%d",
						i,
						pHI->_saveTime->wHour, pHI->_saveTime->wMinute, pHI->_saveTime->wSecond,
						mapName,
						(unsigned int)pHI->_time / (60 * 1000), (unsigned int)(pHI->_time / 1000) % 60, (unsigned int)pHI->_time % 1000 / 100,
						i
					);

					i--;

					if (ImGui::Selectable(label, DAMAGEMETER.GetCurrentHistoryId() == i) && !DAMAGEMETER.isRun()) 
					{
						if (!DAMAGEMETER.isRun()) {
							bChangeHistory = true;
							iSelectedID = i;
							pSelectedHI = pHI;
						}
					}
				}

				if (bChangeHistory)
				{
					DAMAGEMETER.SetCurrentHistoryId(iSelectedID);
					DAMAGEMETER.SetHistory((LPVOID)pSelectedHI);
					bChangeHistory = false;
				}
				HISTORY.FreeLock();
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_MEOW").data())) {
			PLOTWINDOW.OpenWindow();
		}

		ImGui::Separator();

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_OPTIONS").data())) {
			UIOPTION.OpenOption();
		}

		ImGui::Separator();

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_EXIT").data())) {
			PostMessage(UIWINDOW.GetHWND(), WM_CLOSE, 0, 0);
		}

		ImGui::EndPopup();
	}
}

void PlayerTable::SetupTable() {

	if (UIOPTION.isVertical()) {
		SetupVerticalTable();
		return;
	}

	ImGuiTableFlags tableFlags = ImGuiTableFlags_None;
	tableFlags |= (ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable);
	tableFlags |= THEME.TableFlags();

	if (ImGui::BeginTable("###Player Table", IM_ARRAYSIZE(kColumns), tableFlags)) {

		ImGuiTableColumnFlags columnFlags = ImGuiTableColumnFlags_None;
		columnFlags |= ImGuiTableColumnFlags_NoSort;

		ImGui::SetWindowFontScale(_columnFontScale);

		ImGui::TableSetupColumn(T(kColumns[0].key), ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoClip | ImGuiTableColumnFlags_WidthFixed | columnFlags, -1);
		for (int i = 1; i < IM_ARRAYSIZE(kColumns); i++)
			ImGui::TableSetupColumn(T(kColumns[i].key), columnFlags | ImGuiTableColumnFlags_WidthFixed | (kColumns[i].shown ? 0 : ImGuiTableColumnFlags_DefaultHide), -1);

		ImGui::TableHeadersRow();

		// DPS reads "-" for the first second, so waiting keeps the fit from
		// sizing that column to its header.
		if (_fitColumns && _tableTime >= 1) {
			ImGui::TableSetColumnWidthAutoAll(ImGui::GetCurrentTable());
			_fitColumns = false;
		}

		float window_width = ImGui::GetWindowWidth();

		ImGui::SetWindowFontScale(_tableFontScale);

		UpdateTable(window_width);

		ImGui::SetWindowFontScale(_globalFontScale);

		ImGui::EndTable();
	}

}

void PlayerTable::UpdateTable(float windowWidth) {
	uint64_t max_Damage = 1;
	char comma[128] = { 0 }; char label[128] = { 0 };

	for (auto itr = DAMAGEMETER.begin(); itr != DAMAGEMETER.end(); itr++) {

		// 
		if (UIOPTION.isSoloMode() && DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU")) {
			continue;
		}

		// Skip Unknown Player
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) == LANGMANAGER.GetText("PLAYER_NAME_CANT_FIND").data())
			continue;

		// 
		if (itr == DAMAGEMETER.begin())
			max_Damage = (*itr)->GetDamage();

		float damage_percent = static_cast<float>((double)(*itr)->GetDamage() / (double)max_Damage);

		if (damage_percent > 1)
			damage_percent = 1;
		else if (damage_percent < 0)
			damage_percent = 0;

		const uint32_t playerId = (*itr)->GetID();
		const ImU32 jobColor = UIOPTION.GetJobColor(DAMAGEMETER.GetPlayerJob(playerId));
		uint64_t milliTableTime = (uint64_t)((double)_tableTime * 1000);

		// NAME
		const char* playerName = DAMAGEMETER.GetPlayerName(playerId);
		if (UIOPTION.doHideName() && playerName != LANGMANAGER.GetText("STR_TABLE_YOU").data()) {
			playerName = "";
		}

		const MeterTheme& theme = THEME.Current();
		ImVec4 nameColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		if (playerId == DAMAGEMETER.GetAggro()) {
			nameColor = theme.aggroColor;
		}
		else if (playerId == DAMAGEMETER.GetOwnerID(DAMAGEMETER.GetAggro())) {
			nameColor = theme.aggroOwnerColor;
		}

		if (DAMAGEMETER.PlayerInAwakening(playerId)) {
			nameColor = theme.awakeningColor;
		}

		if (_collecting) {
			_vertical.push_back({ playerId, playerName, nameColor, jobColor, DAMAGEMETER.GetPlayerJob(playerId) });
		}
		else {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			DrawBar(windowWidth, damage_percent, jobColor);

			ImGui::PushStyleColor(ImGuiCol_Text, nameColor);
			bool useImage = UIOPTION.isUseImage();
			Texture playerTexture = DIRECTX11.getcharacterTexture(DAMAGEMETER.GetPlayerJob(playerId));

			// Centered by hand: render-side centering would ignore the image.
			// Never left of the cell, or a narrow column cuts the name's start off
			// and auto-fit measures only half of it.
			float nameWidth = ImGui::CalcTextSize(playerName).x;
			if (useImage && playerTexture.ptr)
				nameWidth += playerTexture.xSize;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (ImGui::GetColumnWidth() - nameWidth) * 0.5f));
			if (useImage && playerTexture.ptr) {
				ImGui::Image((void*)playerTexture.ptr, ImVec2((float)playerTexture.xSize, (float)playerTexture.ySize));
				ImGui::SameLine();
			}
			ImGui::TextAlignCenter::UnSetTextAlignCenter(); //some gay custom function, breaks text align with image
			if (ImGui::Selectable(playerName, false, ImGuiSelectableFlags_SpanAllColumns))
				ToggleSelectInfo(playerId);

			ImGui::TextAlignCenter::SetTextAlignCenter();
			ImGui::PopStyleColor();

			ImGui::TableNextColumn();
		}


		// DPS
		if (_tableTime < 1) {
			Cell("-");
		}
		else {
			double dps = ((double)(*itr)->GetDamage()) / _tableTime;
			if (UIOPTION.is1K()) 
				dps /= 1000;
			else if (UIOPTION.is1M()) 
				dps /= 1000000;
			else if (UIOPTION.is10K())
				dps /= 10000;
			if (UIOPTION.is1M()) 
				TextCommmaIncludeDecimal(dps, sizeof(comma), comma);
			else {
				sprintf_s(label, 128, "%.0lf", dps);
				TextCommma(label, comma);
			}
			if (UIOPTION.is1K())
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K").data());
			else if (UIOPTION.is1M()) 
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1M").data());
			else if (UIOPTION.is10K())
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K").data());
			Cell(comma);

			bool isFirstElement = ((itr - DAMAGEMETER.begin()) == 0);
			PLOTWINDOW.AddData((*itr)->GetID(), DAMAGEMETER.GetPlayerName((*itr)->GetID()), dps, _tableTime, isFirstElement);
		}
		

		NextCell();

		// D%
		if (DAMAGEMETER.GetPlayerTotalDamage() == 0) {
			sprintf_s(label, 128, "%.0lf", (float)0);
			Cell(label);
		}
		else {
			sprintf_s(label, 128, "%.0lf", ((double)(*itr)->GetDamage() / (double)DAMAGEMETER.GetPlayerTotalDamage()) * 100);
			Cell(label);
		}

		NextCell();

		// DAMAGE
		uint64_t damage = (*itr)->GetDamage();
		if (UIOPTION.is1K())
			damage /= 1000;
		else if (UIOPTION.is1M())
			damage /= 1000000;
		else if (UIOPTION.is10K())
			damage /= 10000;
		sprintf_s(label, 128, "%llu", damage);
		TextCommma(label, comma);
		if (UIOPTION.is1K())
			strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K").data());
		else if (UIOPTION.is1M())
			strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1M").data());
		else if (UIOPTION.is10K())
			strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K").data());
		Cell(comma);

		NextCell();

		// HIT
		sprintf_s(label, 128, "%d", (*itr)->GetHitCount());
		TextCommma(label, comma);
		Cell(comma);

		NextCell();

		// CRIT
		float crit = 0;

		if ((*itr)->GetHitCount() != 0)
			crit = (float)(*itr)->GetCritHitCountForCritRate() / (float)(*itr)->GetHitCountForCritRate() * 100;

		sprintf_s(label, 128, "%.1f", crit);
		Cell(label);

		NextCell();

		// HIT/S
		if (_tableTime == (float)0) {
			sprintf_s(label, 128, "%d", 0);
			Cell(label);
		}
		else {
			sprintf_s(label, 128, "%.2lf", (double)(*itr)->GetHitCount() / _tableTime);
			Cell(label);

		}
		NextCell();

		//CRIT/S
		if (_tableTime == (float)0) {
			sprintf_s(label, 128, "%d", 0);
			Cell(label);
		}
		else {
			sprintf_s(label, 128, "%.2lf", (double)(*itr)->GetCritHitCount() / _tableTime);
			Cell(label);
		}

		NextCell();

		// Skill/s
		if (_tableTime == 0.0f) {
			sprintf_s(label, 128, "-");
			Cell(label);
		}
		else {
			sprintf_s(label, 128, "%.2lf", (double)(*itr)->GetSkillUsed() / _tableTime);
			Cell(label);
		}

		NextCell();


		// MAXC
		sprintf_s(label, 128, "%d", (*itr)->GetMaxCombo());
		TextCommma(label, comma);
		Cell(comma);

		NextCell();

		uint32_t playerID = (*itr)->GetID();
		SWDamageMeter::SW_PLAYER_METADATA* playerMetaData = DAMAGEMETER.GetPlayerMetaData(playerID);

		// Not found stat data
		if (playerMetaData == NULL) {
			continue;
		}

		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime < 1) {
			// Attack+Crit SUM
			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();

			// SG
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::SG));
			Cell(label);
			NextCell();
			// AttackSpeed
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::AttackSpeed));
			Cell(label);
			NextCell();

			// AB
			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();
			// BD
			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();
			// STAM
			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();
			// SV
			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();
		}
		else {
			// Attack+Crit SUM
			// TODO: Re-enable M option if we ever get there 
			double gongchihap = (double)playerMetaData->GetStat(StatType::MaxAttack) + (double)playerMetaData->GetStat(StatType::CritDamage);
			if (UIOPTION.is1K())
				gongchihap /= 1000;
			else if (UIOPTION.is1M()) {
				if (!strcmp(LANGMANAGER.GetText("STR_DISPLAY_DEFAULT_UNIT").data(), "K"))
					gongchihap /= 1000;
				else if (!strcmp(LANGMANAGER.GetText("STR_DISPLAY_DEFAULT_UNIT").data(), "10K"))
					gongchihap /= 10000;
				// gongchihap /= 1000000
			}
			else if (UIOPTION.is10K())
				gongchihap /= 10000;
			sprintf_s(label, 128, "%.0f", gongchihap);
			TextCommma(label, comma);
			if (UIOPTION.is1K())
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K").data());
			else if (UIOPTION.is1M()) {
				if (!strcmp(LANGMANAGER.GetText("STR_DISPLAY_DEFAULT_UNIT").data(), "K"))
					strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K").data());
				else if (!strcmp(LANGMANAGER.GetText("STR_DISPLAY_DEFAULT_UNIT").data(), "10K"))
					strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K").data());
				// strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1M"));
			}
			else if (UIOPTION.is10K())
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K").data());
			Cell(comma);
			NextCell();

			static float statTmp = 0;

			// SG
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::SG));
			Cell(label);

			NextCell();
			// AttackSpeed
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::AttackSpeed));
			Cell(label);

			NextCell();
			// AB
			statTmp = playerMetaData->GetStat(StatType::ArmorBreak);
			sprintf_s(label, 128, "%.1f", statTmp);
			PLOTWINDOW.AddAbData(statTmp, _tableTime);
			Cell(label);
			
			NextCell();
			// BD
			statTmp = playerMetaData->GetSpecialStat(SpecialStatType::BossDamageAddRate);
			sprintf_s(label, 128, "%.1f", statTmp);
			PLOTWINDOW.AddBdData(statTmp, _tableTime);
			Cell(label);

			NextCell();
			// stamina
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::Stamina));
			Cell(label);

			NextCell();
			// SV
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::SV));
			Cell(label);
			NextCell();
		}



		// Soulstone all percent
		double soulstoneAllPercent;
		if ((*itr)->GetDamage() == 0) {
			soulstoneAllPercent = 0.0;
		}
		else {
			soulstoneAllPercent = ((double)(*itr)->GetSoulstoneDamage()) / (*itr)->GetDamage() * 100;
		}

		sprintf_s(label, 128, "%.1f", soulstoneAllPercent);
		Cell(label);
		NextCell();

		// Soulstone crit rate
		double soulstoneProcRate;
		if ((*itr)->GetCritHitCountForCritRate() == 0) {
			soulstoneProcRate = 0.0;
		}
		else {
			soulstoneProcRate = ((double)(*itr)->GetSoulstoneCount()) / (*itr)->GetHitCountForCritRate() * 100;
		}

		sprintf_s(label, 128, "%.1f", soulstoneProcRate);
		Cell(label);
		NextCell();

		// Soulstone damage %
		double soulstoneDamage;
		if ((*itr)->GetDamageForSoulstone() == 0) {
			soulstoneDamage = 0.0;
		}
		else {
			soulstoneDamage = ((double)(*itr)->GetSoulStoneDamageForSoulstone()) / (*itr)->GetDamageForSoulstone() * 100;
		}
		sprintf_s(label, 128, "%.1f", soulstoneDamage);
		Cell(label);
		NextCell();

		// history data tmp
		static double savedResultAB = 0;

		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0) {
			sprintf_s(label, 128, "-");
		}
		else if (DAMAGEMETER.isHistoryMode()) {
			savedResultAB = (*itr)->GetHistoryAvgAB();
			sprintf_s(label, 128, "%.1f", savedResultAB);
		}
		else {

			if ((int64_t)(milliTableTime - playerMetaData->_avgABPreviousTime) < 0) {
				sprintf_s(label, 128, "%.1f", savedResultAB);
			}
			else {
				uint64_t timeDifference = (milliTableTime - playerMetaData->_avgABPreviousTime);
				double currentAB = playerMetaData->GetStat(StatType::ArmorBreak);
				currentAB = currentAB > 100.0 ? 100.0 : currentAB;
				uint64_t calculatedAvgAB = static_cast<uint64_t>((playerMetaData->_avgABSum + timeDifference * currentAB));

				savedResultAB = (double)calculatedAvgAB / milliTableTime;
				sprintf_s(label, 128, "%.1f", savedResultAB);
			}
		}

		Cell(label);
		NextCell();

		// history data tmp
		static double savedResultABU = 0;

		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0) {
			sprintf_s(label, 128, "-");
		}
		else if (DAMAGEMETER.isHistoryMode()) {
			savedResultABU = (*itr)->GetHistoryAvgABU();
			sprintf_s(label, 128, "%.1f", savedResultABU);
		}
		else {

			if ((int64_t)(milliTableTime - playerMetaData->_avgABPreviousTimeU) < 0) {
				sprintf_s(label, 128, "%.1f", savedResultABU);
			}
			else {
				uint64_t timeDifference = (milliTableTime - playerMetaData->_avgABPreviousTimeU);
				double currentABU = playerMetaData->GetStat(StatType::ArmorBreak);
				uint64_t calculatedAvgABU = static_cast<uint64_t>((playerMetaData->_avgABSumU + timeDifference * currentABU));

				savedResultABU = (double)calculatedAvgABU / milliTableTime;
				sprintf_s(label, 128, "%.1f", savedResultABU);
			}
		}

		Cell(label);
		NextCell();

		// BD
		static double savedResultBD = 0;

		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0) {
			sprintf_s(label, 128, "-");
		}
		else if (DAMAGEMETER.isHistoryMode()) {
			savedResultBD = (*itr)->GetHistoryAvgBD();
			sprintf_s(label, 128, "%.1f", savedResultBD);
		}
		else {

			if ((int64_t)(milliTableTime - playerMetaData->_avgBDPreviousTime) < 0) {
				sprintf_s(label, 128, "%.1f", savedResultBD);
			}
			else {
				uint64_t timeDifference = (milliTableTime - playerMetaData->_avgBDPreviousTime);
				double currentBD = playerMetaData->GetSpecialStat(SpecialStatType::BossDamageAddRate);
				uint64_t calculatedAvgBD = static_cast<uint64_t>((playerMetaData->_avgBDSum + timeDifference * currentBD));

				savedResultBD = (double)calculatedAvgBD / milliTableTime;
				sprintf_s(label, 128, "%.1f", savedResultBD);
			}
		}

		Cell(label);
		NextCell();
		// Miss
		sprintf_s(label, 128, "%d", (*itr)->GetMissCount());
		TextCommma(label, comma);
		Cell(comma);

		NextCell();
		// Miss%
		if ((*itr)->GetMissCount() == 0 || (*itr)->GetHitCountForCritRate() == 0) {
			sprintf_s(label, 128, "%.1f", 0.0);
		}
		else {
			sprintf_s(label, 128, "%.1f", (double)(*itr)->GetMissCount() / (*itr)->GetHitCountForCritRate() * 100);
		}

		Cell(label);
		NextCell();

		// MissDamageRate
		sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::PartialDamage));
		Cell(label);
		NextCell();

		// GetHit(Include Zero Damage)
		sprintf_s(label, 128, "%d", (*itr)->GetGetHitAll());
		TextCommma(label, comma);
		Cell(comma);

		NextCell();

		// GetHit
		sprintf_s(label, 128, "%d", (*itr)->GetGetHit());
		TextCommma(label, comma);
		Cell(comma);

		NextCell();

		// GetHit(BS)
		sprintf_s(label, 128, "%d", (*itr)->GetGetHitBS());
		TextCommma(label, comma);
		Cell(comma);

		NextCell();


		// Evade A
		if ((*itr)->GetGetHitAll() == 0) {
			sprintf_s(label, 128, "-");
		}
		else {
			sprintf_s(label, 128, "%.1f%%", (double)(*itr)->GetGetHitMissed() / (*itr)->GetGetHitAll() * 100);
		}
		Cell(label);
		NextCell();

		// Evade B
		if ((*itr)->GetGetHit() == 0) {
			sprintf_s(label, 128, "-");
		}
		else {
			sprintf_s(label, 128, "%.1f%%", (double)(*itr)->GetGetHitMissedReal() / (*itr)->GetGetHit() * 100);
		}
		Cell(label);
		NextCell();


		// Enlighten
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0) {
			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();

			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();
		}
		else {
			sprintf_s(label, 128, "%u", (*itr)->GetGigaEnlighten());
			Cell(label);
			NextCell();

			sprintf_s(label, 128, "%u", (*itr)->GetTeraEnlighten());
			Cell(label);
			NextCell();
		}

		// Brooch procs (Fever / Fury / Backstep / Technic)
		{
			const BroochProc broochProcs[] = { BROOCH_FEVER, BROOCH_FURY, BROOCH_BACKSTEP, BROOCH_TECHNIC };
			bool isYou = DAMAGEMETER.GetPlayerName((*itr)->GetID()) == LANGMANAGER.GetText("STR_TABLE_YOU").data() && _tableTime != 0;

			for (BroochProc type : broochProcs) {
				if (!isYou)
					sprintf_s(label, 128, "-");
				else
					sprintf_s(label, 128, "%u", (*itr)->GetBroochProc(type));

				Cell(label);
				NextCell();
			}
		}

		// HP
		// TODO: Re-enable M if we ever get there
		double losedHP = 0.0;
		if (DAMAGEMETER.isHistoryMode()) {
			losedHP = (*itr)->GetHistoryLosedHP();
		}
		else {
			losedHP = playerMetaData->_losedHp;
		}
		if (UIOPTION.is1K())
			losedHP /= 1000;
		else if (UIOPTION.is1M()) {
			if (!strcmp(LANGMANAGER.GetText("STR_DISPLAY_DEFAULT_UNIT").data(), "K"))
				losedHP /= 1000;
			else if (!strcmp(LANGMANAGER.GetText("STR_DISPLAY_DEFAULT_UNIT").data(), "10K"))
				losedHP /= 10000;
			// losedHP /= 1000000;
		}
		else if (UIOPTION.is10K())
			losedHP /= 10000;
		sprintf_s(label, 128, "%.0f", losedHP);
		TextCommma(label, comma);

		if (UIOPTION.is1K())
			strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K").data());
		else if (UIOPTION.is1M())
		{
			if (!strcmp(LANGMANAGER.GetText("STR_DISPLAY_DEFAULT_UNIT").data(), "K"))
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1K").data());
			else if (!strcmp(LANGMANAGER.GetText("STR_DISPLAY_DEFAULT_UNIT").data(), "10K"))
				strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K").data());
			// strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_1M"));
		}
		else if (UIOPTION.is10K())
			strcat_s(comma, 128, LANGMANAGER.GetText("STR_DISPLAY_UNIT_10K").data());

		Cell(comma);
		NextCell();

		// Dodge
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0) {
			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();
		}
		else {
			sprintf_s(label, 128, "%u", (*itr)->GetDodgeUsed());
			Cell(label);
			NextCell();
		}

		// Death Counter
		sprintf_s(label, 128, "%u", (*itr)->GetDeathCount());
		Cell(label);
		NextCell();

		static double savedResultFullAB = 0;
		// Full AB Time
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) == LANGMANAGER.GetText("STR_TABLE_YOU").data()) {
			if (DAMAGEMETER.isHistoryMode()) {
				savedResultFullAB = (*itr)->GetHistoryABTime();
			}
			else {
				playerMetaData->CalcFullABTime(DAMAGEMETER.GetTime());
				savedResultFullAB = playerMetaData->_fullABTime;
			}
			sprintf_s(label, 128, "%.1f", savedResultFullAB);
		}
		else {
			sprintf_s(label, 128, "-");
		}
		Cell(label);
		NextCell();

		// Full AB Percent
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) == LANGMANAGER.GetText("STR_TABLE_YOU").data()) {
			sprintf_s(label, 128, "%.0f", ((double)(savedResultFullAB * 1000) / DAMAGEMETER.GetTime()) * 100);
		}
		else {
			sprintf_s(label, 128, "-");
		}
		Cell(label);
		NextCell();

		// Enli/Skill(%)
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0 || (*itr)->GetSkillUsed() <= 0) {
			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();

			sprintf_s(label, 128, "-");
			Cell(label);
			NextCell();
		}
		else {
			sprintf_s(label, 128, "%.1f", ((double)(*itr)->GetGigaEnlighten() / (*itr)->GetSkillUsed()) * 100);
			Cell(label);
			NextCell();

			sprintf_s(label, 128, "%.1f", ((double)(*itr)->GetTeraEnlighten() / (*itr)->GetSkillUsed()) * 100);
			Cell(label);
			NextCell();
		}

		// Aggro Percent
		static double savedResultAggroTime = 0;
		if (DAMAGEMETER.isHistoryMode()) {
			savedResultAggroTime = (*itr)->GetHistoryAggroTime();
		}
		else {
			playerMetaData->CalcAggroTime(DAMAGEMETER.GetTime());
			savedResultAggroTime = playerMetaData->_AggroTime;
		}
		sprintf_s(label, 128, "%.0f", ((double)(savedResultAggroTime * 1000) / DAMAGEMETER.GetTime()) * 100);
		Cell(label);
		NextCell();

		static double savedResultFullAS = 0;
		// Full AS Time
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) == LANGMANAGER.GetText("STR_TABLE_YOU").data()) {
			if (DAMAGEMETER.isHistoryMode()) {
				savedResultFullAS = (*itr)->GetHistoryASTime();
			}
			else {
				playerMetaData->CalcFullASTime(DAMAGEMETER.GetTime());
				savedResultFullAS = playerMetaData->_fullASTime;
			}
			sprintf_s(label, 128, "%.1f", savedResultFullAS);
		}
		else {
			sprintf_s(label, 128, "-");
		}
		Cell(label);
		NextCell();

		// Full AS Percent
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) == LANGMANAGER.GetText("STR_TABLE_YOU").data()) {
			sprintf_s(label, 128, "%.0f", ((double)(savedResultFullAS * 1000) / DAMAGEMETER.GetTime()) * 100);
		}
		else {
			sprintf_s(label, 128, "-");
		}
		Cell(label);
		NextCell();

		// AS
		static double savedResultAS = 0;
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0) {
			sprintf_s(label, 128, "-");
		}
		else if (DAMAGEMETER.isHistoryMode()) {
			savedResultAS = (*itr)->GetHistoryAvgAS();
			sprintf_s(label, 128, "%.1f", savedResultAS);
		}
		else {

			if ((int64_t)(milliTableTime - playerMetaData->_avgASPreviousTime) < 0) {
				sprintf_s(label, 128, "%.1f", savedResultAS);
			}
			else {
				uint64_t timeDifference = (milliTableTime - playerMetaData->_avgASPreviousTime);
				double currentAS = playerMetaData->GetStat(StatType::AttackSpeed);
				uint64_t calculatedAvgAS = static_cast<uint64_t>((playerMetaData->_avgASSum + timeDifference * currentAS));

				savedResultAS = (double)calculatedAvgAS / milliTableTime;
				sprintf_s(label, 128, "%.1f", savedResultAS);
			}
		}
		Cell(label);
		NextCell();

		//  (etc)
		PLOTWINDOW.AddJqData((*itr)->GetJqStack(), _tableTime);
	}
}

void PlayerTable::Cell(const char* text) {

	if (_collecting)
		_vertical.back().cells.emplace_back(text);
	else
		ImGui::TextUnformatted(text);
}

void PlayerTable::NextCell() {

	// Past the last column TableNextColumn opens a new row, which would leave
	// an empty padding-high row under every player.
	if (!_collecting && ImGui::TableGetColumnIndex() < ImGui::TableGetColumnCount() - 1)
		ImGui::TableNextColumn();
}

bool PlayerTable::VerticalRowShown(int column) {

	const std::string& rows = UIOPTION.VerticalRows();
	return column < (int)rows.size() ? rows[column] == '1' : kColumns[column].shown;
}

void PlayerTable::ToggleVerticalRow(int column) {

	std::string& rows = UIOPTION.VerticalRows();
	while (rows.size() < IM_ARRAYSIZE(kColumns))
		rows += kColumns[rows.size()].shown ? '1' : '0';

	rows[column] = rows[column] == '1' ? '0' : '1';
	UIOPTION.SaveOption();
}

// Players across, stats down. Each player's header cell carries their job color.
void PlayerTable::SetupVerticalTable() {

	_vertical.clear();
	_collecting = true;
	UpdateTable(0.0f);
	_collecting = false;

	ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | THEME.TableFlags();
	if (!ImGui::BeginTable("###Player Table Vertical", 1 + (int)_vertical.size(), tableFlags))
		return;

	ImGui::TableSetupColumn("##Stat", ImGuiTableColumnFlags_WidthFixed);
	for (size_t i = 0; i < _vertical.size(); i++)
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

	ImGui::SetWindowFontScale(_columnFontScale);
	ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
	ImGui::TableNextColumn();

	const float barOpacity = THEME.Current().barOpacity;
	for (size_t i = 0; i < _vertical.size(); i++) {
		const VerticalPlayer& player = _vertical[i];
		ImGui::TableNextColumn();

		ImVec4 bg = ImGui::ColorConvertU32ToFloat4(player.jobColor);
		bg.w *= barOpacity;
		if (bg.w > 0.0f)
			ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32(bg));

		ImGui::PushID((int)i);
		ImGui::PushStyleColor(ImGuiCol_Text, player.nameColor);
		Texture playerTexture = DIRECTX11.getcharacterTexture(player.job);
		if (UIOPTION.isUseImage() && playerTexture.ptr) {
			// Same hand centering as the horizontal name cell.
			float nameWidth = ImGui::CalcTextSize(player.name).x + playerTexture.xSize + ImGui::GetStyle().ItemSpacing.x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (ImGui::GetColumnWidth() - nameWidth) * 0.5f));
			ImGui::Image((void*)playerTexture.ptr, ImVec2((float)playerTexture.xSize, (float)playerTexture.ySize));
			ImGui::SameLine();
			ImGui::TextAlignCenter::UnSetTextAlignCenter();
			if (ImGui::Selectable(player.name))
				ToggleSelectInfo(player.id);
			ImGui::TextAlignCenter::SetTextAlignCenter();
		}
		else if (ImGui::Selectable(player.name))
			ToggleSelectInfo(player.id);
		ImGui::PopStyleColor();
		ImGui::PopID();
	}

	ImGui::SetWindowFontScale(_tableFontScale);
	for (int row = 1; row < IM_ARRAYSIZE(kColumns); row++) {
		if (!VerticalRowShown(row))
			continue;

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextAlignCenter::UnSetTextAlignCenter();
		ImGui::TextUnformatted(T(kColumns[row].key));
		ImGui::TextAlignCenter::SetTextAlignCenter();

		for (const VerticalPlayer& player : _vertical) {
			ImGui::TableNextColumn();
			// A player without stat data stops early; the rest stay blank.
			if (row - 1 < (int)player.cells.size())
				ImGui::TextUnformatted(player.cells[row - 1].c_str());
		}
	}
	ImGui::SetWindowFontScale(_globalFontScale);

	ImGui::EndTable();
}

void PlayerTable::DrawBar(float window_Width, float percent, ImU32 color) {

	THEME.DrawBar(window_Width, percent, color);
}

bool PlayerTable::ToggleSelectInfo(uint32_t id) {

	for (auto itr = _selectInfo.begin(); itr != _selectInfo.end(); itr++) {
		if ((*itr)->_playerID == id) {
			(*itr)->_isSelected = !(*itr)->_isSelected;

			return (*itr)->_isSelected;
		}
	}

	bool isMe = id != 0 && id == DAMAGEMETER.GetMyID(TRUE);

	SELECTED_PLAYER* selectinfo = new SELECTED_PLAYER(id, TRUE, isMe, _nextWindowIndex++, new SpecificInformation(id));
	_selectInfo.push_back(selectinfo);

	return selectinfo->_isSelected;
}

void PlayerTable::FollowMyID() {

	uint32_t myID = DAMAGEMETER.GetMyID(TRUE);

	if (myID == 0)
		return;

	for (auto itr = _selectInfo.begin(); itr != _selectInfo.end(); itr++) {

		if (!(*itr)->_isMe || (*itr)->_playerID == myID)
			continue;

		// Restarting a run hands the local player a brand new id, which would
		// leave this window pointing at a player that no longer exists.
		(*itr)->_playerID = myID;
		(*itr)->_specificInfo->SetPlayerID(myID);
	}
}

void PlayerTable::ShowSelectedTable() {

	for (auto itr = _selectInfo.begin(); itr != _selectInfo.end(); itr++) {
		if ((*itr)->_isSelected == TRUE) {
			(*itr)->_specificInfo->Update(&(*itr)->_isSelected, (*itr)->_windowIndex);
		}
	}
}