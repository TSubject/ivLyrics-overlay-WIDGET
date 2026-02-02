/*
 * gif_player.cpp - GIF Animation Player using GDI+
 * Each GIF is displayed in its own transparent, draggable window
 */

#include "gif_player.h"

#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <new>  // std::nothrow

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// GIF 창 정보 구조체
typedef struct {
    Image* pImage;
    UINT frameCount;
    UINT currentFrame;
    GUID dimensionID;
    int width;
    int height;
    HWND hwnd;
    bool isPlaying;
    UINT* frameDelays;      // 각 프레임별 딜레이 (ms)
    DWORD lastFrameTime;    // 마지막 프레임 전환 시간
    float speedMultiplier;  // 속도 배율 (1.0 = 원본)
} GifWindow;

// 전역 변수
static GdiplusStartupInput g_gdiplusStartupInput;
static ULONG_PTR g_gdiplusToken = 0;
static GifWindow g_gifs[MAX_GIFS];
static int g_gifCount = 0;
static bool g_initialized = false;
static wchar_t g_assetsPath[MAX_PATH];
static HINSTANCE g_hInstance = NULL;
static bool g_isPlaying = false;
static float g_globalSpeedMultiplier = 1.0f;  // 전역 속도 배율
static bool g_clickThroughMode = false;  // 클릭 투과 모드

const wchar_t GIF_CLASS_NAME[] = L"GifWindowClass";

#define RESIZE_BORDER 8  // 리사이즈 감지 영역 크기

// 폴더 감시 관련 변수
static HANDLE g_watchThread = NULL;
static HANDLE g_watchStopEvent = NULL;
static bool g_watchRunning = false;

// 이미 로드된 GIF 파일 목록 (중복 방지)
static wchar_t g_loadedFiles[MAX_GIFS][MAX_PATH];
static int g_loadedFileCount = 0;

// 대기 중인 GIF 파일 (UI 스레드에서 처리)
static wchar_t g_pendingGifs[MAX_GIFS][MAX_PATH];
static int g_pendingGifCount = 0;
static CRITICAL_SECTION g_pendingLock;

// 전방 선언
static void UpdateGifWindow(int index);
static int GetGifIndexFromHwnd(HWND hwnd);

// HWND로 GIF 인덱스 찾기
static int GetGifIndexFromHwnd(HWND hwnd) {
    for (int i = 0; i < g_gifCount; i++) {
        if (g_gifs[i].hwnd == hwnd) return i;
    }
    return -1;
}

