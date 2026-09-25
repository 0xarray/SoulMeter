#pragma once
#include "pch.h"
#include <unordered_map>
#include ".\UI\Theme.h"

#define UIOPTION UiOption::getInstance()

#define OPTION_FILE_NAME "option.xml"

#ifdef _DEBUG
#define DEBUG_READ_XML 0
#define DEBUG_COLUMN_WIDTH 0
#endif

class UiOption : public Singleton<UiOption> {
private:
	void ShowFeatures();
	void ShowDiscord();

	BOOL _is1K;
	BOOL _is1M;
	BOOL _is10K;
	BOOL _isSoloMode;
	BOOL _hideName;
	float _framerate;
	float _windowWidth;
	float _refreshTime;
	BOOL _isTopMost;
	BOOL _teamTA_LF;
	int32_t _teamTA_LF_Mode = 1;
	char _selectedLang[128] = { 0 };
	BOOL _isSoloRankMode;
	BOOL _isUseSaveData;
	
	BOOL _oriIsUseSaveData;
	BOOL _isDontSaveUnfinishedMaze;

	BOOL _unlockFps;
	int32_t _fpsCap;
	BOOL _unlockFov;
	BOOL _highDpi;

	void ShowGameTweaks();

	bool ShowTableOption();
	bool ShowHotkeySetting();
	void ShowLangSelector();
	void ChangeLang();

	void ShowTeamTALFSelector();
	void ReadLegacyColors(tinyxml2::XMLNode* node);


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
	const float& GetColumnFontScale();
	const float& GetTableFontScale();
	const ImVec4& GetActiveColor();
	const ImVec4& GetInActiveColor();
	bool is1K();
	bool is1M();
	bool is10K();
	bool isSoloMode();
	bool doHideName();
	bool isTopMost();
	bool isUseImage();
	bool isTeamTALF();
	const int32_t& TeamTALFMode();
	bool isSoloRankMode();
	bool isUseSaveData();
	bool isDontSaveUnfinishedMaze();

	// Read straight from option.xml: DPI awareness has to be set before the
	// first window is created, long before the rest of the options load.
	static bool WantsHighDpi();

	bool ToggleTopMost();

	// Players as columns and stats as rows instead of the other way round.
	bool isVertical() { return _isVertical != FALSE; }
	// One '0'/'1' per meter column saying whether vertical mode shows it as a
	// row; shorter than the column list for columns it has never been told about.
	std::string& VerticalRows() { return _verticalRows; }

	const float& GetFramerate();
	void SetFramerate(float i);

	const float& GetWindowWidth();
	void SetWindowWidth(const float& width);

	const float& GetRefreshTime();

	// Meter title template with $map, $time, $version, $ping; empty
	// means the built-in title.
	const char* GetTitleFormat() { return _titleFormat; }
	char _titleFormat[256] = { 0 };

	bool SaveOption(bool skipWarning = FALSE);
	BOOL _isUseImage;
	BOOL _isVertical = FALSE;
	std::string _verticalRows;

	// Pushes both tweaks to the hook. Safe to call when no game is up.
	void ApplyGameTweaks();
};