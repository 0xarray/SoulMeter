#include "pch.h"
#include "PlotWindow.h"
#include "UtillWindow.h"
#include <vector>

#include ".\Damage Meter\MySQLite.h"
#include ".\Damage Meter\Damage Meter.h"

void PlotWindow::AddData(uint32_t id, std::string name, double DPS, double time, bool isFirstElement)
{
	if (_historyMode)
		return;
	if (_pi == nullptr)
		_pi = new PlotInfo();

	_pi->AddData(id, name, DPS, time, isFirstElement);
}

void PlotInfo::AddData(uint32_t id, std::string name, double DPS, double time, bool isFirstElement)
{
	if (_isHistoryMode)
		return;

	if (isFirstElement) {
		if (_lastTime == time) {
			_allowed = false;
			return;
		}
		else {
			_allowed = true;
		}
	}
	// 
	else {
		if (!_allowed) {
			return;
		}
	}
	_lastTime = time;

	auto it = _metaInfos.begin();
	bool metaInfoFound = false;
	for (; it != _metaInfos.end(); it++) {
		if ((*it)->_id == id) {
			metaInfoFound = true;
			break;
		}
	}

	if (!metaInfoFound) {
		_metaInfos.push_back(new metaInfo(id, name));

		std::vector<double> newDPSvector;
		newDPSvector.push_back(DPS);
		_dpsList.emplace(id, newDPSvector);

		std::vector<double> newTimevector;
		newTimevector.push_back(time);
		_timeList.emplace(id, newTimevector);
	}
	else {
		_dpsList[id].push_back(DPS);
		_timeList[id].push_back(time);
	}
}

void PlotWindow::AddAbData(double DPS, double time)
{
	if (_historyMode)
		return;
	if (_pi == nullptr)
		_pi = new PlotInfo();
	_pi->AddAbData(DPS, time);
}

void PlotInfo::AddAbData(double DPS, double time)
{
	if (_isHistoryMode)
		return;

	if (_abLastTime == time) {
		return;
	}
	_abLastTime = time;

	_abList.push_back(DPS);
	_abTimeList.push_back(time);
}

void PlotWindow::AddBdData(double DPS, double time)
{
	if (_historyMode)
		return;
	if (_pi == nullptr)
		_pi = new PlotInfo();
	_pi->AddBdData(DPS, time);
}

void PlotInfo::AddBdData(double DPS, double time)
{
	if (_isHistoryMode)
		return;

	if (_bdLastTime == time) {
		return;
	}
	_bdLastTime = time;

	_bdList.push_back(DPS);
	_bdTimeList.push_back(time);
}

void PlotWindow::AddJqData(BYTE stack, double time)
{
	if (_historyMode)
		return;
	if (_pi == nullptr)
		_pi = new PlotInfo();
	_pi->AddJqData(stack, time);
}

void PlotInfo::AddJqData(BYTE stack, double time)
{
	if (_isHistoryMode)
		return;

	if (_jqLastTime == time) {
		return;
	}
	_jqLastTime = time;

	_jqList.push_back(stack);
	_jqTimeList.push_back(time);
}

void PlotWindow::AddBossHpData(uint32_t id, uint64_t HP, double time)
{
	if (_historyMode)
		return;

	if (_pi == nullptr)
		_pi = new PlotInfo();
	_pi->AddBossHpData(id, HP, time);
}

void PlotInfo::AddBossHpData(uint32_t id, uint64_t HP, double time)
{
	if (_isHistoryMode)
		return;

	if (_bhLastTime == time) {
		return;
	}
	_bhLastTime = time;

	_bossHpList[id].push_back(static_cast<double>(HP / 1000000));
	_bossTimeList[id].push_back(time);
}

void PlotWindow::OpenWindow()
{
	_isOpen = true;
}

