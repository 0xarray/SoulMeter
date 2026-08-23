#include "pch.h"
#include "HotKey.h"
#include ".\UI\DX Input.h"
#include ".\UI\PlayerTable.h"
#include ".\Damage Meter\Damage Meter.h"

namespace {
	struct KEYNAME {
		int key;
		const char* name;
	};

	const KEYNAME KEY_NAME_TABLE[] = {
		{ DIK_ESCAPE, "Esc" }, { DIK_1, "1" }, { DIK_2, "2" }, { DIK_3, "3" }, { DIK_4, "4" },
		{ DIK_5, "5" }, { DIK_6, "6" }, { DIK_7, "7" }, { DIK_8, "8" }, { DIK_9, "9" }, { DIK_0, "0" },
		{ DIK_MINUS, "-" }, { DIK_EQUALS, "=" }, { DIK_BACK, "Backspace" }, { DIK_TAB, "Tab" },
		{ DIK_Q, "Q" }, { DIK_W, "W" }, { DIK_E, "E" }, { DIK_R, "R" }, { DIK_T, "T" }, { DIK_Y, "Y" },
		{ DIK_U, "U" }, { DIK_I, "I" }, { DIK_O, "O" }, { DIK_P, "P" },
		{ DIK_LBRACKET, "[" }, { DIK_RBRACKET, "]" }, { DIK_RETURN, "Enter" }, { DIK_LCONTROL, "LCtrl" },
		{ DIK_A, "A" }, { DIK_S, "S" }, { DIK_D, "D" }, { DIK_F, "F" }, { DIK_G, "G" }, { DIK_H, "H" },
		{ DIK_J, "J" }, { DIK_K, "K" }, { DIK_L, "L" },
		{ DIK_SEMICOLON, ";" }, { DIK_APOSTROPHE, "'" }, { DIK_GRAVE, "`" }, { DIK_LSHIFT, "LShift" },
		{ DIK_BACKSLASH, "\\" },
		{ DIK_Z, "Z" }, { DIK_X, "X" }, { DIK_C, "C" }, { DIK_V, "V" }, { DIK_B, "B" }, { DIK_N, "N" }, { DIK_M, "M" },
		{ DIK_COMMA, "," }, { DIK_PERIOD, "." }, { DIK_SLASH, "/" }, { DIK_RSHIFT, "RShift" },
		{ DIK_MULTIPLY, "Num *" }, { DIK_LMENU, "LAlt" }, { DIK_SPACE, "Space" }, { DIK_CAPITAL, "CapsLock" },
		{ DIK_F1, "F1" }, { DIK_F2, "F2" }, { DIK_F3, "F3" }, { DIK_F4, "F4" }, { DIK_F5, "F5" },
		{ DIK_F6, "F6" }, { DIK_F7, "F7" }, { DIK_F8, "F8" }, { DIK_F9, "F9" }, { DIK_F10, "F10" },
		{ DIK_NUMLOCK, "NumLock" }, { DIK_SCROLL, "ScrollLock" },
		{ DIK_NUMPAD7, "Num 7" }, { DIK_NUMPAD8, "Num 8" }, { DIK_NUMPAD9, "Num 9" }, { DIK_SUBTRACT, "Num -" },
		{ DIK_NUMPAD4, "Num 4" }, { DIK_NUMPAD5, "Num 5" }, { DIK_NUMPAD6, "Num 6" }, { DIK_ADD, "Num +" },
		{ DIK_NUMPAD1, "Num 1" }, { DIK_NUMPAD2, "Num 2" }, { DIK_NUMPAD3, "Num 3" }, { DIK_NUMPAD0, "Num 0" },
		{ DIK_DECIMAL, "Num ." },
		{ DIK_F11, "F11" }, { DIK_F12, "F12" },
		{ DIK_NUMPADENTER, "Num Enter" }, { DIK_RCONTROL, "RCtrl" }, { DIK_DIVIDE, "Num /" },
		{ DIK_SYSRQ, "PrintScreen" }, { DIK_RMENU, "RAlt" }, { DIK_PAUSE, "Pause" },
		{ DIK_HOME, "Home" }, { DIK_UP, "Up" }, { DIK_PRIOR, "PageUp" }, { DIK_LEFT, "Left" },
		{ DIK_RIGHT, "Right" }, { DIK_END, "End" }, { DIK_DOWN, "Down" }, { DIK_NEXT, "PageDown" },
		{ DIK_INSERT, "Insert" }, { DIK_DELETE, "Delete" },
		{ DIK_LWIN, "LWin" }, { DIK_RWIN, "RWin" }, { DIK_APPS, "Menu" },
	};
}

AutoHotKey::~AutoHotKey() {
	_callbacks.clear();
}

void AutoHotKey::SetKey(const int key1, const int key2, const int key3) {

	_key[0] = key1; _key[1] = key2; _key[2] = key3;
	_isActive = FALSE;

	if (key1 == -1)
		_hotkeyCount = 0;
	else if (key2 == -1)
		_hotkeyCount = 1;
	else if (key3 == -1)
		_hotkeyCount = 2;
	else
		_hotkeyCount = 3;
}

