/*
 * MusicWidget - Desktop Music Visualizer
 */

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include "media_info.h"
#include "gif_player.h"
#include "settings.h"

// DWM CLOAK 속성 (Windows 10+)
#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 13
#endif
#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif
#define DWM_CLOAKED_APP 1

#define WINDOW_WIDTH  400
#define WINDOW_HEIGHT 120
#define UPDATE_TIMER_ID 1
#define UPDATE_INTERVAL 200
#define GIF_TIMER_ID 2
#define GIF_INTERVAL 66           // 66ms = 15 FPS (CPU 최적화)
#define SAVE_TIMER_ID 3
#define SAVE_INTERVAL 5000        // 5초마다 위치 저장 체크
#define TOPMOST_TIMER_ID 4
#define TOPMOST_INTERVAL 2000     // 2초마다 최상위 유지 (CPU 최적화)

// 메뉴 관련
#define ID_MENU_SHOW_GIFS 1001
#define ID_MENU_HIDE_GIFS 1002
#define ID_MENU_EXIT 1005
#define ID_MENU_SPEED_SLOW 1010
#define ID_MENU_SPEED_NORMAL 1011
#define ID_MENU_SPEED_FAST 1012
#define ID_MENU_SPEED_VFAST 1013
#define ID_MENU_SPEED_ULTRA 1014
#define ID_MENU_AUTOSTART 1020
#define ID_MENU_CLICKTHROUGH 1021
#define ID_MENU_AUTOMODE 1022

// 트레이 아이콘 관련
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_SHOW 2001
#define ID_TRAY_EXIT 2099

// 설정 버튼 영역
#define SETTINGS_BTN_SIZE 24
#define SETTINGS_BTN_X (WINDOW_WIDTH - SETTINGS_BTN_SIZE - 10)
#define SETTINGS_BTN_Y 10

// 윈도우 클래스 이름
const wchar_t CLASS_NAME[] = L"MusicWidgetClass";

// 폰트 핸들
HFONT g_hFont = NULL;
HFONT g_hFontSmall = NULL;

// 현재 미디어 정보
MediaInfo g_mediaInfo = {0};

// 앨범 아트 비트맵
HBITMAP g_hAlbumArt = NULL;
int g_albumArtSize = 80;

HWND g_hwndMain = NULL;

// 트레이 아이콘
static NOTIFYICONDATAW g_trayIcon = {0};
static int g_widgetVisible = 1;

// 클릭 투과 모드
static int g_clickThrough = 0;
static int g_autoGameMode = 1;  // 자동 게임 모드 (기본 활성화)
static int g_manualClickThrough = 0;  // 수동으로 설정된 클릭 투과 상태

// 전체 화면 앱 감지
int IsFullscreenAppRunning(void) {
    HWND hwndForeground = GetForegroundWindow();
    if (!hwndForeground || hwndForeground == g_hwndMain) return 0;
    
    // 데스크톱이나 쉘 창은 무시
    wchar_t className[256];
    GetClassNameW(hwndForeground, className, 256);
    if (wcscmp(className, L"Progman") == 0 || 
        wcscmp(className, L"WorkerW") == 0 ||
        wcscmp(className, L"Shell_TrayWnd") == 0) {
        return 0;
    }
    
    RECT rcWindow;
    GetWindowRect(hwndForeground, &rcWindow);
    
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // 전체 화면을 덮고 있으면 게임/전체화면 앱으로 간주
    return (rcWindow.left <= 0 && rcWindow.top <= 0 && 
            rcWindow.right >= screenWidth && 
            rcWindow.bottom >= screenHeight);
}

// 클릭 투과 상태 설정 (직접 설정)
void SetClickThrough(int enable) {
    if (g_clickThrough == enable) return;  // 이미 같은 상태면 스킵
    
    g_clickThrough = enable;
    
    // 메인 위젯은 WM_NCHITTEST에서 처리하므로 WS_EX_TRANSPARENT 사용 안 함
    // GIF 창들에만 적용
    GifPlayer_SetClickThrough(g_clickThrough);
}

