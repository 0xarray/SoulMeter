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

PlayerTable::PlayerTable() : _tableResize(0), _globalFontScale(0), _columnFontScale(0), _tableFontScale(0), _curWindowSize(0), _tableTime(0), _accumulatedTime(0)
{

}

PlayerTable::~PlayerTable() {
	ClearTable();
}

void PlayerTable::ClearTable() {

	for (auto itr = _selectInfo.begin(); itr != _selectInfo.end(); itr++) {
		delete (*itr)->_specificInfo;
	}

	for (auto itr = _selectInfo.begin(); itr != _selectInfo.end(); itr++) {
		delete (*itr);
	}

	_selectInfo.clear();
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

		ImVec4 prevInActiveColor = style.Colors[10];
		ImVec4 prevActiveColor = style.Colors[11];

		if (DAMAGEMETER.isRun()) {
			style.Colors[10] = UIOPTION.GetActiveColor();
			style.Colors[11] = UIOPTION.GetActiveColor();
		}
		else {
			style.Colors[10] = UIOPTION.GetInActiveColor();
			style.Colors[11] = UIOPTION.GetInActiveColor();
		}

		_accumulatedTime += UIWINDOW.GetDeltaTime();

		if (_accumulatedTime > UIOPTION.GetRefreshTime()) {
			_tableTime = static_cast<float>(((double)DAMAGEMETER.GetTime()) / 1000);
			_accumulatedTime = 0;
		}

		SetupFontScale();

		ImGuiWindowFlags windowFlag = ImGuiWindowFlags_None;
		windowFlag |= (ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);

		if (!UIOPTION.isOption())
			windowFlag = windowFlag | ImGuiWindowFlags_NoResize;

		char title[1024] = { 0 };
		unsigned int miliseconds = ((unsigned int)DAMAGEMETER.GetTime() % 1000);
		if (DAMAGEMETER.mswideness == 1)
		{
			miliseconds = miliseconds / 100;
		}
		else if (DAMAGEMETER.mswideness == 2)
		{
			miliseconds = miliseconds / 10;
		}
		//and if 3 then do nothing basically so we display all to 999
		std::string milisecondsstring = std::to_string(miliseconds);
		//turning it into a string before so we dont display leading 0's so timer is more readable

		if (!PipeReceiverIsConnected()) {
			// game not hooked yet - the player launches it themselves
			sprintf_s(title, 1024, "%s [v%s]  %s ###DamageMeter",
				LANGMANAGER.GetText("STR_WAITING_FOR_GAME").data(),
				APP_VERSION,
				SOULMETER_DISCORD_INVITE
			);
		}
		else {
			sprintf_s(title, 1024, "%s - %02d:%02d.%s [v%s_@Rainy] %s: %ums ###DamageMeter",
				DAMAGEMETER.GetWorldName(),
				(unsigned int)DAMAGEMETER.GetTime() / (60 * 1000), (unsigned int)(DAMAGEMETER.GetTime() / 1000) % 60, milisecondsstring.c_str(),
				APP_VERSION,
				LANGMANAGER.GetText("STR_MENU_PING").data(),
				DAMAGEMETER.GetPing()
			);
		}

		ImGui::Begin(title, 0, windowFlag);
		{
			if (!UIOPTION.isOption() || _tableResize)
				SetWindowSize();

			if (UIOPTION.isOption())
				StoreWindowWidth();

			SetMainWindowSize();

			BeginPopupMenu();

			ImGui::OutlineText::PushOutlineText(ImGui::IMGUIOUTLINETEXT(UIOPTION.GetOutlineColor(), 1));
			ImGui::TextAlignCenter::SetTextAlignCenter();
			{
				SetupTable();
			}
			ImGui::TextAlignCenter::UnSetTextAlignCenter();
			ImGui::OutlineText::PopOutlineText();
		}
		ImGui::End();

		ShowSelectedTable();

		style.WindowPadding.x = prevWindowPadding.x;
		style.WindowPadding.y = prevWindowPadding.y;

		style.Colors[10] = prevInActiveColor;
		style.Colors[11] = prevActiveColor;
	}
	DAMAGEMETER.FreeLock();
}