// GIF 창 프로시저
static LRESULT CALLBACK GifWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_NCHITTEST: {
            // 클릭 투과 모드면 모든 마우스 이벤트 통과
            if (g_clickThroughMode) {
                return HTTRANSPARENT;
            }
            
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            ScreenToClient(hwnd, &pt);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            int w = rc.right;
            int h = rc.bottom;
            
            // 모서리 감지 (우선순위)
            if (pt.x >= w - RESIZE_BORDER && pt.y >= h - RESIZE_BORDER) return HTBOTTOMRIGHT;
            if (pt.x <= RESIZE_BORDER && pt.y >= h - RESIZE_BORDER) return HTBOTTOMLEFT;
            if (pt.x >= w - RESIZE_BORDER && pt.y <= RESIZE_BORDER) return HTTOPRIGHT;
            if (pt.x <= RESIZE_BORDER && pt.y <= RESIZE_BORDER) return HTTOPLEFT;
            
            // 가장자리 감지
            if (pt.x >= w - RESIZE_BORDER) return HTRIGHT;
            if (pt.x <= RESIZE_BORDER) return HTLEFT;
            if (pt.y >= h - RESIZE_BORDER) return HTBOTTOM;
            if (pt.y <= RESIZE_BORDER) return HTTOP;
            
            // 나머지는 드래그 이동
            return HTCAPTION;
        }
        
        case WM_SIZING: {
            // 원본 비율 유지하면서 리사이즈
            int index = GetGifIndexFromHwnd(hwnd);
            if (index >= 0 && g_gifs[index].pImage) {
                RECT* pRect = (RECT*)lParam;
                int width = pRect->right - pRect->left;
                int height = pRect->bottom - pRect->top;
                
                // 원본 비율
                int origWidth = g_gifs[index].pImage->GetWidth();
                int origHeight = g_gifs[index].pImage->GetHeight();
                float ratio = (float)origHeight / origWidth;
                
                int newWidth, newHeight;
                
                // 드래그 방향에 따라 기준 결정
                switch (wParam) {
                    case WMSZ_LEFT:
                    case WMSZ_RIGHT:
                        newWidth = width;
                        newHeight = (int)(width * ratio);
                        break;
                    case WMSZ_TOP:
                    case WMSZ_BOTTOM:
                        newHeight = height;
                        newWidth = (int)(height / ratio);
                        break;
                    default:  // 모서리
                        newWidth = width;
                        newHeight = (int)(width * ratio);
                        break;
                }
                
                // 최소 크기 제한
                if (newWidth < 30) newWidth = 30;
                if (newHeight < 30) newHeight = 30;
                
                // 위치 조정
                switch (wParam) {
                    case WMSZ_LEFT:
                    case WMSZ_TOPLEFT:
                    case WMSZ_BOTTOMLEFT:
                        pRect->left = pRect->right - newWidth;
                        break;
                    default:
                        pRect->right = pRect->left + newWidth;
                        break;
                }
                
                switch (wParam) {
                    case WMSZ_TOP:
                    case WMSZ_TOPLEFT:
                    case WMSZ_TOPRIGHT:
                        pRect->top = pRect->bottom - newHeight;
                        break;
                    default:
                        pRect->bottom = pRect->top + newHeight;
                        break;
                }
            }
            return TRUE;
        }
        
        case WM_SIZE: {
            // 창 크기 변경 시 GIF 크기도 업데이트 (비율 유지)
            int index = GetGifIndexFromHwnd(hwnd);
            if (index >= 0) {
                RECT rc;
                GetWindowRect(hwnd, &rc);
                int newWidth = rc.right - rc.left;
                int newHeight = rc.bottom - rc.top;
                
                if (newWidth > 10 && newHeight > 10) {
                    g_gifs[index].width = newWidth;
                    g_gifs[index].height = newHeight;
                    UpdateGifWindow(index);
                }
            }
            return 0;
        }
        
        case WM_MOUSEWHEEL: {
            // Shift + 마우스 휠로 크기 조절
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                int index = GetGifIndexFromHwnd(hwnd);
                if (index >= 0 && g_gifs[index].pImage) {
                    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                    int step = 10;  // 한 번에 변경되는 크기
                    
                    // 원본 이미지 크기
                    int origWidth = g_gifs[index].pImage->GetWidth();
                    int origHeight = g_gifs[index].pImage->GetHeight();
                    float ratio = (float)origHeight / origWidth;
                    
                    // 현재 너비 기준으로 크기 조절
                    int newWidth = g_gifs[index].width;
                    if (delta > 0) {
                        newWidth += step;  // 휠 위로 = 확대
                    } else {
                        newWidth -= step;  // 휠 아래로 = 축소
                    }
                    
                    // 최소/최대 크기 제한
                    if (newWidth < 30) newWidth = 30;
                    if (newWidth > 800) newWidth = 800;
                    
                    // 비율 유지하면서 높이 계산
                    int newHeight = (int)(newWidth * ratio);
                    if (newHeight < 30) newHeight = 30;
                    
                    g_gifs[index].width = newWidth;
                    g_gifs[index].height = newHeight;
                    
                    // 창 크기 변경
                    SetWindowPos(hwnd, NULL, 0, 0, newWidth, newHeight, 
                                 SWP_NOMOVE | SWP_NOZORDER);
                    UpdateGifWindow(index);
                }
                return 0;
            }
            break;
        }
        
        case WM_NCLBUTTONDOWN: {
            // 캡션 영역(드래그 영역) 클릭 시 맨 위로 올리기
            // TOPMOST 창들 사이에서 순서를 바꾸려면 HWND_TOPMOST 사용
            if (wParam == HTCAPTION || wParam == HTBOTTOMRIGHT || 
                wParam == HTBOTTOMLEFT || wParam == HTTOPRIGHT || 
                wParam == HTTOPLEFT || wParam == HTRIGHT || 
                wParam == HTLEFT || wParam == HTTOP || wParam == HTBOTTOM) {
                // 일시적으로 TOPMOST 해제 후 다시 TOPMOST로 설정하면 맨 위로 감
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, 
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            // 드래그/리사이즈를 위해 DefWindowProc 호출
            break;
        }
        
        case WM_LBUTTONDOWN: {
            // 클라이언트 영역 클릭 시에도 맨 위로 올리기
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, 
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        }
        
        case WM_RBUTTONUP: {
            // 우클릭으로 해당 GIF 창 닫기
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;
            
        case WM_DESTROY:
            return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// 레이어드 윈도우 업데이트 (투명 배경 GIF)
static void UpdateGifWindow(int index) {
    GifWindow* gif = &g_gifs[index];
    if (!gif->hwnd || !gif->pImage) return;
    
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    // 32비트 비트맵 생성 (알파 채널 포함)
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = gif->width;
    bmi.bmiHeader.biHeight = -gif->height;  // 탑다운
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* pBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
    
    // GDI+로 GIF 그리기 (최적화: 중간 품질)
    Graphics graphics(hdcMem);
    graphics.SetInterpolationMode(InterpolationModeBilinear);      // CPU 최적화
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetCompositingQuality(CompositingQualityDefault);     // CPU 최적화
    
    // 투명 배경
    graphics.Clear(Color(0, 0, 0, 0));
    
    // 현재 프레임 선택
    gif->pImage->SelectActiveFrame(&gif->dimensionID, gif->currentFrame);
    
    // GIF 그리기
    graphics.DrawImage(gif->pImage, 0, 0, gif->width, gif->height);
    
    // 레이어드 윈도우 업데이트
    POINT ptSrc = {0, 0};
    SIZE sizeWnd = {gif->width, gif->height};
    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    POINT ptPos;
    RECT rc;
    GetWindowRect(gif->hwnd, &rc);
    ptPos.x = rc.left;
    ptPos.y = rc.top;
    
    UpdateLayeredWindow(gif->hwnd, hdcScreen, &ptPos, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    
    // 정리
    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

// 실행 파일 경로 기준으로 assets 폴더 경로 구하기
static void GetAssetsPath(wchar_t* outPath, int maxLen) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    // 실행 파일명 제거하여 디렉토리 경로만 남김
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    
    // assets 폴더 경로 생성
    wsprintfW(outPath, L"%s\\assets", exePath);
}

// GIF 창 생성
static HWND CreateGifWindow(int x, int y, int width, int height, int index) {
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,  // 항상 위, 투명, 태스크바 숨김
        GIF_CLASS_NAME,
        L"GIF",
        WS_POPUP,
        x, y, width, height,
        NULL, NULL, g_hInstance, NULL
    );
    
    if (hwnd) {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, index);
        ShowWindow(hwnd, SW_SHOW);
    }
    
    return hwnd;
}

// GIF 하나 로드 (width/height가 0이면 원본 크기 사용)
static bool LoadGif(const wchar_t* filePath, int x, int y, int width, int height) {
    if (g_gifCount >= MAX_GIFS) return false;
    
    Image* pImage = Image::FromFile(filePath);
    if (pImage == NULL || pImage->GetLastStatus() != Ok) {
        if (pImage) delete pImage;
        return false;
    }
    
    GifWindow* gif = &g_gifs[g_gifCount];
    gif->pImage = pImage;
    
    // width/height가 0이면 원본 크기 사용, 최대 800px 제한
    int origW = pImage->GetWidth();
    int origH = pImage->GetHeight();
    
    if (width > 0) {
        gif->width = width;
    } else {
        gif->width = (origW > 800) ? 800 : origW;
    }
    
    if (height > 0) {
        gif->height = height;
    } else {
        // 원본 비율 유지하면서 제한 적용
        if (origW > 800) {
            float ratio = (float)origH / origW;
            gif->height = (int)(800 * ratio);
        } else {
            gif->height = origH;
        }
    }
    
    gif->currentFrame = 0;
    gif->isPlaying = true;
    gif->frameDelays = NULL;
    gif->lastFrameTime = GetTickCount();
    gif->speedMultiplier = 1.0f;
    gif->frameCount = 1;  // 기본값
    memset(&gif->dimensionID, 0, sizeof(gif->dimensionID));  // 초기화
    
    // 프레임 수 가져오기
    UINT dimensionCount = pImage->GetFrameDimensionsCount();
    if (dimensionCount > 0) {
        GUID* dimensionIDs = new GUID[dimensionCount];
        if (dimensionIDs) {
            pImage->GetFrameDimensionsList(dimensionIDs, dimensionCount);
            gif->dimensionID = dimensionIDs[0];
            gif->frameCount = pImage->GetFrameCount(&gif->dimensionID);
            delete[] dimensionIDs;
        }
    }
    
    if (gif->frameCount == 0) gif->frameCount = 1;
    
    // 프레임 딜레이 정보 읽기 (PropertyTagFrameDelay = 0x5100)
    UINT propSize = pImage->GetPropertyItemSize(PropertyTagFrameDelay);
    if (propSize > 0) {
        PropertyItem* propItem = (PropertyItem*)malloc(propSize);
        if (propItem) {
            if (pImage->GetPropertyItem(PropertyTagFrameDelay, propSize, propItem) == Ok) {
                gif->frameDelays = new (std::nothrow) UINT[gif->frameCount];
                if (gif->frameDelays) {
                    UINT* delays = (UINT*)propItem->value;
                    for (UINT i = 0; i < gif->frameCount; i++) {
                        // GIF 딜레이는 1/100초 단위, ms로 변환
                        gif->frameDelays[i] = delays[i] * 10;
                        // 0이면 기본값 100ms 사용
                        if (gif->frameDelays[i] == 0) gif->frameDelays[i] = 100;
                    }
                }
            }
            free(propItem);
        }
    }
    
    // 딜레이 정보가 없으면 기본값 사용
    if (gif->frameDelays == NULL) {
        gif->frameDelays = new (std::nothrow) UINT[gif->frameCount];
        if (gif->frameDelays) {
            for (UINT i = 0; i < gif->frameCount; i++) {
                gif->frameDelays[i] = 100;  // 기본 100ms
            }
        }
    }
    
    // 창 생성 (gif->width/height는 원본 크기로 이미 설정됨)
    gif->hwnd = CreateGifWindow(x, y, gif->width, gif->height, g_gifCount);
    
    g_gifCount++;
    
    // 초기 프레임 그리기
    UpdateGifWindow(g_gifCount - 1);
    
    return true;
}

// config.txt 파싱
static void LoadConfig(const wchar_t* assetsPath) {
    wchar_t configPath[MAX_PATH];
    wsprintfW(configPath, L"%s\\config.txt", assetsPath);
    
    FILE* fp = _wfopen(configPath, L"r");
    if (!fp) {
        // config.txt 없으면 모든 GIF 파일 기본값으로 로드
        wchar_t searchPath[MAX_PATH];
        wsprintfW(searchPath, L"%s\\*.gif", assetsPath);
        
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath, &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int xPos = screenWidth - 150;  // 우측 하단에서 시작
            int yPos = 100;
            do {
                wchar_t gifPath[MAX_PATH];
                wsprintfW(gifPath, L"%s\\%s", assetsPath, findData.cFileName);
                LoadGif(gifPath, xPos, yPos, 0, 0);  // 0,0 = 원본 크기
                yPos += 130;
            } while (FindNextFileW(hFind, &findData) && g_gifCount < MAX_GIFS);
            FindClose(hFind);
        }
        return;
    }
    
    // config.txt 읽기
    // 형식: 파일명,x,y,width,height (화면 좌표)
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        // 빈 줄이나 주석 건너뛰기
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\0') continue;
        
        char filename[256];
        int x, y, w, h;
        
        if (sscanf(line, "%[^,],%d,%d,%d,%d", filename, &x, &y, &w, &h) == 5) {
            wchar_t wFilename[256];
            MultiByteToWideChar(CP_UTF8, 0, filename, -1, wFilename, 256);
            
            wchar_t gifPath[MAX_PATH];
            wsprintfW(gifPath, L"%s\\%s", assetsPath, wFilename);
            
            LoadGif(gifPath, x, y, w, h);
        }
    }
    
    fclose(fp);
    
    // config.txt에서 유효한 GIF를 못 찾으면 자동 로드
    if (g_gifCount == 0) {
        wchar_t searchPath[MAX_PATH];
        wsprintfW(searchPath, L"%s\\*.gif", assetsPath);
        
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath, &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int xPos = screenWidth - 150;
            int yPos = 100;
            do {
                wchar_t gifPath[MAX_PATH];
                wsprintfW(gifPath, L"%s\\%s", assetsPath, findData.cFileName);
                LoadGif(gifPath, xPos, yPos, 120, 120);
                yPos += 130;
            } while (FindNextFileW(hFind, &findData) && g_gifCount < MAX_GIFS);
            FindClose(hFind);
        }
    }
}