void PlotWindow::Update()
{
	if (_isOpen) {

		char label[128] = { 0 };
		sprintf_s(label, "%s###MeowGraph", LANGMANAGER.GetText("STR_MENU_MEOW").data());

		// The plots fill the available width, so an unsized window would
		// auto-fit to almost nothing the first time it opens.
		const float em = ImGui::GetFontSize();
		ImGui::SetNextWindowSize(ImVec2(em * 30.0f, em * 20.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin(label, &_isOpen, ImGuiWindowFlags_None);

		if (ImGui::BeginTabBar(u8"PlotWindowTab"))
		{
			if (_pi != nullptr) {
				UpdatePlotTab();
				UpdateAbPlotTab();
				UpdateBdPlotTab();
				UpdateJqPlotTab();
				UpdateBossHpPlotTab();
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
	}
}

void PlotWindow::UpdatePlotTab()
{
	if (ImGui::BeginTabItem(LANGMANAGER.GetText("STR_PLOTWINDOW_DPSGRAPH").data()))
	{
		auto timeList = _pi->GetTimeList();
		auto dpsList = _pi->GetDPSList();
		auto metaInfos = _pi->GetMetaInfo();
		if (timeList.size() > 0) {
			uint32_t firstId = metaInfos.front()->_id;
			uint32_t lastId = metaInfos.back()->_id;
			size_t currentSize = timeList[firstId].size();
			if (currentSize > 0)
			{
				// 
				double startX = 0.0;
				if (currentSize > 45) {
					startX = timeList[firstId].at(currentSize - 45);
				}
				double endX = timeList[firstId].at(currentSize - 1);
				//
				double startY = 0;
				double endY = 10000;
				auto itr = dpsList[firstId].begin();
				if (currentSize > 45) {
					itr += (dpsList[firstId].size() - 1) - (45 - 1);
				}
				for (; itr != dpsList[firstId].end(); itr++) {
					if (*itr > endY) {
						endY = *itr;
					}
				}
				startY = endY - 7000;
				if (startY <= 0) {
					startY = 0;
				}
				endY += 1000;

				if (!_end) {
					ImPlot::SetNextAxisLimits(ImAxis_X1, startX, endX, ImGuiCond_Always);
					ImPlot::SetNextAxisLimits(ImAxis_Y1, startY, endY, ImGuiCond_Always);
				}
			}
		}

		if (ImPlot::BeginPlot(LANGMANAGER.GetText("STR_PLOTWINDOW_DPSGRAPH").data(), ImVec2(-1, 0))) {
			ImPlot::SetupAxes(
			LANGMANAGER.GetText("STR_PLOTWINDOW_TIME_SEC").data(),
			LANGMANAGER.GetText("STR_PLOTWINDOW_DPSGRAPH").data(), ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
			auto it = metaInfos.begin();
			for (; it != metaInfos.end(); it++) {
				uint32_t id = (*it)->_id;
				std::string name = (*it)->_name;

				ImPlot::PlotLine(name.c_str(), timeList[id].data(), dpsList[id].data(), static_cast<int>(dpsList[id].size()));
			}

			ImPlot::EndPlot();
		}
		ImGui::EndTabItem();
	}
}

void PlotWindow::UpdateAbPlotTab()
{
	if (ImGui::BeginTabItem(LANGMANAGER.GetText("STR_PLOTWINDOW_ABGRAPH").data()))
	{
		auto _abTimeList = _pi->GetABTimeList();
		auto _abList = _pi->GetABList();
		size_t currentSize = _abTimeList.size();
		if (currentSize > 0)
		{
			double startX = 0.0;
			double endX = 5.0;
			if (currentSize > 45) {
				startX = _abTimeList.at(currentSize - 45);
			}
			if (currentSize > 0) {
				endX = _abTimeList.back();
			}

			if (!_end) {
				ImPlot::SetNextAxisLimits(ImAxis_X1, startX, endX, ImGuiCond_Always);
			}
			ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0, 100.0, ImGuiCond_Always);
		}
		
		if (ImPlot::BeginPlot(LANGMANAGER.GetText("STR_PLOTWINDOW_ABGRAPH").data(), ImVec2(-1, 0))) {
			ImPlot::SetupAxes(
			LANGMANAGER.GetText("STR_PLOTWINDOW_TIME_SEC").data(),
			LANGMANAGER.GetText("STR_PLOTWINDOW_ABGRAPH").data(), ImPlotAxisFlags_None, ImPlotAxisFlags_None);
			ImPlot::PlotLine(LANGMANAGER.GetText("STR_TABLE_YOU").data(), _abTimeList.data(), _abList.data(), static_cast<int>(_abList.size()));
			ImPlot::EndPlot();
		}
		ImGui::EndTabItem();
	}
}

void PlotWindow::UpdateBdPlotTab()
{
	if (ImGui::BeginTabItem(LANGMANAGER.GetText("STR_PLOTWINDOW_BDGRAPH").data()))
	{
		auto _bdTimeList = _pi->GetBDTimeList();
		auto _bdList = _pi->GetBDList();
		size_t currentSize = _bdTimeList.size();
		if (currentSize > 0) {
			// 
			double startX = 0.0;
			if (currentSize > 45) {
				startX = _bdTimeList.at(currentSize - 45);
			}
			double endX = _bdTimeList.at(currentSize - 1);
			//
			double startY = 0;
			double endY = 200;
			auto itr = _bdList.begin();
			if (currentSize > 45) {
				itr += (_bdList.size() - 1) - (45 - 1);
			}
			for (; itr != _bdList.end(); itr++) {
				if (*itr > endY) {
					endY = *itr;
				}
			}
			startY = endY - 1000;
			if (startY <= 0) {
				startY = 0;
			}
			endY += 10;

			if (!_end) {
				ImPlot::SetNextAxisLimits(ImAxis_X1, startX, endX, ImGuiCond_Always);
				ImPlot::SetNextAxisLimits(ImAxis_Y1, startY, endY, ImGuiCond_Always);
			}
		}
		if (ImPlot::BeginPlot(LANGMANAGER.GetText("STR_PLOTWINDOW_BDGRAPH").data(), ImVec2(-1, 0))) {
			ImPlot::SetupAxes(
			LANGMANAGER.GetText("STR_PLOTWINDOW_TIME_SEC").data(),
			LANGMANAGER.GetText("STR_PLOTWINDOW_BDGRAPH").data(), ImPlotAxisFlags_None, ImPlotAxisFlags_None);
			ImPlot::PlotLine(LANGMANAGER.GetText("STR_TABLE_YOU").data(), _bdTimeList.data(), _bdList.data(), static_cast<int>(_bdList.size()));
			ImPlot::EndPlot();
		}
		ImGui::EndTabItem();
	}
}

void PlotWindow::UpdateJqPlotTab()
{
	if (ImGui::BeginTabItem(LANGMANAGER.GetText("STR_PLOTWINDOW_JQGRAPH").data()))
	{
		auto _jqTimeList = _pi->GetJQTimeList();
		auto _jqList = _pi->GetJQList();
		size_t currentSize = _jqTimeList.size();
		if (currentSize > 0)
		{
			double startX = 0.0;
			double endX = 5.0;
			if (currentSize > 45) {
				startX = _jqTimeList.at(currentSize - 45);
			}
			if (currentSize > 0) {
				endX = _jqTimeList.back();
			}
			if (!_end) {
				ImPlot::SetNextAxisLimits(ImAxis_X1, startX, endX, ImGuiCond_Always);
			}
			ImPlot::SetNextAxisLimits(ImAxis_Y1, 0, 4, ImGuiCond_Always);
		}

		if (ImPlot::BeginPlot(LANGMANAGER.GetText("STR_PLOTWINDOW_JQGRAPH").data(), ImVec2(-1, 0))) {
			ImPlot::SetupAxes(
			LANGMANAGER.GetText("STR_PLOTWINDOW_TIME_SEC").data(),
			LANGMANAGER.GetText("STR_PLOTWINDOW_JQGRAPH").data(), ImPlotAxisFlags_None, ImPlotAxisFlags_None);
			ImPlot::PlotLine(LANGMANAGER.GetText("STR_TABLE_YOU").data(), _jqTimeList.data(), _jqList.data(), static_cast<int>(_jqList.size()));
			ImPlot::EndPlot();
		}
		ImGui::EndTabItem();
	}
}

void PlotWindow::UpdateBossHpPlotTab()
{
	if (ImGui::BeginTabItem(LANGMANAGER.GetText("STR_PLOTWINDOW_BOSSHPGRAPH").data()))
	{
		UpdateBossHpPlotCombo();

		if (_selectedBossHpComboID != -1)
			UpdateBossHpPlotGraph();

		ImGui::EndTabItem();
	}
}

void PlotWindow::UpdateBossHpPlotCombo()
{
	std::unordered_map<uint32_t, const char*> bossInfos;

	// Get all monster data
	for (auto itr = DAMAGEMETER.begin(); itr < DAMAGEMETER.end(); itr++) {
		for (auto itr2 = (*itr)->begin(); itr2 != (*itr)->end(); itr2++) {
			if ((*itr2)->GetType() == 3 || (*itr2)->GetType() == 4)
				bossInfos.emplace((*itr2)->GetID(), (*itr2)->GetName());
		}
	}

	const char* comboPreview = nullptr;

	if (bossInfos.begin() != bossInfos.end()) {
		if (_selectedBossHpComboID == -1 || bossInfos.find(_selectedBossHpComboID) == bossInfos.end())
			_selectedBossHpComboID = bossInfos.begin()->first;
		comboPreview = bossInfos.at(_selectedBossHpComboID);

		if (ImGui::BeginCombo("BOSS", comboPreview, ImGuiComboFlags_HeightLarge)) {

			for (auto itr = bossInfos.begin(); itr != bossInfos.end(); itr++) 
			{
				char label[MONSTER_NAME_LEN] = { 0 };
				sprintf_s(label, MONSTER_NAME_LEN, "%s##%d", itr->second, itr->first);

				if (ImGui::Selectable(label, _selectedBossHpComboID == itr->first)) {
					_selectedBossHpComboID = itr->first;
				}
			}

			ImGui::EndCombo();
		}
	}
}

void PlotWindow::UpdateBossHpPlotGraph()
{
	auto timeList = _pi->GetBossTimeList();
	auto bossHpList = _pi->GetBossHpList();
	if (timeList.size() > 0) {
		size_t currentSize = timeList[_selectedBossHpComboID].size();
		if (currentSize > 0)
		{
			// 
			double startX = 0.0;
			if (currentSize > 45) {
				startX = timeList[_selectedBossHpComboID].at(currentSize - 45);
			}
			double endX = timeList[_selectedBossHpComboID].at(currentSize - 1);
			//
			double startY = 0;
			double endY = 100;
			auto itr = bossHpList[_selectedBossHpComboID].begin();
			if (currentSize > 45) {
				itr += (bossHpList[_selectedBossHpComboID].size() - 1) - (45 - 1);
			}
			for (; itr != bossHpList[_selectedBossHpComboID].end(); itr++) {
				if (*itr > endY) {
					endY = *itr;
				}
			}
			startY = endY - 7000;
			if (startY <= 0) {
				startY = 0;
			}
			endY += 100;

			if (!_end) {
				ImPlot::SetNextAxisLimits(ImAxis_X1, startX, endX, ImGuiCond_Always);
				ImPlot::SetNextAxisLimits(ImAxis_Y1, startY, endY, ImGuiCond_Always);
			}
		}
	}

	if (ImPlot::BeginPlot(LANGMANAGER.GetText("STR_PLOTWINDOW_BOSSHPGRAPH").data(), ImVec2(-1, 0))) {
		ImPlot::SetupAxes(
		LANGMANAGER.GetText("STR_PLOTWINDOW_TIME_SEC").data(),
		LANGMANAGER.GetText("STR_PLOTWINDOW_BOSSHPGRAPH").data(), ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
		ImPlot::PlotLine(LANGMANAGER.GetText("STR_PLOTWINDOW_BOSSHPGRAPH_UNIT").data(), timeList[_selectedBossHpComboID].data(), bossHpList[_selectedBossHpComboID].data(), static_cast<int>(bossHpList[_selectedBossHpComboID].size()));
		ImPlot::EndPlot();
	}
}

void PlotWindow::Start()
{
	_end = false;
}

void PlotWindow::End()
{
	_end = true;
}

void PlotWindow::Clear()
{
	_end = false;
	_pi = nullptr;
	_selectedBossHpComboID = -1;
	_historyMode = false;
}

void PlotWindow::SetPlotInfo(PlotInfo* p_pi)
{
	_pi = p_pi;
	_end = true;
	_historyMode = true;
}

PlotInfo* PlotWindow::GetPlotInfo()
{
	return _pi;
}

PlotWindow::~PlotWindow()
{
}