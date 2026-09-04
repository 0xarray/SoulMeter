#pragma once
#include ".\UI\SpecificInfomation.h"

typedef struct _SELECTED_PLAYER {
	
	uint32_t _playerID;
	bool _isSelected;
	// TRUE when this window follows the local player instead of a fixed id.
	bool _isMe;
	// Stays the same for the life of the window so the imgui window keeps its
	// position when other detail windows are removed.
	int64_t _windowIndex;
	SpecificInformation* _specificInfo;

	_SELECTED_PLAYER(uint32_t playerID, bool isSelected, bool isMe, int64_t windowIndex, SpecificInformation* specificInfo) : _playerID(playerID), _isSelected(isSelected), _isMe(isMe), _windowIndex(windowIndex), _specificInfo(specificInfo) { }
}SELECTED_PLAYER;

#define PLAYERTABLE PlayerTable::getInstance()

class PlayerTable : public Singleton<PlayerTable> {
private:
	std::vector<SELECTED_PLAYER*> _selectInfo;

	bool ToggleSelectInfo(uint32_t id);
	void ShowSelectedTable();
	void FollowMyID();
	void ClearSelectInfo(bool all);
	void BeginPopupMenu();

	void SetWindowSize();
	void SetMainWindowSize();
	void StoreWindowWidth();
	void SetupFontScale();

	void DrawBar(float window_Width, float percent, ImU32 color);
	void SetupTable();
	void UpdateTable(float windowWidth);

	float _globalFontScale;
	float _columnFontScale;
	float _tableFontScale;

	float _curWindowSize;

	bool _tableResize;

	float _tableTime;
	float _accumulatedTime;

	int64_t _nextWindowIndex;

public:
	PlayerTable();
	~PlayerTable();

	void Update();
	void ClearTable();
	void ResizeTalbe();

	LONG64 _lastSendTimestamp = 0;
	LONG64 _ping = 0;
	uint32_t _tick = 0;
	bool _isNewestVersion = TRUE;

	float GetTableTime()
	{
		return _tableTime;
	}

};