/////////////////////////////////////////
//  (             (                
//  )\ )     (    )\ )             
// (()/((    )\  (()/(    (   )    
//  /(_))\ )((_)  /(_))  ))\ /((   
// (_))(()/( _   (_))_  /((_|_))\  
// / __|)(_)) |   |   \(_)) _)((_) 
// \__ \ || | |   | |) / -_)\ V /  
// |___/\_, |_|   |___/\___| \_/   
//      |__/                       
//
// 2014 Moritz Kretz
/////////////////////////////////////////
// Cerb3rus
// -- I don't have fancy ASCII art :( --
// 2014 David Lehn
/////////////////////////////////////////

#include <Windows.h>
#include <string>
#include <vector>
#include <map>
#include <shlobj.h>
#include <Shlwapi.h>

#include "sup_constants.h"
#include "sup_chat_commands.h"
#include "sup_window_util.h"

#pragma comment(lib, "Shlwapi.lib")
#pragma pack(1)

namespace SUP
{
	enum ScreenCorner
	{
		TopLeft = 0,
		TopRight,
		BottomRight,
		BottomLeft
	};

	HINSTANCE hInst = NULL;
	HINSTANCE hLib = 0;
	FARPROC p[14] = {0};

	HWND hWnd = NULL;
	LONG oldWndProc;
	HMENU hMenu = NULL;

	HMENU hUtilMenu = NULL;
	HMENU hLayoutMenu = NULL;
	HMENU hDisplayMenu = NULL;
	HMENU hPosMenu = NULL;
	
	ChatCommandHandler commandHandler;

	std::wstring iniPath;
	bool enableChatFormat = true;
	bool hideAds = false;
	bool hideAppToolbar = false;
	bool hideIdentityPanel = false;
	ScreenCorner notifCorner = BottomRight;
	std::wstring notifDisplay = L"";
	int notifOffsetX = 0;
	int notifOffsetY = 0;

	std::vector<std::wstring> displayNames;
	int appToolbarHeight = 0;
	int identityPanelHeight = 0;

	void createMenus(HMENU _parent)
	{
		hUtilMenu = CreateMenu();
		AppendMenu(_parent, MF_STRING | MF_POPUP, (UINT_PTR)hUtilMenu, L"&Util");

		UINT flags = MF_STRING | MF_UNCHECKED;
		AppendMenu(hUtilMenu, flags, ID_ENABLE_CHAT_FORMAT, L"Allow Chat &Formatting");

		hLayoutMenu = CreateMenu();
		AppendMenu(hUtilMenu, MF_STRING | MF_POPUP, (UINT_PTR)hLayoutMenu,
			L"&Layout");
		AppendMenu(hLayoutMenu, flags, ID_HIDE_ADS, L"Hide &Ads");
		AppendMenu(hLayoutMenu, flags, ID_HIDE_APP_TOOLBAR, L"Hide &Home Toolbar");
		AppendMenu(hLayoutMenu, flags, ID_HIDE_IDENTITY_PANEL, L"Hide &Identity Panel");

		HMENU notifMenu = CreateMenu();
		AppendMenu(hUtilMenu, MF_STRING | MF_POPUP, (UINT_PTR)notifMenu,
			L"Show &Notifications");

		hDisplayMenu = CreateMenu();
		AppendMenu(notifMenu, MF_STRING | MF_POPUP, (UINT_PTR)hDisplayMenu,
			L"On &Display");

		hPosMenu = CreateMenu();
		AppendMenu(notifMenu, MF_STRING | MF_POPUP, (UINT_PTR)hPosMenu,
			L"At &Location");
		
		AppendMenu(hPosMenu, flags, ID_SET_NOTIFICATION_POS + TopLeft, L"&Top Left");
		AppendMenu(hPosMenu, flags, ID_SET_NOTIFICATION_POS + TopRight, L"T&op Right");
		AppendMenu(hPosMenu, flags, ID_SET_NOTIFICATION_POS + BottomRight, L"Bottom &Right");
		AppendMenu(hPosMenu, flags, ID_SET_NOTIFICATION_POS + BottomLeft, L"Bottom &Left");

		HMENU helpMenu = CreateMenu();
		AppendMenu(hUtilMenu, MF_STRING | MF_POPUP, (UINT_PTR)helpMenu,
			L"&Help");

		AppendMenu(helpMenu, MF_STRING, ID_SHOW_HELP, L"Show Online &Help");
		AppendMenu(helpMenu, MF_STRING, ID_SHOW_UPDATES, L"Check for &New Version");
		AppendMenu(helpMenu, MF_STRING | MF_DISABLED, 0, L"SUP Version: " SUP_VERSION);

		HMENU creditsMenu = CreateMenu();
		AppendMenu(helpMenu, MF_STRING | MF_POPUP, (UINT_PTR)creditsMenu,
			L"&Credits");
		AppendMenu(creditsMenu, MF_STRING, ID_SHOW_CREDITS_DAVE, L"&David Lehn");
		AppendMenu(creditsMenu, MF_STRING, ID_SHOW_CREDITS_MOE, L"&Moritz Kretz");
	}