AutoHotKey::AutoHotKey(const int key1, int key2, int key3, const char* name, int callback_num, HOTKEYCALLBACK* callback, ...) {
	SetKey(key1, key2, key3);
	memcpy(_defaultKey, _key, sizeof(_defaultKey));

	if (name != nullptr)
		strcpy_s(_name, name);

	va_list va;
	va_start(va, callback);

	for (int i = 0; i < callback_num; i++) {
		_callbacks.push_back(move(*callback));
		callback = va_arg(va, HOTKEYCALLBACK*);
	}

	va_end(va);
}

AutoHotKey::AutoHotKey(const int key1, int key2, const char* name, int callback_num, HOTKEYCALLBACK* callback, ...) {
	SetKey(key1, key2, -1);
	memcpy(_defaultKey, _key, sizeof(_defaultKey));

	if (name != nullptr)
		strcpy_s(_name, name);

	va_list va;
	va_start(va, callback);

	for (int i = 0; i < callback_num; i++) {
		_callbacks.push_back(move(*callback));
		callback = va_arg(va, HOTKEYCALLBACK*);
	}

	va_end(va);
}

AutoHotKey::AutoHotKey(const int key1, const char* name, int callback_num, HOTKEYCALLBACK* callback, ...) {
	SetKey(key1, -1, -1);
	memcpy(_defaultKey, _key, sizeof(_defaultKey));

	if (name != nullptr)
		strcpy_s(_name, name);

	va_list va;
	va_start(va, callback);

	for (int i = 0; i < callback_num; i++) {
		_callbacks.push_back(move(*callback));
		callback = va_arg(va, HOTKEYCALLBACK*);
	}

	va_end(va);
}

void AutoHotKey::SetDefaultKey(const int key1, const int key2, const int key3) {
	_defaultKey[0] = key1; _defaultKey[1] = key2; _defaultKey[2] = key3;
}

void AutoHotKey::ResetKey() {
	SetKey(_defaultKey[0], _defaultKey[1], _defaultKey[2]);
}

bool AutoHotKey::isDefaultKey() {
	return memcmp(_key, _defaultKey, sizeof(_key)) == 0;
}

void AutoHotKey::CheckKey() {

	if (_hotkeyCount < 1 || HOTKEY._pressedKey.empty()) {
		_isActive = FALSE;
		return;
	}

	if (_hotkeyCount > HOTKEY._pressedKey.size()) {
		_isActive = FALSE;
		return;
	}

	if (_isActive) {
		if (DXINPUT.isKeyIdle(_key[0]) || DXINPUT.isKeyIdle(_key[1]) || DXINPUT.isKeyIdle(_key[2])) {
			_isActive = FALSE;
		}
		else {
			return;
		}
	}

	for (auto itr = HOTKEY._pressedKey.begin(); itr != HOTKEY._pressedKey.end(); itr++) {

		bool find = FALSE;

		for (int i = 0; i < _hotkeyCount; i++) {
			if (_key[i] == *itr) {
				find = TRUE;
				break;
			}
		}

		if (!find) {
			_isActive = FALSE;
			return;
		}
	}

#if DEBUG_HOTKEY == 1
	LogInstance.WriteLog("[DEBUG] [HOTKEY] [KEY1 = %d] [KEY2 = %d] [KEY3 = %d] [%s]", _key[0], _key[1], _key[2], _name);
#endif

	_isActive = TRUE;

	for (auto itr = _callbacks.begin(); itr != _callbacks.end(); itr++) {
		if ((*itr) != nullptr) {
			DAMAGEMETER.GetLock();
			{
				(*itr)();
			}
			DAMAGEMETER.FreeLock();
		}
	}
}

HotKey::~HotKey() {
	for (auto itr = _hotkeys.begin(); itr != _hotkeys.end(); itr++) {
		delete* itr;
	}
}

void HotKey::CheckKey() {

	for (unsigned int i = 0; i < DXINPUT.GetStateSize(); i++) {
		if (DXINPUT.isKeyRelease(i)) {
			for (auto itr = _pressedKey.begin(); itr != _pressedKey.end(); itr++) {
				if (*itr == i) {

#if DEBUG_HOTKEY == 1
					LogInstance.WriteLog(const_cast<char*>("Release Key - %d"), i);
#endif
					_pressedKey.erase(itr);
					break;
				}
			}
		}
	}

	for (unsigned int i = 0; i < DXINPUT.GetStateSize(); i++) {
		if (DXINPUT.isKeyDown(i)) {
#if DEBUG_HOTKEY == 1
			LogInstance.WriteLog(const_cast<char*>("Down Key - %d"), i);
#endif
			_pressedKey.push_back(i);
		}
	}
}

void HotKey::CheckHotKey() {

	for (auto itr = _hotkeys.begin(); itr != _hotkeys.end(); itr++) {
		(*itr)->CheckKey();
	}
}