// 클릭 투과 모드 토글 (수동)
void ToggleClickThrough(void) {
    g_manualClickThrough = !g_manualClickThrough;
    SetClickThrough(g_manualClickThrough);
}

// 설정
AppSettings g_settings = {0};
static int g_lastWidgetX = -1;
static int g_lastWidgetY = -1;

// 설정 저장 함수
void SaveCurrentSettings(void) {
    // 위젯 위치
    RECT rc;
    if (g_hwndMain && GetWindowRect(g_hwndMain, &rc)) {
        g_settings.widgetX = rc.left;
        g_settings.widgetY = rc.top;
    }
    
    // GIF 위치 및 Z-order
    g_settings.gifCount = GifPlayer_GetCount();
    for (int i = 0; i < g_settings.gifCount && i < MAX_GIFS; i++) {
        GifPlayer_GetPosition(i, &g_settings.gifs[i].x, &g_settings.gifs[i].y, &g_settings.gifs[i].size);
        g_settings.gifs[i].zOrder = GifPlayer_GetZOrder(i);
    }
    
    // 속도
    g_settings.gifSpeedMultiplier = GifPlayer_GetSpeedMultiplier();
    
    // 자동 실행
    g_settings.autoStart = Settings_IsAutoStartEnabled();
    
    Settings_Save(&g_settings);
}

// 설정 버튼 영역 체크
int IsInSettingsButton(int x, int y) {
    return (x >= SETTINGS_BTN_X && x <= SETTINGS_BTN_X + SETTINGS_BTN_SIZE &&
            y >= SETTINGS_BTN_Y && y <= SETTINGS_BTN_Y + SETTINGS_BTN_SIZE);
}

// 설정 메뉴 표시
void ShowSettingsMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    HMENU hSpeedMenu = CreatePopupMenu();
    
    float currentSpeed = GifPlayer_GetSpeedMultiplier();
    
    // 속도 서브메뉴 (배율 기반)
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 0.5f ? MF_CHECKED : 0), ID_MENU_SPEED_SLOW, L"0.5x (Slow)");
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 1.0f ? MF_CHECKED : 0), ID_MENU_SPEED_NORMAL, L"1.0x (Normal)");
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 1.5f ? MF_CHECKED : 0), ID_MENU_SPEED_FAST, L"1.5x (Fast)");
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 2.0f ? MF_CHECKED : 0), ID_MENU_SPEED_VFAST, L"2.0x (Very Fast)");
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 4.0f ? MF_CHECKED : 0), ID_MENU_SPEED_ULTRA, L"4.0x (Ultra)");
    
    AppendMenuW(hMenu, MF_STRING, ID_MENU_SHOW_GIFS, L"Show All GIFs");
    AppendMenuW(hMenu, MF_STRING, ID_MENU_HIDE_GIFS, L"Hide All GIFs");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSpeedMenu, L"GIF Speed");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    
    // 자동 실행 옵션
    int autoStartEnabled = Settings_IsAutoStartEnabled();
    AppendMenuW(hMenu, MF_STRING | (autoStartEnabled ? MF_CHECKED : 0), ID_MENU_AUTOSTART, L"Start with Windows");
    
    // 클릭 투과 모드 (게임용)
    AppendMenuW(hMenu, MF_STRING | (g_manualClickThrough ? MF_CHECKED : 0), ID_MENU_CLICKTHROUGH, L"Click-through Mode");
    
    // 자동 게임 모드
    AppendMenuW(hMenu, MF_STRING | (g_autoGameMode ? MF_CHECKED : 0), ID_MENU_AUTOMODE, L"Auto Game Mode (Fullscreen)");
    
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_MENU_EXIT, L"Exit");
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// 트레이 아이콘 생성
void CreateTrayIcon(HWND hwnd) {
    g_trayIcon.cbSize = sizeof(NOTIFYICONDATAW);
    g_trayIcon.hWnd = hwnd;
    g_trayIcon.uID = 1;
    g_trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_trayIcon.uCallbackMessage = WM_TRAYICON;
    g_trayIcon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(g_trayIcon.szTip, 128, L"MusicWidget - 우클릭으로 설정");
    
    Shell_NotifyIconW(NIM_ADD, &g_trayIcon);
}