	void updateUtilMenu()
	{
		CheckMenuItem(hUtilMenu, ID_ENABLE_CHAT_FORMAT,
			enableChatFormat ? MF_CHECKED : MF_UNCHECKED);
	}

	void updateLayoutMenu()
	{
		CheckMenuItem(hLayoutMenu, ID_HIDE_ADS,
			hideAds ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(hLayoutMenu, ID_HIDE_APP_TOOLBAR,
			hideAppToolbar ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(hLayoutMenu, ID_HIDE_IDENTITY_PANEL,
			hideIdentityPanel ? MF_CHECKED : MF_UNCHECKED);
	}

	void updatePosMenu()
	{
		for (unsigned i = 0; i < 4; i++)
		{
			CheckMenuItem(hPosMenu, ID_SET_NOTIFICATION_POS + i,
				(i == notifCorner) ? MF_CHECKED : MF_UNCHECKED);
		}
	}

	void updateDisplayMenu()
	{
		int items = GetMenuItemCount(hDisplayMenu);
		for (int i = items - 1; i >= 0; i--)
		{
			DeleteMenu(hDisplayMenu, i, MF_BYPOSITION);
		}

		displayNames.clear();

		static auto enumDisplays = [](HMONITOR _monitor, HDC _hdc, LPRECT _rect, LPARAM _param)
			-> BOOL
		{
			MONITORINFOEX info;
			info.cbSize = sizeof(MONITORINFOEX);

			if (GetMonitorInfo(_monitor, &info))
				displayNames.push_back(info.szDevice);

			return TRUE;
		};

		EnumDisplayMonitors(NULL, NULL, enumDisplays, NULL);

		for (unsigned i = 0; i < displayNames.size(); i++)
		{
			UINT flags = MF_STRING
				| ((displayNames[i] == notifDisplay) ? MF_CHECKED : MF_UNCHECKED);
			AppendMenu(hDisplayMenu, flags, ID_SET_NOTIFICATION_DISPLAY + i,
				displayNames[i].c_str());
		}
	}

	POINT calcWindowPos(const RECT& _base)
	{
		RECT workArea = {0};

		static auto enumDisplays = [](HMONITOR _monitor, HDC _hdc, LPRECT _rect, LPARAM _param)
			-> BOOL
		{
			RECT& r = *(RECT*)_param;
			MONITORINFOEX info;
			info.cbSize = sizeof(MONITORINFOEX);

			if (!GetMonitorInfo(_monitor, &info))
				TRUE;

			r = info.rcWork;

			return info.szDevice != notifDisplay;
		};
		EnumDisplayMonitors(NULL, NULL, enumDisplays, (LPARAM)&workArea);

		int width = _base.right - _base.left, height = _base.bottom - _base.top;

		POINT result;
		if (notifCorner == TopLeft || notifCorner == BottomLeft)
			result.x = workArea.left + notifOffsetX;
		else
			result.x = workArea.right - width - notifOffsetX;

		if (notifCorner == TopLeft || notifCorner == TopRight)
			result.y = workArea.top + notifOffsetY;
		else
			result.y = workArea.bottom - height - notifOffsetY;

		return result;
	}

	void chatFormatChanged()
	{
		WritePrivateProfileString(L"config", L"enableChatFormat",
			std::to_wstring(enableChatFormat ? 1 : 0).c_str(), iniPath.c_str());

		std::wstring cmd = L"/setupkey *Lib/Conversation/EnableWiki ";
		cmd += enableChatFormat ? L'1' : L'0';

		commandHandler.queueCommand(cmd, L"changeChatFormat", true);
	}

	void hideAdsChanged()
	{
		WritePrivateProfileString(L"config", L"hideAds", hideAds ? L"1" : L"0",
			iniPath.c_str());

		if (!hideAds)
			return;

		RECT r;
		HWND banner = NULL;
		while (true)
		{
			banner = FindWindowEx(hWnd, banner, CLS_CHAT_BANNER.c_str(), nullptr);
			if (!banner)
				break;

			GetWindowRect(banner, &r);
			SetWindowPos(banner, NULL, 0, 0, r.right - r.left, 0, SWP_NOMOVE | SWP_NOZORDER);
		}

		// Combined view
		HWND parent = NULL;
		while (true)
		{
			parent = FindWindowEx(hWnd, parent, CLS_CONVERSATION_FORM.c_str(), nullptr);
			if (!parent)
				break;

			banner = NULL;
			while (true)
			{
				banner = FindWindowEx(hWnd, banner, CLS_CHAT_BANNER.c_str(), nullptr);
				if (!banner)
					break;

				GetWindowRect(banner, &r);
				SetWindowPos(banner, NULL, 0, 0, r.right - r.left, 0, SWP_NOMOVE | SWP_NOZORDER);
			}
		}

		// Split view
		parent = NULL;
		while (true)
		{
			parent = findWindowInProcess(CLS_CONVERSATION_FORM.c_str(), nullptr, parent);
			if (!parent)
				break;

			banner = NULL;
			while (true)
			{
				banner = FindWindowEx(hWnd, banner, CLS_CHAT_BANNER.c_str(), nullptr);
				if (!banner)
					break;

				GetWindowRect(banner, &r);
				SetWindowPos(banner, NULL, 0, 0, r.right - r.left, 0, SWP_NOMOVE | SWP_NOZORDER);
			}
		}
	}

	void hideAppToolbarChanged()
	{
		WritePrivateProfileString(L"config", L"hideAppToolbar", hideAppToolbar ? L"1" : L"0",
			iniPath.c_str());

		HWND appToolbar = FindWindowEx(hWnd, NULL, CLS_APP_TOOLBAR.c_str(), nullptr);
		if (!appToolbar)
			return;

		RECT r;
		GetWindowRect(appToolbar, &r);

		if (hideAppToolbar)
		{
			appToolbarHeight = r.bottom - r.top;
			WritePrivateProfileString(L"config", L"appToolbarHeight",
				std::to_wstring(appToolbarHeight).c_str(), iniPath.c_str());
		}
		else
		{
			appToolbarHeight = GetPrivateProfileInt(L"config", L"appToolbarHeight",
				appToolbarHeight, iniPath.c_str());
		}

		SetWindowPos(appToolbar, NULL, 0, 0, r.right - r.left,
			hideAppToolbar ? 0 : appToolbarHeight, SWP_NOMOVE | SWP_NOZORDER);
	}

	void hideIdentityPanelChanged()
	{
		WritePrivateProfileString(L"config", L"hideIdentityPanel", hideIdentityPanel ? L"1" : L"0",
			iniPath.c_str());

		HWND identityPanel = FindWindowEx(hWnd, NULL, CLS_IDENTITY_PANEL.c_str(), nullptr);
		if (!identityPanel)
			return;

		RECT r;
		GetWindowRect(identityPanel, &r);

		if (hideIdentityPanel)
		{
			identityPanelHeight = r.bottom - r.top;
			WritePrivateProfileString(L"config", L"identityPanelHeight",
				std::to_wstring(identityPanelHeight).c_str(), iniPath.c_str());
		}
		else
		{
			identityPanelHeight = GetPrivateProfileInt(L"config", L"identityPanelHeight",
				identityPanelHeight, iniPath.c_str());
		}

		SetWindowPos(identityPanel, NULL, 0, 0, r.right - r.left,
			hideIdentityPanel ? 0 : identityPanelHeight, SWP_NOMOVE | SWP_NOZORDER);
	}

	LRESULT CALLBACK newWndProc(HWND _hwnd, UINT _message, WPARAM _wParam, LPARAM _lParam)
	{
		switch (_message)
		{
		case WM_ERASEBKGND:
		{
			// This message happens to be sent when the user changes Skype's layout. We can use
			// this to check whether we need to reattach our custom menu.
			HMENU hCurrent = GetMenu(_hwnd);
			if (hCurrent != hMenu)
			{
				createMenus(hCurrent);
				hMenu = hCurrent;
			}
			break;
		}
		case WM_INITMENUPOPUP:
			if ((HMENU)_wParam == hUtilMenu)
				updateUtilMenu();
			else if ((HMENU)_wParam == hLayoutMenu)
				updateLayoutMenu();
			else if ((HMENU)_wParam == hPosMenu)
				updatePosMenu();
			else if ((HMENU)_wParam == hDisplayMenu)
				updateDisplayMenu();
			break;
		case WM_COMMAND:
			if (_wParam == ID_ENABLE_CHAT_FORMAT)
			{
				enableChatFormat = !enableChatFormat;
				chatFormatChanged();
			}
			else if (_wParam == ID_HIDE_ADS)
			{
				hideAds = !hideAds;
				hideAdsChanged();
			}
			else if (_wParam == ID_HIDE_APP_TOOLBAR)
			{
				hideAppToolbar = !hideAppToolbar;
				hideAppToolbarChanged();
			}
			else if (_wParam == ID_HIDE_IDENTITY_PANEL)
			{
				hideIdentityPanel = !hideIdentityPanel;
				hideIdentityPanelChanged();
			}
			else if (_wParam == ID_SHOW_HELP)
			{
				ShellExecute(NULL, L"open",
					L"https://github.com/dlehn/Skype-Utility-Project#what-do-those-options-do",
					nullptr, nullptr, SW_SHOWNORMAL);
			}
			else if (_wParam == ID_SHOW_UPDATES)
			{
				ShellExecute(NULL, L"open",
					L"https://github.com/dlehn/Skype-Utility-Project/releases", nullptr, nullptr,
					SW_SHOWNORMAL);
			}
			else if (_wParam == ID_SHOW_CREDITS_DAVE)
			{
				ShellExecute(NULL, L"open", L"http://blog.mountain-view.de/", nullptr, nullptr,
					SW_SHOWNORMAL);
			}
			else if (_wParam == ID_SHOW_CREDITS_MOE)
			{
				ShellExecute(NULL, L"open", L"http://kretzmoritz.wordpress.com/", nullptr,
					nullptr, SW_SHOWNORMAL);
			}
			else if (_wParam >= ID_SET_NOTIFICATION_DISPLAY
				&& _wParam < ID_SET_NOTIFICATION_DISPLAY + displayNames.size())
			{
				notifDisplay = displayNames[_wParam - ID_SET_NOTIFICATION_DISPLAY];
				WritePrivateProfileString(L"config", L"notifDisplay",
					notifDisplay.c_str(), iniPath.c_str());
			}
			else if (_wParam >= ID_SET_NOTIFICATION_POS
				&& _wParam < ID_SET_NOTIFICATION_POS + 4)
			{
				notifCorner = (ScreenCorner)(_wParam - ID_SET_NOTIFICATION_POS);
				WritePrivateProfileString(L"config", L"notifCorner",
					std::to_wstring((int)notifCorner).c_str(), iniPath.c_str());
			}
			break;
		}

		return CallWindowProc((WNDPROC)oldWndProc, _hwnd, _message, _wParam, _lParam);
	}

	void APIENTRY notificationHook(HWINEVENTHOOK _hWinEventHook, DWORD _event, HWND _hwnd,
		LONG _idObject, LONG _idChild, DWORD _idEventThread, DWORD _dwmsEventTime)
	{
		switch (_event)
		{
		case EVENT_OBJECT_SHOW:
		{
			wchar_t className[MAX_CLASS_NAME] = {0};
			GetClassName(_hwnd, className, MAX_CLASS_NAME);

			if (className == CLS_CHAT_RICH_EDIT)
			{
				commandHandler.executePendingCommands(_hwnd);
			}
			break;
		}
		case EVENT_OBJECT_LOCATIONCHANGE:
		{
			wchar_t className[MAX_CLASS_NAME] = {0};
			GetClassName(_hwnd, className, MAX_CLASS_NAME);

			if (className == CLS_TRAY_ALERT)
			{
				RECT rect;
				if (!GetWindowRect(_hwnd, &rect))
					return;

				POINT pos = calcWindowPos(rect);
				MoveWindow(_hwnd, pos.x, pos.y, rect.right - rect.left, rect.bottom - rect.top,
					TRUE);
			}
			else if ((hideAds && className == CLS_CHAT_BANNER)
				|| (hideAppToolbar && className == CLS_APP_TOOLBAR)
				|| (hideIdentityPanel && className == CLS_IDENTITY_PANEL))
			{
				RECT r;
				GetWindowRect(_hwnd, &r);
				SetWindowPos(_hwnd, NULL, 0, 0, r.right - r.left, 0, SWP_NOMOVE | SWP_NOZORDER);
			}
			break;
		}
		}
	}

	DWORD WINAPI hook()
	{
		wchar_t buffer[MAX_PATH];

		DWORD procId = GetCurrentProcessId();
		SetWinEventHook(EVENT_OBJECT_SHOW,
			EVENT_OBJECT_SHOW, hInst,
			notificationHook, procId, NULL,
			WINEVENT_INCONTEXT | WINEVENT_SKIPOWNTHREAD);
		SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,
			EVENT_OBJECT_LOCATIONCHANGE, hInst,
			notificationHook, procId, NULL,
			WINEVENT_INCONTEXT | WINEVENT_SKIPOWNTHREAD);

		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, buffer)))
			iniPath = std::wstring(buffer) + L"\\Skype\\sup.ini";
		else
		{
			GetModuleFileName((HMODULE)hInst, buffer, MAX_PATH);
			PathRemoveFileSpec(buffer);

			iniPath = std::wstring(buffer) + L"\\sup.ini";
		}

		enableChatFormat = GetPrivateProfileInt(L"config", L"enableChatFormat", 1,
			iniPath.c_str()) != 0;
		hideAds = GetPrivateProfileInt(L"config", L"hideAds", 0,
			iniPath.c_str()) != 0;
		hideAppToolbar = GetPrivateProfileInt(L"config", L"hideAppToolbar", 0,
			iniPath.c_str()) != 0;
		hideIdentityPanel = GetPrivateProfileInt(L"config", L"hideIdentityPanel", 0,
			iniPath.c_str()) != 0;
		notifCorner = (ScreenCorner)GetPrivateProfileInt(L"config", L"notifCorner", BottomRight,
			iniPath.c_str());
		GetPrivateProfileString(L"config", L"notifDisplay", L"", buffer, MAX_PATH,
			iniPath.c_str());
		notifDisplay = buffer;
		notifOffsetX = GetPrivateProfileInt(L"config", L"notifOffsetX", 0, iniPath.c_str());
		notifOffsetY = GetPrivateProfileInt(L"config", L"notifOffsetY", 0, iniPath.c_str());

		while (hWnd == NULL)
			hWnd = findWindowInProcess(L"tSkMainForm", nullptr);

		oldWndProc = SetWindowLong(hWnd, GWL_WNDPROC, (LONG)newWndProc);

		commandHandler.setMainForm(hWnd);

		hideAdsChanged();
		chatFormatChanged();

		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		return TRUE;
	}
}

