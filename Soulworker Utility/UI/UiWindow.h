#pragma once

#define UI_WINDOW_CLASSNAME L"UI_WINDOW_CLASSNAME"
#define IMGUI_OPTION_FILE "option.xml"

#define UIWINDOW UiWindow::getInstance()

class UiWindow : public Singleton<UiWindow> {
private:
	IDXGISwapChain* _swapChain;
	ID3D11RenderTargetView* _renderTargetView;

	HINSTANCE _hInst;
	HWND _hWnd;

	unsigned int _x;
	unsigned int _y;
	unsigned int _width;
	unsigned int _height;

	ImGuiContext* _imGuiContext;

	timePoint _prevTimePoint;
	float _deltaTime;

	void DrawScene();
	void Update();
	void OnResize();
	void CalcDeltaTime();

	bool InitImGUI();
	bool SetFontList();

	void UpdateMainTable();

	bool _isExit = false;

public:
	UiWindow();
	~UiWindow();
	 
	void Run();
	bool Init(unsigned int x, unsigned int y, unsigned int width, unsigned int height);
	LRESULT CALLBACK WndProc(HWND hWnd, unsigned int msg, WPARAM wParam, LPARAM lParam);
	const HWND& GetHWND();
	const float& GetDeltaTime();
};