void PlayerTable::SetWindowSize() {

	_tableResize = FALSE;

	ImGuiStyle& style = ImGui::GetStyle();

	if (ImGui::GetScrollMaxY() > 0)
		_curWindowSize += ImGui::GetScrollMaxY();

	ImGui::SetWindowSize(ImVec2(UIOPTION.GetWindowWidth(), FLOOR(_curWindowSize)));
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
	UIOPTION.SetWindowWidth(ImGui::GetWindowSize().x);
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

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_TOPMOST").data(), nullptr, UIOPTION.isTopMost())) {
			UIOPTION.ToggleTopMost();
		}

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

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_OPTIONS").data())) {
			UIOPTION.OpenOption();
		}

		if (ImGui::MenuItem(LANGMANAGER.GetText("STR_MENU_EXIT").data())) {
			PostMessage(UIWINDOW.GetHWND(), WM_CLOSE, 0, 0);
		}

		ImGui::EndPopup();
	}
}

void PlayerTable::SetupTable() {

	ImGuiTableFlags tableFlags = ImGuiTableFlags_None;
	tableFlags |= (ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Resizable);

	const int columnSize = 48;
	if (ImGui::BeginTable("###Player Table", columnSize, tableFlags)) {

		ImGuiTableColumnFlags columnFlags = ImGuiTableColumnFlags_None;
		columnFlags |= ImGuiTableColumnFlags_NoSort;

		ImGui::SetWindowFontScale(_columnFontScale);

		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_NAME").data(), ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoClip | ImGuiTableColumnFlags_WidthFixed | columnFlags, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_DPS").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_DAMAGE_PERCENT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TOTAL_DAMAGE").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TOTAL_HIT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_CRIT_RATE").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_HIT_PER_SECOND").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_CRIT_HIT_PER_SECOND").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_SKILL_PER_SECOND").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_MAX_COMBO").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_ATTACK_CDMG_SUM").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_SOUL_GAUGE").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_ATTACK_SPEED").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_ARMOR_BREAK").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_BOSS_DAMAGE").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_STAMINA").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_SOUL_VAPOR").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_SOULSTONE_PERCENT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_SOULSTONE_PROC").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_SOULSTONE_DAMAGE").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_AVERAGE_AB").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_AVERAGE_AB_U").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_AVERAGE_BD").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_MISS").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_MISS_RATE").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_PARTIAL").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_GET_HIT_INCLUDE_ZERO_DAMAGE").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_GET_HIT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_GET_HIT_BS").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_EVADE_RATE_A").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_EVADE_RATE_B").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_GIGA_ENLIGHTEN").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TERA_ENLIGHTEN").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TERA_FEVER").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TERA_FURY").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TERA_BACKSTEP").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TERA_TECHNIC").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_LOSED_HP").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_DODGE_COUNT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_DEATH").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_FULL_AB_TIME").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_FULL_AB_PERCENT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_GIGA_ENLIGHTEN_SKILL_PERCENT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_TERA_ENLIGHTEN_SKILL_PERCENT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_AGGRO_TIME_PERCENT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_FULL_AS_TIME").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_FULL_AS_PERCENT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		ImGui::TableSetupColumn(LANGMANAGER.GetText("STR_TABLE_AVG_AS_PERCENT").data(), columnFlags | ImGuiTableColumnFlags_WidthFixed, -1);
		//ImGuiTableColumnFlags_WidthStretch

		ImGui::TableHeadersRow();

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

		ImGui::TableNextRow();
		ImGui::TableNextColumn();


		DrawBar(windowWidth, damage_percent, UIOPTION.GetJobColor(DAMAGEMETER.GetPlayerJob((*itr)->GetID())));
		uint64_t milliTableTime = (uint64_t)((double)_tableTime * 1000);

		// NAME
		const char* playerName = DAMAGEMETER.GetPlayerName((*itr)->GetID());
		if (UIOPTION.doHideName() && playerName != LANGMANAGER.GetText("STR_TABLE_YOU").data()) {
			playerName = "";
		}

		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4 saved = ImVec4(style.Colors[0].x, style.Colors[0].y, style.Colors[0].z, style.Colors[0].w);
		
		uint32_t playerId = (*itr)->GetID();
		if (playerId == DAMAGEMETER.GetAggro()) {
			style.Colors[0] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
		}
		else if (playerId == DAMAGEMETER.GetOwnerID(DAMAGEMETER.GetAggro())) {
			style.Colors[0] = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
		}

		if (DAMAGEMETER.PlayerInAwakening(playerId)) {
			style.Colors[0] = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
		}
		bool useImage = UIOPTION.isUseImage();


		Texture playerTexture = DIRECTX11.getcharacterTexture(DAMAGEMETER.GetPlayerJob((*itr)->GetID()));

		if (useImage && playerTexture.ptr) {
			ImGui::SetCursorPosX((ImGui::GetColumnWidth() * 0.5f) - ((ImGui::CalcTextSize(playerName).x + playerTexture.xSize) / 2)); //because font size can change, we need to use texture size to center properly
		}
		else
		{
			ImGui::SetCursorPosX(ImGui::GetColumnWidth() * 0.5f - (ImGui::CalcTextSize(playerName).x / 2)); // we dont use texture here so center text only, have to do it myself because disabled text centering for this part as it was breaking rendering
		}
		if (useImage && playerTexture.ptr) {
			ImGui::Image((void*)playerTexture.ptr, ImVec2((float)playerTexture.xSize, (float)playerTexture.ySize));
			ImGui::SameLine();
		}
		ImGui::TextAlignCenter::UnSetTextAlignCenter(); //some gay custom function, breaks text align with image
		if (ImGui::Selectable(playerName, false, ImGuiSelectableFlags_SpanAllColumns))
			ToggleSelectInfo((*itr)->GetID());

		ImGui::TextAlignCenter::SetTextAlignCenter();

		ImGui::TableNextColumn();
		style.Colors[0] = saved;


		// DPS
		if (_tableTime < 1) {
			ImGui::Text("-");
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
			ImGui::Text(comma);

			bool isFirstElement = ((itr - DAMAGEMETER.begin()) == 0);
			PLOTWINDOW.AddData((*itr)->GetID(), DAMAGEMETER.GetPlayerName((*itr)->GetID()), dps, _tableTime, isFirstElement);
		}
		

		ImGui::TableNextColumn();

		// D%
		if (DAMAGEMETER.GetPlayerTotalDamage() == 0) {
			sprintf_s(label, 128, "%.0lf", (float)0);
			ImGui::Text(label);
		}
		else {
			sprintf_s(label, 128, "%.0lf", ((double)(*itr)->GetDamage() / (double)DAMAGEMETER.GetPlayerTotalDamage()) * 100);
			ImGui::Text(label);
		}

		ImGui::TableNextColumn();

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
		ImGui::Text(comma);

		ImGui::TableNextColumn();

		// HIT
		sprintf_s(label, 128, "%d", (*itr)->GetHitCount());
		TextCommma(label, comma);
		ImGui::Text(comma);

		ImGui::TableNextColumn();

		// CRIT
		float crit = 0;

		if ((*itr)->GetHitCount() != 0)
			crit = (float)(*itr)->GetCritHitCountForCritRate() / (float)(*itr)->GetHitCountForCritRate() * 100;

		sprintf_s(label, 128, "%.1f", crit);
		ImGui::Text(label);

		ImGui::TableNextColumn();

		// HIT/S
		if (_tableTime == (float)0) {
			sprintf_s(label, 128, "%d", 0);
			ImGui::Text(label);
		}
		else {
			sprintf_s(label, 128, "%.2lf", (double)(*itr)->GetHitCount() / _tableTime);
			ImGui::Text(label);

		}
		ImGui::TableNextColumn();

		//CRIT/S
		if (_tableTime == (float)0) {
			sprintf_s(label, 128, "%d", 0);
			ImGui::Text(label);
		}
		else {
			sprintf_s(label, 128, "%.2lf", (double)(*itr)->GetCritHitCount() / _tableTime);
			ImGui::Text(label);
		}

		ImGui::TableNextColumn();

		// Skill/s
		if (_tableTime == 0.0f) {
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
		}
		else {
			sprintf_s(label, 128, "%.2lf", (double)(*itr)->GetSkillUsed() / _tableTime);
			ImGui::Text(label);
		}

		ImGui::TableNextColumn();


		// MAXC
		sprintf_s(label, 128, "%d", (*itr)->GetMaxCombo());
		TextCommma(label, comma);
		ImGui::Text(comma);

		ImGui::TableNextColumn();

		uint32_t playerID = (*itr)->GetID();
		SWDamageMeter::SW_PLAYER_METADATA* playerMetaData = DAMAGEMETER.GetPlayerMetaData(playerID);

		// Not found stat data
		if (playerMetaData == NULL) {
			continue;
		}

		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime < 1) {
			// Attack+Crit SUM
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();

			// SG
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::SG));
			ImGui::Text(label);
			ImGui::TableNextColumn();
			// AttackSpeed
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::AttackSpeed));
			ImGui::Text(label);
			ImGui::TableNextColumn();

			// AB
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();
			// BD
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();
			// STAM
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();
			// SV
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();
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
			ImGui::Text(comma);
			ImGui::TableNextColumn();

			static float statTmp = 0;

			// SG
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::SG));
			ImGui::Text(label);

			ImGui::TableNextColumn();
			// AttackSpeed
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::AttackSpeed));
			ImGui::Text(label);

			ImGui::TableNextColumn();
			// AB
			statTmp = playerMetaData->GetStat(StatType::ArmorBreak);
			sprintf_s(label, 128, "%.1f", statTmp);
			PLOTWINDOW.AddAbData(statTmp, _tableTime);
			ImGui::Text(label);
			
			ImGui::TableNextColumn();
			// BD
			statTmp = playerMetaData->GetSpecialStat(SpecialStatType::BossDamageAddRate);
			sprintf_s(label, 128, "%.1f", statTmp);
			PLOTWINDOW.AddBdData(statTmp, _tableTime);
			ImGui::Text(label);

			ImGui::TableNextColumn();
			// stamina
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::Stamina));
			ImGui::Text(label);

			ImGui::TableNextColumn();
			// SV
			sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::SV));
			ImGui::Text(label);
			ImGui::TableNextColumn();
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
		ImGui::Text(label);
		ImGui::TableNextColumn();

		// Soulstone crit rate
		double soulstoneProcRate;
		if ((*itr)->GetCritHitCountForCritRate() == 0) {
			soulstoneProcRate = 0.0;
		}
		else {
			soulstoneProcRate = ((double)(*itr)->GetSoulstoneCount()) / (*itr)->GetHitCountForCritRate() * 100;
		}

		sprintf_s(label, 128, "%.1f", soulstoneProcRate);
		ImGui::Text(label);
		ImGui::TableNextColumn();

		// Soulstone damage %
		double soulstoneDamage;
		if ((*itr)->GetDamageForSoulstone() == 0) {
			soulstoneDamage = 0.0;
		}
		else {
			soulstoneDamage = ((double)(*itr)->GetSoulStoneDamageForSoulstone()) / (*itr)->GetDamageForSoulstone() * 100;
		}
		sprintf_s(label, 128, "%.1f", soulstoneDamage);
		ImGui::Text(label);
		ImGui::TableNextColumn();

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

		ImGui::Text(label);
		ImGui::TableNextColumn();

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

		ImGui::Text(label);
		ImGui::TableNextColumn();

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

		ImGui::Text(label);
		ImGui::TableNextColumn();
		// Miss
		sprintf_s(label, 128, "%d", (*itr)->GetMissCount());
		TextCommma(label, comma);
		ImGui::Text(comma);

		ImGui::TableNextColumn();
		// Miss%
		if ((*itr)->GetMissCount() == 0 || (*itr)->GetHitCountForCritRate() == 0) {
			sprintf_s(label, 128, "%.1f", 0.0);
		}
		else {
			sprintf_s(label, 128, "%.1f", (double)(*itr)->GetMissCount() / (*itr)->GetHitCountForCritRate() * 100);
		}

		ImGui::Text(label);
		ImGui::TableNextColumn();

		// MissDamageRate
		sprintf_s(label, 128, "%.1f", playerMetaData->GetStat(StatType::PartialDamage));
		ImGui::Text(label);
		ImGui::TableNextColumn();

		// GetHit(Include Zero Damage)
		sprintf_s(label, 128, "%d", (*itr)->GetGetHitAll());
		TextCommma(label, comma);
		ImGui::Text(comma);

		ImGui::TableNextColumn();

		// GetHit
		sprintf_s(label, 128, "%d", (*itr)->GetGetHit());
		TextCommma(label, comma);
		ImGui::Text(comma);

		ImGui::TableNextColumn();

		// GetHit(BS)
		sprintf_s(label, 128, "%d", (*itr)->GetGetHitBS());
		TextCommma(label, comma);
		ImGui::Text(comma);

		ImGui::TableNextColumn();


		// Evade A
		if ((*itr)->GetGetHitAll() == 0) {
			sprintf_s(label, 128, "-");
		}
		else {
			sprintf_s(label, 128, "%.1f%%", (double)(*itr)->GetGetHitMissed() / (*itr)->GetGetHitAll() * 100);
		}
		ImGui::Text(label);
		ImGui::TableNextColumn();

		// Evade B
		if ((*itr)->GetGetHit() == 0) {
			sprintf_s(label, 128, "-");
		}
		else {
			sprintf_s(label, 128, "%.1f%%", (double)(*itr)->GetGetHitMissedReal() / (*itr)->GetGetHit() * 100);
		}
		ImGui::Text(label);
		ImGui::TableNextColumn();


		// Enlighten
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0) {
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();

			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();
		}
		else {
			sprintf_s(label, 128, "%u", (*itr)->GetGigaEnlighten());
			ImGui::Text(label);
			ImGui::TableNextColumn();

			sprintf_s(label, 128, "%u", (*itr)->GetTeraEnlighten());
			ImGui::Text(label);
			ImGui::TableNextColumn();
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

				ImGui::Text(label);
				ImGui::TableNextColumn();
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

		ImGui::Text(comma);
		ImGui::TableNextColumn();

		// Dodge
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0) {
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();
		}
		else {
			sprintf_s(label, 128, "%u", (*itr)->GetDodgeUsed());
			ImGui::Text(label);
			ImGui::TableNextColumn();
		}

		// Death Counter
		sprintf_s(label, 128, "%u", (*itr)->GetDeathCount());
		ImGui::Text(label);
		ImGui::TableNextColumn();

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
		ImGui::Text(label);
		ImGui::TableNextColumn();

		// Full AB Percent
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) == LANGMANAGER.GetText("STR_TABLE_YOU").data()) {
			sprintf_s(label, 128, "%.0f", ((double)(savedResultFullAB * 1000) / DAMAGEMETER.GetTime()) * 100);
		}
		else {
			sprintf_s(label, 128, "-");
		}
		ImGui::Text(label);
		ImGui::TableNextColumn();

		// Enli/Skill(%)
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) != LANGMANAGER.GetText("STR_TABLE_YOU").data() || _tableTime == 0 || (*itr)->GetSkillUsed() <= 0) {
			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();

			sprintf_s(label, 128, "-");
			ImGui::Text(label);
			ImGui::TableNextColumn();
		}
		else {
			sprintf_s(label, 128, "%.1f", ((double)(*itr)->GetGigaEnlighten() / (*itr)->GetSkillUsed()) * 100);
			ImGui::Text(label);
			ImGui::TableNextColumn();

			sprintf_s(label, 128, "%.1f", ((double)(*itr)->GetTeraEnlighten() / (*itr)->GetSkillUsed()) * 100);
			ImGui::Text(label);
			ImGui::TableNextColumn();
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
		ImGui::Text(label);
		ImGui::TableNextColumn();

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
		ImGui::Text(label);
		ImGui::TableNextColumn();

		// Full AS Percent
		if (DAMAGEMETER.GetPlayerName((*itr)->GetID()) == LANGMANAGER.GetText("STR_TABLE_YOU").data()) {
			sprintf_s(label, 128, "%.0f", ((double)(savedResultFullAS * 1000) / DAMAGEMETER.GetTime()) * 100);
		}
		else {
			sprintf_s(label, 128, "-");
		}
		ImGui::Text(label);
		ImGui::TableNextColumn();

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
		ImGui::Text(label);
		ImGui::TableNextColumn();

		//  (etc)
		PLOTWINDOW.AddJqData((*itr)->GetJqStack(), _tableTime);
	}
}