BOOL WINAPI DllMain(HINSTANCE _hInst, DWORD _reason, LPVOID _reserved)
{
	if (_reason == DLL_PROCESS_ATTACH)
	{
		SUP::hInst = _hInst;

		wchar_t buffer[MAX_PATH];
		GetSystemDirectory(buffer, MAX_PATH);
		std::wstring path = std::wstring(buffer) + L"\\d3d9.dll";

		SUP::hLib = LoadLibrary(path.c_str());
		
		if (!SUP::hLib)
		{
			return FALSE;
		}

		SUP::p[0] = GetProcAddress(SUP::hLib, "D3DPERF_BeginEvent");
		SUP::p[1] = GetProcAddress(SUP::hLib, "D3DPERF_EndEvent");
		SUP::p[2] = GetProcAddress(SUP::hLib, "D3DPERF_GetStatus");
		SUP::p[3] = GetProcAddress(SUP::hLib, "D3DPERF_QueryRepeatFrame");
		SUP::p[4] = GetProcAddress(SUP::hLib, "D3DPERF_SetMarker");
		SUP::p[5] = GetProcAddress(SUP::hLib, "D3DPERF_SetOptions");
		SUP::p[6] = GetProcAddress(SUP::hLib, "D3DPERF_SetRegion");
		SUP::p[7] = GetProcAddress(SUP::hLib, "DebugSetLevel");
		SUP::p[8] = GetProcAddress(SUP::hLib, "DebugSetMute");
		SUP::p[9] = GetProcAddress(SUP::hLib, "Direct3DCreate9");
		SUP::p[10] = GetProcAddress(SUP::hLib, "Direct3DCreate9Ex");
		SUP::p[11] = GetProcAddress(SUP::hLib, "Direct3DShaderValidatorCreate9");
		SUP::p[12] = GetProcAddress(SUP::hLib, "PSGPError");
		SUP::p[13] = GetProcAddress(SUP::hLib, "PSGPSampleTexture");

		CreateThread(0, 0, (LPTHREAD_START_ROUTINE)SUP::hook, nullptr, 0, nullptr);
	}
	else if (_reason == DLL_PROCESS_DETACH)
	{
		FreeLibrary(SUP::hLib);
	}

	return TRUE;
}