extern "C" {

int GifPlayer_Init(void) {
    g_hInstance = GetModuleHandle(NULL);
    
    // 윈도우 클래스 등록
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = GifWindowProc;
    wc.hInstance = g_hInstance;
    wc.lpszClassName = GIF_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
    
    // GDI+ 초기화
    if (GdiplusStartup(&g_gdiplusToken, &g_gdiplusStartupInput, NULL) != Ok) {
        return 0;
    }
    
    g_initialized = true;
    g_gifCount = 0;
    
    // assets 경로 구하기
    GetAssetsPath(g_assetsPath, MAX_PATH);
    
    // 설정 파일 로드
    LoadConfig(g_assetsPath);
    
    return 1;
}

void GifPlayer_Cleanup(void) {
    for (int i = 0; i < g_gifCount; i++) {
        if (g_gifs[i].hwnd) {
            DestroyWindow(g_gifs[i].hwnd);
            g_gifs[i].hwnd = NULL;
        }
        if (g_gifs[i].pImage) {
            delete g_gifs[i].pImage;
            g_gifs[i].pImage = NULL;
        }
        if (g_gifs[i].frameDelays) {
            delete[] g_gifs[i].frameDelays;
            g_gifs[i].frameDelays = NULL;
        }
    }
    g_gifCount = 0;
    
    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
    
    UnregisterClassW(GIF_CLASS_NAME, g_hInstance);
    g_initialized = false;
}

void GifPlayer_NextFrame(void) {
    DWORD currentTime = GetTickCount();
    
    for (int i = 0; i < g_gifCount; i++) {
        GifWindow* gif = &g_gifs[i];
        if (gif->pImage && gif->frameCount > 1 && gif->hwnd && IsWindowVisible(gif->hwnd)) {
            // 현재 프레임의 딜레이 시간 계산 (속도 배율 적용)
            UINT delay = (UINT)(gif->frameDelays[gif->currentFrame] / (gif->speedMultiplier * g_globalSpeedMultiplier));
            if (delay < 10) delay = 10;  // 최소 10ms
            
            // 딜레이 시간이 지났으면 다음 프레임으로
            if (currentTime - gif->lastFrameTime >= delay) {
                gif->currentFrame = (gif->currentFrame + 1) % gif->frameCount;
                gif->pImage->SelectActiveFrame(&gif->dimensionID, gif->currentFrame);
                gif->lastFrameTime = currentTime;
                UpdateGifWindow(i);
            }
        }
    }
}

void GifPlayer_Draw(HDC hdc) {
    // 이제 각 GIF는 자체 창에서 그려지므로 이 함수는 비워둠
    // 하위 호환성을 위해 유지
    (void)hdc;
}

int GifPlayer_IsLoaded(void) {
    return (g_gifCount > 0) ? 1 : 0;
}

int GifPlayer_GetCount(void) {
    return g_gifCount;
}

void GifPlayer_SetPlaying(int playing) {
    g_isPlaying = (playing != 0);
}

void GifPlayer_ShowAll(void) {
    for (int i = 0; i < g_gifCount; i++) {
        if (g_gifs[i].hwnd) {
            ShowWindow(g_gifs[i].hwnd, SW_SHOW);
        }
    }
}

void GifPlayer_HideAll(void) {
    for (int i = 0; i < g_gifCount; i++) {
        if (g_gifs[i].hwnd) {
            ShowWindow(g_gifs[i].hwnd, SW_HIDE);
        }
    }
}

void GifPlayer_BringToTop(void) {
    // TOPMOST 속성만 유지하고 Z-order는 변경하지 않음
    // 순서를 유지하기 위해 SWP_NOZORDER 사용
    for (int i = 0; i < g_gifCount; i++) {
        if (g_gifs[i].hwnd && IsWindowVisible(g_gifs[i].hwnd)) {
            // TOPMOST 속성이 없어졌을 수 있으니 다시 설정
            // 하지만 순서는 변경하지 않음
            LONG_PTR exStyle = GetWindowLongPtr(g_gifs[i].hwnd, GWL_EXSTYLE);
            if (!(exStyle & WS_EX_TOPMOST)) {
                // TOPMOST가 해제된 경우에만 다시 설정
                SetWindowPos(g_gifs[i].hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
    }
}

void GifPlayer_SetClickThrough(int enable) {
    g_clickThroughMode = (enable != 0);
    
    for (int i = 0; i < g_gifCount; i++) {
        if (g_gifs[i].hwnd) {
            LONG_PTR exStyle = GetWindowLongPtr(g_gifs[i].hwnd, GWL_EXSTYLE);
            
            if (enable) {
                exStyle |= WS_EX_TRANSPARENT;
            } else {
                exStyle &= ~WS_EX_TRANSPARENT;
            }
            
            SetWindowLongPtr(g_gifs[i].hwnd, GWL_EXSTYLE, exStyle);
        }
    }
}

void GifPlayer_SetSpeedMultiplier(float multiplier) {
    if (multiplier < 0.1f) multiplier = 0.1f;
    if (multiplier > 10.0f) multiplier = 10.0f;
    g_globalSpeedMultiplier = multiplier;
}

float GifPlayer_GetSpeedMultiplier(void) {
    return g_globalSpeedMultiplier;
}

int GifPlayer_GetPosition(int index, int* x, int* y, int* size) {
    if (index < 0 || index >= g_gifCount || !g_gifs[index].hwnd) {
        return 0;
    }
    
    RECT rc;
    GetWindowRect(g_gifs[index].hwnd, &rc);
    
    if (x) *x = rc.left;
    if (y) *y = rc.top;
    if (size) *size = rc.right - rc.left;
    
    return 1;
}

int GifPlayer_SetPosition(int index, int x, int y, int size) {
    if (index < 0 || index >= g_gifCount || !g_gifs[index].hwnd) {
        return 0;
    }
    
    if (x >= 0 && y >= 0) {
        SetWindowPos(g_gifs[index].hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    
    // size가 0이거나 음수면 크기 변경 안 함 (원본 크기 유지)
    if (size > 0) {
        // 원본 비율 유지하면서 크기 조절
        Image* pImage = g_gifs[index].pImage;
        if (pImage) {
            int origWidth = pImage->GetWidth();
            int origHeight = pImage->GetHeight();
            
            // 비율 계산 (긴 쪽 기준)
            float ratio = (float)origHeight / origWidth;
            int newWidth, newHeight;
            
            if (origWidth >= origHeight) {
                newWidth = size;
                newHeight = (int)(size * ratio);
            } else {
                newHeight = size;
                newWidth = (int)(size / ratio);
            }
            
            g_gifs[index].width = newWidth;
            g_gifs[index].height = newHeight;
            SetWindowPos(g_gifs[index].hwnd, NULL, 0, 0, newWidth, newHeight, SWP_NOMOVE | SWP_NOZORDER);
            UpdateGifWindow(index);
        }
    }
    
    return 1;
}

// 현재 GIF의 저장된 zOrder 값 가져오기
int GifPlayer_GetZOrder(int index) {
    if (index < 0 || index >= g_gifCount) {
        return 0;
    }
    
    // 실제 윈도우 Z-order를 기반으로 계산
    // 맨 위에 있는 창일수록 높은 값
    int zOrder = 0;
    HWND hwndTarget = g_gifs[index].hwnd;
    if (!hwndTarget) return 0;
    
    // 다른 모든 GIF 창과 비교
    for (int i = 0; i < g_gifCount; i++) {
        if (i == index || !g_gifs[i].hwnd) continue;
        
        // hwndTarget이 g_gifs[i].hwnd보다 위에 있는지 확인
        HWND hwnd = hwndTarget;
        while (hwnd) {
            HWND next = GetWindow(hwnd, GW_HWNDNEXT);
            if (next == g_gifs[i].hwnd) {
                // target 아래에 other가 있음 = target이 위에 있음
                zOrder++;
                break;
            }
            hwnd = next;
        }
    }
    
    return zOrder;
}

// 저장된 Z-order 순서대로 창 정렬
void GifPlayer_ApplyZOrder(int* zOrderArray, int count) {
    if (!zOrderArray || count <= 0 || count > g_gifCount) return;
    
    // 인덱스와 zOrder를 쌍으로 만들기
    typedef struct {
        int index;
        int zOrder;
    } ZOrderPair;
    
    ZOrderPair pairs[MAX_GIFS];
    int validCount = 0;
    
    for (int i = 0; i < count && i < MAX_GIFS; i++) {
        if (g_gifs[i].hwnd) {
            pairs[validCount].index = i;
            pairs[validCount].zOrder = zOrderArray[i];
            validCount++;
        }
    }
    
    // zOrder 오름차순 정렬 (버블 정렬)
    for (int i = 0; i < validCount - 1; i++) {
        for (int j = 0; j < validCount - i - 1; j++) {
            if (pairs[j].zOrder > pairs[j + 1].zOrder) {
                ZOrderPair tmp = pairs[j];
                pairs[j] = pairs[j + 1];
                pairs[j + 1] = tmp;
            }
        }
    }
    
    // zOrder가 낮은 것부터 HWND_TOP으로 설정
    // 마지막에 호출한 것이 맨 위로 감
    for (int i = 0; i < validCount; i++) {
        int idx = pairs[i].index;
        if (g_gifs[idx].hwnd) {
            SetWindowPos(g_gifs[idx].hwnd, HWND_TOP, 0, 0, 0, 0, 
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

// 파일이 이미 로드되었는지 확인
static bool IsFileAlreadyLoaded(const wchar_t* filename) {
    for (int i = 0; i < g_loadedFileCount; i++) {
        if (_wcsicmp(g_loadedFiles[i], filename) == 0) {
            return true;
        }
    }
    return false;
}

// 로드된 파일 목록에 추가
static void AddToLoadedList(const wchar_t* filename) {
    if (g_loadedFileCount < MAX_GIFS) {
        wcscpy_s(g_loadedFiles[g_loadedFileCount], MAX_PATH, filename);
        g_loadedFileCount++;
    }
}

// 동적 GIF 추가 (외부 호출용)
int GifPlayer_AddGif(const wchar_t* filePath) {
    if (!g_initialized || g_gifCount >= MAX_GIFS) return 0;
    
    // 파일명만 추출
    const wchar_t* filename = wcsrchr(filePath, L'\\');
    if (filename) filename++; else filename = filePath;
    
    // 이미 로드되었는지 확인
    if (IsFileAlreadyLoaded(filename)) return 0;
    
    // .gif 파일인지 확인
    const wchar_t* ext = wcsrchr(filename, L'.');
    if (!ext || _wcsicmp(ext, L".gif") != 0) return 0;
    
    // 기본 위치 계산 (화면 우측, 기존 GIF 아래)
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int xPos = screenWidth - 150;
    int yPos = 100 + (g_gifCount * 130);
    
    // GIF 로드 (원본 크기 사용)
    if (LoadGif(filePath, xPos, yPos, 0, 0)) {
        AddToLoadedList(filename);
        return 1;
    }
    return 0;
}

// 폴더 감시 스레드 함수
static DWORD WINAPI FolderWatchThread(LPVOID lpParam) {
    HANDLE hDir = CreateFileW(
        g_assetsPath,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );
    
    if (hDir == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    BYTE buffer[4096];
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    HANDLE handles[2] = { overlapped.hEvent, g_watchStopEvent };
    
    while (g_watchRunning) {
        DWORD bytesReturned = 0;
        
        if (ReadDirectoryChangesW(
            hDir,
            buffer,
            sizeof(buffer),
            FALSE,  // 하위 폴더 감시 안 함
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION,
            &bytesReturned,
            &overlapped,
            NULL
        )) {
            DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            
            if (waitResult == WAIT_OBJECT_0) {
                // 파일 변경 감지됨
                if (GetOverlappedResult(hDir, &overlapped, &bytesReturned, FALSE)) {
                    FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)buffer;
                    
                    do {
                        if (fni->Action == FILE_ACTION_ADDED || 
                            fni->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                            // 새 파일 추가됨
                            wchar_t filename[MAX_PATH] = {0};
                            wcsncpy_s(filename, MAX_PATH, fni->FileName, 
                                     fni->FileNameLength / sizeof(wchar_t));
                            
                            // .gif 파일인지 확인
                            const wchar_t* ext = wcsrchr(filename, L'.');
                            if (ext && _wcsicmp(ext, L".gif") == 0) {
                                // 전체 경로 생성
                                wchar_t fullPath[MAX_PATH];
                                wsprintfW(fullPath, L"%s\\%s", g_assetsPath, filename);
                                
                                // 파일이 완전히 복사될 때까지 잠시 대기
                                Sleep(1000);
                                
                                // 대기 큐에 추가 (UI 스레드에서 처리)
                                EnterCriticalSection(&g_pendingLock);
                                if (g_pendingGifCount < MAX_GIFS) {
                                    wcscpy_s(g_pendingGifs[g_pendingGifCount], MAX_PATH, fullPath);
                                    g_pendingGifCount++;
                                }
                                LeaveCriticalSection(&g_pendingLock);
                            }
                        }
                        
                        // 다음 항목으로
                        if (fni->NextEntryOffset == 0) break;
                        fni = (FILE_NOTIFY_INFORMATION*)((BYTE*)fni + fni->NextEntryOffset);
                    } while (true);
                }
                
                ResetEvent(overlapped.hEvent);
            } else {
                // 중지 이벤트
                break;
            }
        } else {
            break;
        }
    }
    
    CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
    return 0;
}

// 폴더 감시 시작
void GifPlayer_StartFolderWatch(void) {
    if (g_watchRunning) return;
    
    // CRITICAL_SECTION 초기화
    InitializeCriticalSection(&g_pendingLock);
    g_pendingGifCount = 0;
    
    // 현재 로드된 파일 목록 초기화
    g_loadedFileCount = 0;
    
    // 현재 assets 폴더의 모든 GIF 파일을 로드 목록에 추가
    wchar_t searchPath[MAX_PATH];
    wsprintfW(searchPath, L"%s\\*.gif", g_assetsPath);
    
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            AddToLoadedList(findData.cFileName);
        } while (FindNextFileW(hFind, &findData) && g_loadedFileCount < MAX_GIFS);
        FindClose(hFind);
    }
    
    // 중지 이벤트 생성
    g_watchStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_watchRunning = true;
    
    // 감시 스레드 시작
    g_watchThread = CreateThread(NULL, 0, FolderWatchThread, NULL, 0, NULL);
}

// 폴더 감시 중지
void GifPlayer_StopFolderWatch(void) {
    if (!g_watchRunning) return;
    
    g_watchRunning = false;
    
    if (g_watchStopEvent) {
        SetEvent(g_watchStopEvent);
    }
    
    if (g_watchThread) {
        WaitForSingleObject(g_watchThread, 3000);
        CloseHandle(g_watchThread);
        g_watchThread = NULL;
    }
    
    if (g_watchStopEvent) {
        CloseHandle(g_watchStopEvent);
        g_watchStopEvent = NULL;
    }
    
    DeleteCriticalSection(&g_pendingLock);
}

// 대기 중인 GIF 처리 (UI 스레드에서 호출해야 함)
void GifPlayer_ProcessPendingGifs(void) {
    if (!g_watchRunning || g_pendingGifCount == 0) return;
    
    EnterCriticalSection(&g_pendingLock);
    
    while (g_pendingGifCount > 0) {
        g_pendingGifCount--;
        wchar_t* path = g_pendingGifs[g_pendingGifCount];
        
        // GIF 추가 시도
        GifPlayer_AddGif(path);
    }
    
    LeaveCriticalSection(&g_pendingLock);
}

} // extern "C"
