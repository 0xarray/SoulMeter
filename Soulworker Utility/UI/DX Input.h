#pragma once

#define DXINPUT DXInput::getInstance()

#pragma comment(lib, "dinput8.lib") 
#pragma comment(lib, "dxguid.lib")

enum INPUT_STATUS {
	Idle = 0,
	Down = 1,
	Press = 2,
	Release = 3,
};

typedef struct _INPUTINFO {
	INPUT_STATUS status;

public:
	_INPUTINFO() :status(INPUT_STATUS::Idle) { }

	void Update(bool isDown) {

		if (isDown) {
			if (status == INPUT_STATUS::Idle) {
				status = INPUT_STATUS::Down;
				return;
			}

			if (status == INPUT_STATUS::Down) {
				status = INPUT_STATUS::Press;
				return;
			}

			if (status == INPUT_STATUS::Release) {
				status = INPUT_STATUS::Down;
				return;
			}
		}

		if (!isDown) {
			if (status == INPUT_STATUS::Down) {
				status = INPUT_STATUS::Release;
				return;
			}

			if (status == INPUT_STATUS::Press) {
				status = INPUT_STATUS::Release;
				return;
			}

			if (status == INPUT_STATUS::Release) {
				status = INPUT_STATUS::Idle;
				return;
			}
		}
	}

	bool isDown() { if (status == INPUT_STATUS::Down) return TRUE; return FALSE; }
	bool isPress() { if (status == INPUT_STATUS::Press) return TRUE; return FALSE; }
	bool isRelease() { if (status == INPUT_STATUS::Release) return TRUE; return FALSE; }
	bool isIdle() { if (status == INPUT_STATUS::Idle) return TRUE; return FALSE; }
}INPUTINFO;

class DXInput : public Singleton<DXInput> {
private:
	IDirectInput8* _directInput;
	IDirectInputDevice8* _keyboard;

	INPUTINFO _inputInfo[256];
	unsigned char _keyboardState[256];

	void Shutdown();

public:
	DXInput() : _directInput(nullptr), _keyboard(nullptr) {}
	~DXInput() { Shutdown(); }

	bool Init(HINSTANCE hinst, HWND hWnd);
	bool Update();

	bool isKeyDown(unsigned int i);
	bool isKeyRelease(unsigned int i);
	bool isKeyIdle(unsigned int i);

	unsigned int GetStateSize();
};