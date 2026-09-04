#include "pch.h"
#include ".\Damage Meter/Damage Meter.h"
#include ".\UI\UiWindow.h"
#include ".\Damage Meter\MySQLite.h"
#include ".\UI\PlayerTable.h"
#include ".\Damage Meter\SaveData.h"
#include ".\Soulworker Packet\SWPacketMaker.h"
#include "SimpleIni.h"
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console" )
#endif

//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd) 
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
	_In_ PSTR szCmdLine, _In_ int iCmdShow) {
	MiniDump::Begin();
	{
		DWORD errorCode = ERROR_SUCCESS;
		char errorMsg[512] = { 0 };

		do
		{
			const unsigned int codePage = GetACP();
			switch (codePage) {
			case 932: // JP
				_wsetlocale(LC_ALL, L"ja-JP.UTF8");
				errorCode = LANGMANAGER.SetCurrentLang("jp.json");
				break;
			case 949:
				_wsetlocale(LC_ALL, L"ko-KR.UTF8");
				errorCode = LANGMANAGER.SetCurrentLang("kr.json");
				break;
			case 936: // ZH-CN
			case 950: // ZH-TW
				_wsetlocale(LC_ALL, L"zh-TW.UTF8");
				errorCode = LANGMANAGER.SetCurrentLang("zh_tw.json");
				break;
			default:
				_wsetlocale(LC_ALL, L"en-US.UTF8");
				errorCode = LANGMANAGER.SetCurrentLang("en.json");
				break;
			}
			if (errorCode) {
				sprintf_s(errorMsg, "Init Lang failed. err: %lu", errorCode);
				break;
			}

			if (!SWDB.Init()) {
				sprintf_s(errorMsg, "Init database failed.");
				break;
			}

			if (UIWINDOW.Init(1, 1, 1, 1)) {
				if ((errorCode = SWPACKETMAKER.Init())) {
					sprintf_s(errorMsg, "Init PacketCapture failed, err: %lu", errorCode);
					break;
				}
				if (UIOPTION.isUseSaveData())
				{
					if ((errorCode = SAVEDATA.Init())) {
						sprintf_s(errorMsg, "Init SaveData failed, err: %lu", errorCode);
						if (errorCode == ERROR_FILE_CORRUPT) {
							std::string saveDataVersionError = std::string(LANGMANAGER.GetText("STR_SAVEDATA_VERSION_ERROR"));
							ANSItoUTF8(saveDataVersionError.data(), errorMsg, sizeof(errorMsg));
						}
						else if (errorCode == ERROR_FILE_SYSTEM_LIMITATION) {
							std::string saveDataMultipleError = std::string(LANGMANAGER.GetText("STR_SAVEDATA_MULTIPLE_ERROR"));
							ANSItoUTF8(saveDataMultipleError.data(), errorMsg, sizeof(errorMsg));
						}
						break;
					}
				}
				
				UIWINDOW.Run();
			}
			else {
				sprintf_s(errorMsg, "Init UI failed.");
				break;
			}

		} while (false);

		if (errorCode != ERROR_SUCCESS) {
			MessageBoxA(NULL, errorMsg, "SoulMeter", MB_ICONERROR | MB_OK | MB_TOPMOST);
			LogInstance.WriteLog(errorMsg);
		}

		MiniDump::End();
	}

	ShowWindow(UIWINDOW.GetHWND(), 0);
	return TRUE;
}