void HotKey::UpdateCapture() {

	if (DXINPUT.isKeyDown(DIK_ESCAPE)) {
		CancelCapture();
		return;
	}

	// Only keys pressed after the capture started are taken, in press order.
	for (unsigned int i = 0; i < DXINPUT.GetStateSize(); i++) {

		if (!DXINPUT.isKeyDown(i) || _captureKey.size() >= static_cast<size_t>(HOTKEY_MAX_KEY))
			continue;

		bool find = FALSE;

		for (auto itr = _captureKey.begin(); itr != _captureKey.end(); itr++) {
			if (*itr == static_cast<int>(i)) {
				find = TRUE;
				break;
			}
		}

		if (!find)
			_captureKey.push_back(i);
	}

	if (_captureKey.empty())
		return;

	// Commit once every captured key is back up.
	for (auto itr = _captureKey.begin(); itr != _captureKey.end(); itr++) {
		if (!DXINPUT.isKeyIdle(*itr))
			return;
	}

	int key[HOTKEY_MAX_KEY] = { -1, -1, -1 };

	for (size_t i = 0; i < _captureKey.size(); i++)
		key[i] = _captureKey[i];

	_capture->SetKey(key[0], key[1], key[2]);

	LogInstance.WriteLog("[HotKey::UpdateCapture] %s bound to key1 = %d, key2 = %d, key3 = %d", _capture->GetName(), key[0], key[1], key[2]);

	_capture = nullptr;
	_captureKey.clear();
	_changed = TRUE;
}

void HotKey::Update() {

	CheckKey();

	// Hotkeys stay muted while a rebind is in progress.
	if (_capture != nullptr) {
		UpdateCapture();
		return;
	}

	CheckHotKey();
}

void HotKey::Init() {

	if (!_hotkeys.empty())
		return;

	InsertHotkeyToogle(DIK_LCONTROL, DIK_END, -1);
	InsertHotkeyStop(DIK_LCONTROL, DIK_DELETE, -1);
}

void HotKey::InsertHotkeyToogle(int key1, int key2, int key3) {

//	HOTKEYCALLBACK callback = bind(&SWDamageMeter::Toggle, &DAMAGEMETER);

//	AutoHotKey* hotkey = new AutoHotKey(key1, key2, key3, "Toogle", 1, &callback);
//	hotkey->SetDefaultKey(DIK_LCONTROL, DIK_END, -1);
//	_hotkeys.push_back(hotkey);

}

void HotKey::InsertHotkeyStop(int key1, int key2, int key3) {

	HOTKEYCALLBACK callback1 = std::bind(&SWDamageMeter::Clear, &DAMAGEMETER);
	HOTKEYCALLBACK callback2 = std::bind(&PlayerTable::ClearTable, &PLAYERTABLE);

	AutoHotKey* hotkey = new AutoHotKey(key1, key2, key3, "Clear", 2, &callback1, &callback2);
	hotkey->SetDefaultKey(DIK_LCONTROL, DIK_DELETE, -1);

	_hotkeys.push_back(hotkey);
}

AutoHotKey* HotKey::Find(const char* name) {

	for (auto itr = _hotkeys.begin(); itr != _hotkeys.end(); itr++) {
		if (strcmp((*itr)->GetName(), name) == 0)
			return *itr;
	}

	return nullptr;
}

bool HotKey::SetKeyByName(const char* name, int key1, int key2, int key3) {

	AutoHotKey* hotkey = Find(name);

	if (hotkey == nullptr || key1 == -1)
		return FALSE;

	hotkey->SetKey(key1, key2, key3);
	return TRUE;
}

void HotKey::BeginCapture(AutoHotKey* hotkey) {

	_capture = hotkey;
	_captureKey.clear();
}

void HotKey::CancelCapture() {

	_capture = nullptr;
	_captureKey.clear();
}

bool HotKey::isCapturing(AutoHotKey* hotkey) {

	if (hotkey == nullptr)
		return _capture != nullptr;

	return _capture == hotkey;
}

bool HotKey::ConsumeChanged() {

	bool changed = _changed;
	_changed = FALSE;

	return changed;
}

const char* HotKey::GetKeyName(int key) {

	for (size_t i = 0; i < _countof(KEY_NAME_TABLE); i++) {
		if (KEY_NAME_TABLE[i].key == key)
			return KEY_NAME_TABLE[i].name;
	}

	return nullptr;
}

void HotKey::GetComboName(const int* key, int count, char* out, size_t outLen) {

	out[0] = 0;

	for (int i = 0; i < count && i < HOTKEY_MAX_KEY; i++) {

		const char* name = GetKeyName(key[i]);
		char unknown[16] = { 0 };

		if (name == nullptr) {
			sprintf_s(unknown, 16, "0x%02X", key[i]);
			name = unknown;
		}

		if (i > 0)
			strcat_s(out, outLen, " + ");

		strcat_s(out, outLen, name);
	}
}

std::vector<AutoHotKey*>::const_iterator HotKey::begin() {
	return _hotkeys.begin();
}

std::vector<AutoHotKey*>::const_iterator HotKey::end() {
	return _hotkeys.end();
}