// D3DPERF_BeginEvent
extern "C" __declspec(naked) void __stdcall __E__0__()
{
	__asm
	{
		jmp SUP::p[0 * 4];
	}
}

// D3DPERF_EndEvent
extern "C" __declspec(naked) void __stdcall __E__1__()
{
	__asm
	{
		jmp SUP::p[1 * 4];
	}
}

// D3DPERF_GetStatus
extern "C" __declspec(naked) void __stdcall __E__2__()
{
	__asm
	{
		jmp SUP::p[2 * 4];
	}
}

// D3DPERF_QueryRepeatFrame
extern "C" __declspec(naked) void __stdcall __E__3__()
{
	__asm
	{
		jmp SUP::p[3 * 4];
	}
}

// D3DPERF_SetMarker
extern "C" __declspec(naked) void __stdcall __E__4__()
{
	__asm
	{
		jmp SUP::p[4 * 4];
	}
}

// D3DPERF_SetOptions
extern "C" __declspec(naked) void __stdcall __E__5__()
{
	__asm
	{
		jmp SUP::p[5 * 4];
	}
}

// D3DPERF_SetRegion
extern "C" __declspec(naked) void __stdcall __E__6__()
{
	__asm
	{
		jmp SUP::p[6 * 4];
	}
}

// DebugSetLevel
extern "C" __declspec(naked) void __stdcall __E__7__()
{
	__asm
	{
		jmp SUP::p[7 * 4];
	}
}

// DebugSetMute
extern "C" __declspec(naked) void __stdcall __E__8__()
{
	__asm
	{
		jmp SUP::p[8 * 4];
	}
}

// Direct3DCreate9
extern "C" __declspec(naked) void __stdcall __E__9__()
{
	__asm
	{
		jmp SUP::p[9 * 4];
	}
}

// Direct3DCreate9Ex
extern "C" __declspec(naked) void __stdcall __E__10__()
{
	__asm
	{
		jmp SUP::p[10 * 4];
	}
}

// Direct3DShaderValidatorCreate9
extern "C" __declspec(naked) void __stdcall __E__11__()
{
	__asm
	{
		jmp SUP::p[11 * 4];
	}
}

// PSGPError
extern "C" __declspec(naked) void __stdcall __E__12__()
{
	__asm
	{
		jmp SUP::p[12 * 4];
	}
}

// PSGPSampleTexture
extern "C" __declspec(naked) void __stdcall __E__13__()
{
	__asm
	{
		jmp SUP::p[13 * 4];
	}
}