void PlayerTable::DrawBar(float window_Width, float percent, ImU32 color) {

	auto draw_list = ImGui::GetWindowDrawList();

	float result_width = window_Width * percent;
	float height = ImGui::GetFontSize();
	ImVec2 line = ImGui::GetCursorScreenPos();

	line.x = FLOOR(line.x);	line.y = line.y;
	height = height;
	ImGui::TablePushBackgroundChannel(); //without this image drawing will break bar
	draw_list->AddRectFilled(ImVec2(line.x, line.y), ImVec2(line.x + result_width, line.y + height), color, 0, 0);
	ImGui::TablePopBackgroundChannel();
}

bool PlayerTable::ToggleSelectInfo(uint32_t id) {

	for (auto itr = _selectInfo.begin(); itr != _selectInfo.end(); itr++) {
		if ((*itr)->_playerID == id) {
			(*itr)->_isSelected = !(*itr)->_isSelected;

			return (*itr)->_isSelected;
		}
	}

	SELECTED_PLAYER* selectinfo = new SELECTED_PLAYER(id, TRUE, new SpecificInformation(id));
	_selectInfo.push_back(selectinfo);

	return selectinfo->_isSelected;
}

void PlayerTable::ShowSelectedTable() {

	for (auto itr = _selectInfo.begin(); itr != _selectInfo.end(); itr++) {
		if ((*itr)->_isSelected == TRUE) {
			(*itr)->_specificInfo->Update(&(*itr)->_isSelected, itr - _selectInfo.begin());
		}
	}
}