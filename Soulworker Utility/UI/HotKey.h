#pragma once

#define HOTKEY HotKey::getInstance()
typedef std::function<void(void)> HOTKEYCALLBACK;

#ifdef _DEBUG
#define DEBUG_HOTKEY 0
#endif
#define AUTO_HOTKEY_NAME_LEN 32
#define HOTKEY_MAX_KEY 3
#define HOTKEY_COMBO_LEN 128

struct AutoHotKey {
private:
	int _key[HOTKEY_MAX_KEY];
	int _defaultKey[HOTKEY_MAX_KEY];
	std::vector<HOTKEYCALLBACK> _callbacks;
	bool _isActive;
	int _hotkeyCount;
	char _name[AUTO_HOTKEY_NAME_LEN];

	AutoHotKey() {}
	AutoHotKey(const AutoHotKey& other) {}

public:
	~AutoHotKey();
	AutoHotKey(const int key1, int key2, int key3, const char* name, int callback_num = 0, HOTKEYCALLBACK* callback = nullptr, ...);
	AutoHotKey(const int key1, int key2, const char* name, int callback_num = 0, HOTKEYCALLBACK* callback = nullptr, ...);
	AutoHotKey(const int key1, const char* name, int callback_num = 0, HOTKEYCALLBACK* callback = nullptr, ...);

	const char* GetName() { return _name; }
	const int& GetKeyCount() { return _hotkeyCount; }
	const int* GetKey() { return _key; }
	const int* GetDefaultKey() { return _defaultKey; }

	void SetKey(const int key1, const int key2, const int key3);
	void SetDefaultKey(const int key1, const int key2, const int key3);
	void ResetKey();
	bool isDefaultKey();

	void CheckKey();
};

class HotKey : public Singleton<HotKey> {
	friend AutoHotKey;
private:
	std::vector<AutoHotKey*> _hotkeys;

	AutoHotKey* _capture = nullptr;
	std::vector<int> _captureKey;
	bool _changed = FALSE;

	void CheckKey();
	void CheckHotKey();
	void UpdateCapture();

protected:
	std::vector<int> _pressedKey;

public:
	HotKey(){}
	~HotKey();

	void Init();
	void Update();

	void InsertHotkeyToogle(int key1, int key2, int key3);
	void InsertHotkeyStop(int key1, int key2, int key3);

	AutoHotKey* Find(const char* name);
	bool SetKeyByName(const char* name, int key1, int key2, int key3);

	// Rebind capture: the next key combo pressed is assigned to the hotkey.
	void BeginCapture(AutoHotKey* hotkey);
	void CancelCapture();
	bool isCapturing(AutoHotKey* hotkey = nullptr);
	bool ConsumeChanged();

	static const char* GetKeyName(int key);
	static void GetComboName(const int* key, int count, char* out, size_t outLen);

	std::vector<AutoHotKey*>::const_iterator begin();
	std::vector<AutoHotKey*>::const_iterator end();
};