// 트레이 아이콘 삭제
void RemoveTrayIcon(void) {
    Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
}

// 위젯 위치 저장 (복원용)
static int g_savedWidgetX = 0;
static int g_savedWidgetY = 0;

// 위젯 숨기기 (완전 숨기기 - 하이브리드 방식)
void HideWidget(HWND hwnd) {
    if (!g_widgetVisible) return;
    
    // 현재 위치 저장
    RECT rc;
    GetWindowRect(hwnd, &rc);
    g_savedWidgetX = rc.left;
    g_savedWidgetY = rc.top;
    
    // 1. DWM Cloak 활성화 (Windows 10+에서 즉시 숨김)
    BOOL cloakValue = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloakValue, sizeof(cloakValue));
    
    // 2. 투명도를 0으로 (완전 투명)
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    
    // 3. 창을 화면 밖으로 이동
    SetWindowPos(hwnd, NULL, -32000, -32000, 0, 0, 
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
    
    // 4. 창 숨기기
    ShowWindow(hwnd, SW_HIDE);
    
    // GIF 위젯은 유지 (음악 위젯만 숨김)
    g_widgetVisible = 0;
}

// 위젯 보이기
void ShowWidget(HWND hwnd) {
    if (g_widgetVisible) return;
    
    // 1. DWM Cloak 해제 (Windows 10+)
    BOOL cloakValue = FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloakValue, sizeof(cloakValue));
    
    // 2. 투명도 복원 (90% 불투명)
    SetLayeredWindowAttributes(hwnd, 0, 230, LWA_ALPHA);
    
    // 3. 원래 위치로 복원 및 최상위
    SetWindowPos(hwnd, HWND_TOPMOST, g_savedWidgetX, g_savedWidgetY, 
                 WINDOW_WIDTH, WINDOW_HEIGHT, SWP_SHOWWINDOW);
    
    // 4. 창 보이기
    ShowWindow(hwnd, SW_SHOW);
    
    // 5. 포그라운드로 가져오기
    SetForegroundWindow(hwnd);
    
    // 6. 다시 그리기 강제
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
    
    g_widgetVisible = 1;
}

// 위젯 토글 (보이기/숨기기)
void ToggleWidgetVisibility(HWND hwnd) {
    if (g_widgetVisible) {
        HideWidget(hwnd);
    } else {
        ShowWidget(hwnd);
    }
}

// 트레이 우클릭 메뉴 표시
void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    HMENU hSpeedMenu = CreatePopupMenu();
    
    float currentSpeed = GifPlayer_GetSpeedMultiplier();
    
    // 음악 위젯 보이기/숨기기
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, g_widgetVisible ? L"Hide Music Widget" : L"Show Music Widget");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    
    // GIF 관련
    AppendMenuW(hMenu, MF_STRING, ID_MENU_SHOW_GIFS, L"Show All GIFs");
    AppendMenuW(hMenu, MF_STRING, ID_MENU_HIDE_GIFS, L"Hide All GIFs");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    
    // 속도 서브메뉴
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 0.5f ? MF_CHECKED : 0), ID_MENU_SPEED_SLOW, L"0.5x (Slow)");
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 1.0f ? MF_CHECKED : 0), ID_MENU_SPEED_NORMAL, L"1.0x (Normal)");
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 1.5f ? MF_CHECKED : 0), ID_MENU_SPEED_FAST, L"1.5x (Fast)");
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 2.0f ? MF_CHECKED : 0), ID_MENU_SPEED_VFAST, L"2.0x (Very Fast)");
    AppendMenuW(hSpeedMenu, MF_STRING | (currentSpeed == 4.0f ? MF_CHECKED : 0), ID_MENU_SPEED_ULTRA, L"4.0x (Ultra)");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSpeedMenu, L"GIF Speed");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    
    // 설정 옵션
    int autoStartEnabled = Settings_IsAutoStartEnabled();
    AppendMenuW(hMenu, MF_STRING | (autoStartEnabled ? MF_CHECKED : 0), ID_MENU_AUTOSTART, L"Start with Windows");
    AppendMenuW(hMenu, MF_STRING | (g_manualClickThrough ? MF_CHECKED : 0), ID_MENU_CLICKTHROUGH, L"Click-through Mode");
    AppendMenuW(hMenu, MF_STRING | (g_autoGameMode ? MF_CHECKED : 0), ID_MENU_AUTOMODE, L"Auto Game Mode");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    
    // 종료
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// 마지막 곡 제목 (변경 감지용)
static wchar_t g_lastTitle[256] = {0};

