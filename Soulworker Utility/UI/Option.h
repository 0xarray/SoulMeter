#pragma once
#include "pch.h"
#include <unordered_map>

#define UIOPTION UiOption::getInstance()

#define OPTION_FILE_NAME "option.xml"
#define NETWORK_NAME_LEN 256

#ifdef _DEBUG
#define DEBUG_READ_XML 0
#define DEBUG_COLUMN_WIDTH 0
#endif

class UiOption : public Singleton<UiOption> {
private:
	ImVec4 _jobColor[11];
	ImVec4 _jobBasicColor[11];
	ImVec4 _activeColor[2];
	ImVec4 _outlineColor;
	ImVec4 _textColor;
	ImVec4 _windowBg;

	float _fontScale;
	bool ShowFontSelector();
	void ShowFeatures();
	void ShowDiscord();

	float _columnFontScale;
	float _tableFontScale;
	BOOL _is1K;
	BOOL _is1M;
	BOOL _is10K;
	BOOL _isSoloMode;
	BOOL _hideName;
	ImVec2 _cellPadding;
	float _framerate;
	float _windowBorderSize;
	float _windowWidth;
	float _refreshTime;
	BOOL _isTopMost;
	BOOL _teamTA_LF;
	int32_t _teamTA_LF_Mode = 1;
	char _selectedLang[128] = { 0 };
	BOOL _isSoloRankMode;
	BOOL _isUseSaveData;
	
	BOOL _oriIsUseSaveData;
	char _selectedFontFile[MAX_PATH] = { 0 };
	BOOL _isDontSaveUnfinishedMaze;

	bool ShowTableOption();
	bool ShowHotkeySetting();
	void ShowLangSelector();
	void ChangeLang();

	void ShowTeamTALFSelector();


	void Helper();

	bool GetOption();
	bool SetBasicOption();

	bool _open;

	bool _inited = false;

	std::unordered_map<std::string, std::string> _allLangList = LANGMANAGER.GetAllLangFile();


public:
	UiOption();
	~UiOption();

	const bool& isOption();
	void OpenOption();
	void Update();
	void Init();

	const ImU32 GetJobColor(unsigned int index);
	const ImU32 GetOutlineColor();
	const float& GetFontScale();
	const float& GetColumnFontScale();
	const float& GetTableFontScale();
	const ImVec4& GetActiveColor();
	const ImVec4& GetInActiveColor();
	const ImVec4& GetWindowBGColor();
	const bool& is1K();
	const bool& is1M();
	const bool& is10K();
	const bool& isSoloMode();
	const bool& doHideName();
	const bool& isTopMost();
	const bool& isUseImage();
	const bool& isTeamTALF();
	const int32_t& TeamTALFMode();
	const bool& isSoloRankMode();
	const bool& isUseSaveData();
	const char* GetFontFile();
	const bool& isDontSaveUnfinishedMaze();

	bool ToggleTopMost();

	const float& GetFramerate();
	void SetFramerate(float i);

	const float& GetWindowWidth();
	void SetWindowWidth(const float& width);

	const float& GetRefreshTime();

	bool SaveOption(bool skipWarning = FALSE);
	BOOL _isUseImage;
};