// 윈도우 프로시저
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            // 타이머 시작
            SetTimer(hwnd, UPDATE_TIMER_ID, UPDATE_INTERVAL, NULL);
            SetTimer(hwnd, GIF_TIMER_ID, 16, NULL);  // 16ms로 빠르게 체크
            SetTimer(hwnd, SAVE_TIMER_ID, SAVE_INTERVAL, NULL);  // 5초마다 설정 저장 체크
            SetTimer(hwnd, TOPMOST_TIMER_ID, TOPMOST_INTERVAL, NULL);  // 최상위 유지
            return 0;
            
        case WM_TIMER:
            if (wParam == UPDATE_TIMER_ID) {
                // 이전 제목 저장
                wchar_t prevTitle[256];
                wcscpy_s(prevTitle, 256, g_lastTitle);
                
                // 미디어 정보 업데이트
                MediaInfo_Update(&g_mediaInfo);
                
                // 곡이 변경되면 앨범 아트 비트맵 갱신
                if (wcscmp(prevTitle, g_mediaInfo.title) != 0) {
                    wcscpy_s(g_lastTitle, 256, g_mediaInfo.title);
                    // 기존 앨범 아트 비트맵 해제
                    if (g_hAlbumArt) {
                        DeleteObject(g_hAlbumArt);
                        g_hAlbumArt = NULL;
                    }
                }
            }
            else if (wParam == GIF_TIMER_ID) {
                // 음악 재생 중일 때만 GIF 프레임 전환
                if (g_mediaInfo.isPlaying) {
                    GifPlayer_NextFrame();
                }
            }
            else if (wParam == SAVE_TIMER_ID) {
                // 위치가 변경되었으면 저장
                RECT rc;
                if (GetWindowRect(hwnd, &rc)) {
                    if (rc.left != g_lastWidgetX || rc.top != g_lastWidgetY) {
                        g_lastWidgetX = rc.left;
                        g_lastWidgetY = rc.top;
                        SaveCurrentSettings();
                    }
                }
            }
            else if (wParam == TOPMOST_TIMER_ID) {
                // 위젯이 보이는 상태에서만 최상위 유지
                if (g_widgetVisible) {
                    // 주기적으로 최상위 유지 (게임 위에 표시)
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    // GIF 창들도 최상위 유지
                    GifPlayer_BringToTop();
                }
                
                // 대기 중인 GIF 처리 (폴더 감시에서 추가된 GIF)
                GifPlayer_ProcessPendingGifs();
                
                // 자동 게임 모드: 전체 화면 앱 감지 시 클릭 투과 자동 활성화
                if (g_autoGameMode && !g_manualClickThrough) {
                    int isFullscreen = IsFullscreenAppRunning();
                    SetClickThrough(isFullscreen);
                }
            }
            // 위젯이 보이는 상태에서만 화면 다시 그리기
            if (g_widgetVisible) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_SYSCOMMAND:
            // 최소화 버튼 = 트레이로 숨기기
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                HideWidget(hwnd);
                return 0;
            }
            break;

        case WM_CLOSE:
            // 창 닫기 = 트레이로 숨기기 (종료 아님)
            HideWidget(hwnd);
            return 0;

        case WM_KEYDOWN:
            // ESC 키로 창 숨기기
            if (wParam == VK_ESCAPE) {
                HideWidget(hwnd);
                return 0;
            }
            break;

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) {
                // 트레이 우클릭 → 메뉴
                ShowTrayMenu(hwnd);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                // 트레이 더블클릭 → 토글
                ToggleWidgetVisibility(hwnd);
            }
            return 0;

        case WM_DESTROY:
            // 종료 전 설정 저장
            SaveCurrentSettings();
            RemoveTrayIcon();
            KillTimer(hwnd, UPDATE_TIMER_ID);
            KillTimer(hwnd, GIF_TIMER_ID);
            KillTimer(hwnd, SAVE_TIMER_ID);
            KillTimer(hwnd, TOPMOST_TIMER_ID);
            PostQuitMessage(0);
            return 0;

        case WM_LBUTTONDOWN: {
            // 설정 버튼 클릭 체크
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            if (IsInSettingsButton(x, y)) {
                ShowSettingsMenu(hwnd);
                return 0;
            }
            // 다른 영역은 드래그로 이동
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_MENU_SHOW_GIFS:
                    GifPlayer_ShowAll();
                    break;
                case ID_MENU_HIDE_GIFS:
                    GifPlayer_HideAll();
                    break;
                case ID_MENU_SPEED_SLOW:
                    GifPlayer_SetSpeedMultiplier(0.5f);
                    SaveCurrentSettings();
                    break;
                case ID_MENU_SPEED_NORMAL:
                    GifPlayer_SetSpeedMultiplier(1.0f);
                    SaveCurrentSettings();
                    break;
                case ID_MENU_SPEED_FAST:
                    GifPlayer_SetSpeedMultiplier(1.5f);
                    SaveCurrentSettings();
                    break;
                case ID_MENU_SPEED_VFAST:
                    GifPlayer_SetSpeedMultiplier(2.0f);
                    SaveCurrentSettings();
                    break;
                case ID_MENU_SPEED_ULTRA:
                    GifPlayer_SetSpeedMultiplier(4.0f);
                    SaveCurrentSettings();
                    break;
                case ID_MENU_AUTOSTART: {
                    int currentState = Settings_IsAutoStartEnabled();
                    Settings_SetAutoStart(!currentState);
                    break;
                }
                case ID_MENU_CLICKTHROUGH:
                    ToggleClickThrough();
                    break;
                case ID_MENU_AUTOMODE:
                    g_autoGameMode = !g_autoGameMode;
                    // 자동 모드 끄면 클릭 투과도 해제
                    if (!g_autoGameMode && !g_manualClickThrough) {
                        SetClickThrough(0);
                    }
                    break;
                case ID_MENU_EXIT:
                    // 기어 버튼 Exit = 트레이로 숨기기
                    ToggleWidgetVisibility(hwnd);
                    break;
                case ID_TRAY_SHOW:
                    // 트레이에서 Show/Hide Widget
                    ToggleWidgetVisibility(hwnd);
                    break;
                case ID_TRAY_EXIT:
                    // 트레이에서 Exit = 프로그램 종료
                    RemoveTrayIcon();
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // 더블 버퍼링용 메모리 DC
            HDC memDC = CreateCompatibleDC(hdc);
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
            
            // 배경 그리기 (다크 테마)
            HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
            FillRect(memDC, &clientRect, bgBrush);
            DeleteObject(bgBrush);
            
            // 텍스트 설정
            SetBkMode(memDC, TRANSPARENT);
            
            // 재생 상태 표시
            HFONT hOldFont = (HFONT)SelectObject(memDC, g_hFont);
            
            if (g_mediaInfo.hasMedia) {
                int artSize = g_albumArtSize;
                int textStartX = 15;
                
                // 앨범 아트 표시
                if (g_mediaInfo.hasAlbumArt && g_mediaInfo.albumArtData) {
                    // 앨범 아트 비트맵 생성 (매번 새로 만들지 않고 캠시)
                    if (g_hAlbumArt == NULL) {
                        BITMAPINFO bmi = {0};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = g_mediaInfo.albumArtWidth;
                        bmi.bmiHeader.biHeight = -g_mediaInfo.albumArtHeight;  // top-down
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;
                        
                        void* pBits = NULL;
                        g_hAlbumArt = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
                        if (g_hAlbumArt && pBits) {
                            memcpy(pBits, g_mediaInfo.albumArtData, 
                                   g_mediaInfo.albumArtWidth * g_mediaInfo.albumArtHeight * 4);
                        }
                    }
                    
                    if (g_hAlbumArt) {
                        HDC hdcArt = CreateCompatibleDC(memDC);
                        HBITMAP oldArtBmp = (HBITMAP)SelectObject(hdcArt, g_hAlbumArt);
                        
                        // 크기 조절해서 그리기
                        SetStretchBltMode(memDC, HALFTONE);
                        StretchBlt(memDC, 15, 15, artSize, artSize,
                                   hdcArt, 0, 0, g_mediaInfo.albumArtWidth, g_mediaInfo.albumArtHeight, SRCCOPY);
                        
                        SelectObject(hdcArt, oldArtBmp);
                        DeleteDC(hdcArt);
                    }
                    textStartX = 15 + artSize + 10;
                } else {
                    // 앨범 아트 없으면 왼쪽 여백만 적용
                    textStartX = 15;
                }
                
                // 제목 (흰색, 큰 폰트)
                SetTextColor(memDC, RGB(255, 255, 255));
                RECT titleRect = {textStartX, 20, WINDOW_WIDTH - 40, 45};
                DrawTextW(memDC, g_mediaInfo.title, -1, &titleRect, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE);
                
                // 아티스트 (회색, 작은 폰트)
                SelectObject(memDC, g_hFontSmall);
                SetTextColor(memDC, RGB(170, 170, 170));
                RECT artistRect = {textStartX, 48, WINDOW_WIDTH - 40, 70};
                DrawTextW(memDC, g_mediaInfo.artist, -1, &artistRect, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE);
            } else {
                // 미디어 없음
                SetTextColor(memDC, RGB(150, 150, 150));
                RECT noMediaRect = {20, 40, WINDOW_WIDTH - 20, 65};
                DrawTextW(memDC, L"No media playing...", -1, &noMediaRect, DT_CENTER);
            }
            
            // GIF는 이제 별도 창에서 표시됨
            GifPlayer_Draw(memDC);
            
            // 설정 버튼 (톱니바퀴) 그리기
            SetTextColor(memDC, RGB(150, 150, 150));
            SelectObject(memDC, g_hFont);
            RECT settingsRect = {SETTINGS_BTN_X, SETTINGS_BTN_Y, SETTINGS_BTN_X + SETTINGS_BTN_SIZE, SETTINGS_BTN_Y + SETTINGS_BTN_SIZE};
            DrawTextW(memDC, L"\u2699", -1, &settingsRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            
            SelectObject(memDC, hOldFont);
            
            // 더블 버퍼 복사
            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
            
            // 정리
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST: {
            // 먼저 설정 버튼 영역 체크 (클릭 투과 모드여도 설정 버튼은 클릭 가능)
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            ScreenToClient(hwnd, &pt);
            if (IsInSettingsButton(pt.x, pt.y)) {
                return HTCLIENT;  // 설정 버튼은 항상 클릭 가능
            }
            
            // 클릭 투과 모드면 나머지 영역은 통과
            if (g_clickThrough) {
                return HTTRANSPARENT;
            }
            
            // 나머지는 드래그로 이동
            LRESULT hit = DefWindowProc(hwnd, uMsg, wParam, lParam);
            if (hit == HTCLIENT) {
                return HTCAPTION;
            }
            return hit;
        }
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                    PWSTR pCmdLine, int nCmdShow) {
    // 설정 로드
    Settings_Load(&g_settings);
    
    // SMTC 초기화
    if (!MediaInfo_Init()) {
        MessageBoxW(NULL, L"Failed to initialize media info", L"Error", MB_OK);
        return 1;
    }
    
    // GIF 플레이어 초기화 (assets/config.txt에서 설정 로드)
    GifPlayer_Init();
    
    // 저장된 GIF 속도 적용
    if (g_settings.gifSpeedMultiplier > 0) {
        GifPlayer_SetSpeedMultiplier(g_settings.gifSpeedMultiplier);
    }
    
    // 저장된 GIF 위치 적용 (size가 0이면 위치만 적용, 크기는 원본 유지)
    for (int i = 0; i < g_settings.gifCount && i < MAX_GIFS; i++) {
        if (g_settings.gifs[i].x >= 0 && g_settings.gifs[i].y >= 0) {
            // size가 0이거나 비정상적으로 크면 위치만 적용
            int size = g_settings.gifs[i].size;
            if (size <= 0 || size > 1000) {
                size = 0;  // 크기 변경 안 함
            }
            GifPlayer_SetPosition(i, g_settings.gifs[i].x, g_settings.gifs[i].y, size);
        }
    }
    
    // 저장된 GIF Z-order 적용
    if (g_settings.gifCount > 0) {
        int zOrders[MAX_GIFS];
        for (int i = 0; i < g_settings.gifCount && i < MAX_GIFS; i++) {
            zOrders[i] = g_settings.gifs[i].zOrder;
        }
        GifPlayer_ApplyZOrder(zOrders, g_settings.gifCount);
    }

    // 폰트 생성 (큰 폰트)
    g_hFont = CreateFontW(
        20,                     // 높이
        0,                      // 너비 (0 = 자동)
        0, 0,                   // 기울기
        FW_SEMIBOLD,            // 굵기
        FALSE, FALSE, FALSE,    // 이탤릭, 밑줄, 취소선
        DEFAULT_CHARSET,        // 문자셋
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,      // 안티앨리어싱
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"             // 폰트 이름
    );
    
    // 폰트 생성 (작은 폰트)
    g_hFontSmall = CreateFontW(
        14,                     // 높이
        0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );

    // 윈도우 클래스 등록
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClassW(&wc);
    
    // 위치 계산 (저장된 위치 또는 화면 중앙)
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int posX, posY;
    
    if (g_settings.widgetX >= 0 && g_settings.widgetY >= 0) {
        // 저장된 위치 사용 (화면 범위 내인지 확인)
        posX = g_settings.widgetX;
        posY = g_settings.widgetY;
        
        // 화면 밖이면 중앙으로
        if (posX < -WINDOW_WIDTH || posX > screenWidth || 
            posY < -WINDOW_HEIGHT || posY > screenHeight) {
            posX = (screenWidth - WINDOW_WIDTH) / 2;
            posY = (screenHeight - WINDOW_HEIGHT) / 2;
        }
    } else {
        // 기본: 화면 상단 중앙
        posX = (screenWidth - WINDOW_WIDTH) / 2;
        posY = 10;  // 상단에서 10픽셀 아래
    }
    
    g_lastWidgetX = posX;
    g_lastWidgetY = posY;
    
    // 창 생성 (테두리 없음, 항상 위, Alt+Tab/작업표시줄 숨김)
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        CLASS_NAME,
        L"Music Widget",
        WS_POPUP,  // 테두리 없는 창
        posX, posY,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );
    
    if (hwnd == NULL) {
        return 0;
    }
    
    g_hwndMain = hwnd;
    
    // 트레이 아이콘 생성
    CreateTrayIcon(hwnd);
    
    // 창 투명도 설정 (선택사항: 90% 불투명)
    SetLayeredWindowAttributes(hwnd, 0, 230, LWA_ALPHA);
    
    // 폴더 감시 시작 (assets 폴더에 GIF 추가 시 자동 로드)
    GifPlayer_StartFolderWatch();
    
    // 창 보이기
    ShowWindow(hwnd, nCmdShow);
    
    // 메시지 루프
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // 정리
    GifPlayer_StopFolderWatch();  // 폴더 감시 중지
    if (g_hFont) DeleteObject(g_hFont);
    if (g_hFontSmall) DeleteObject(g_hFontSmall);
    if (g_hAlbumArt) DeleteObject(g_hAlbumArt);
    MediaInfo_FreeAlbumArt(&g_mediaInfo);
    GifPlayer_Cleanup();
    MediaInfo_Cleanup();
    
    return 0;
}
