/*
 * settings.c - Settings Manager Implementation
 */

#include "settings.h"
#include <shlobj.h>
#include <stdio.h>

// 설정 파일 경로 가져오기 (AppData\Local\MusicWidget\settings.ini)
void Settings_GetFilePath(wchar_t* path, int maxLen) {
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
        _snwprintf(path, maxLen, L"%s\\MusicWidget\\settings.ini", appData);
    } else {
        // fallback: 현재 폴더
        _snwprintf(path, maxLen, L"settings.ini");
    }
}

// 설정 디렉토리 생성
static void EnsureSettingsDir(void) {
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appData))) {
        wchar_t dir[MAX_PATH];
        _snwprintf(dir, MAX_PATH, L"%s\\MusicWidget", appData);
        CreateDirectoryW(dir, NULL);
    }
}

// 설정 로드
int Settings_Load(AppSettings* settings) {
    if (!settings) return 0;
    
    // 기본값 설정
    settings->widgetX = -1;  // -1 = 중앙
    settings->widgetY = -1;
    settings->gifCount = 0;
    settings->gifSpeedMultiplier = 1.0f;
    settings->autoStart = 0;
    
    for (int i = 0; i < MAX_GIFS; i++) {
        settings->gifs[i].x = -1;
        settings->gifs[i].y = -1;
        settings->gifs[i].size = 150;
        settings->gifs[i].zOrder = i;  // 기본값: 인덱스 순서
    }
    
    wchar_t path[MAX_PATH];
    Settings_GetFilePath(path, MAX_PATH);
    
    FILE* file = _wfopen(path, L"r");
    if (!file) {
        return 0;  // 파일 없음 - 기본값 사용
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // 위젯 위치
        if (sscanf(line, "widgetX=%d", &settings->widgetX) == 1) continue;
        if (sscanf(line, "widgetY=%d", &settings->widgetY) == 1) continue;
        
        // GIF 속도
        if (sscanf(line, "gifSpeed=%f", &settings->gifSpeedMultiplier) == 1) continue;
        
        // 자동 실행
        if (sscanf(line, "autoStart=%d", &settings->autoStart) == 1) continue;
        
        // GIF 위치 (gif0_x=100 형식)
        int gifIdx;
        int val;
        if (sscanf(line, "gif%d_x=%d", &gifIdx, &val) == 2 && gifIdx < MAX_GIFS) {
            settings->gifs[gifIdx].x = val;
            if (gifIdx >= settings->gifCount) settings->gifCount = gifIdx + 1;
            continue;
        }
        if (sscanf(line, "gif%d_y=%d", &gifIdx, &val) == 2 && gifIdx < MAX_GIFS) {
            settings->gifs[gifIdx].y = val;
            continue;
        }
        if (sscanf(line, "gif%d_size=%d", &gifIdx, &val) == 2 && gifIdx < MAX_GIFS) {
            settings->gifs[gifIdx].size = val;
            continue;
        }
        if (sscanf(line, "gif%d_z=%d", &gifIdx, &val) == 2 && gifIdx < MAX_GIFS) {
            settings->gifs[gifIdx].zOrder = val;
            continue;
        }
    }
    
    fclose(file);
    return 1;
}

// 설정 저장
int Settings_Save(const AppSettings* settings) {
    if (!settings) return 0;
    
    EnsureSettingsDir();
    
    wchar_t path[MAX_PATH];
    Settings_GetFilePath(path, MAX_PATH);
    
    FILE* file = _wfopen(path, L"w");
    if (!file) {
        return 0;
    }
    
    fprintf(file, "[MusicWidget]\n");
    fprintf(file, "widgetX=%d\n", settings->widgetX);
    fprintf(file, "widgetY=%d\n", settings->widgetY);
    fprintf(file, "gifSpeed=%f\n", settings->gifSpeedMultiplier);
    fprintf(file, "autoStart=%d\n", settings->autoStart);
    
    // GIF 위치 및 Z-order
    for (int i = 0; i < settings->gifCount && i < MAX_GIFS; i++) {
        fprintf(file, "gif%d_x=%d\n", i, settings->gifs[i].x);
        fprintf(file, "gif%d_y=%d\n", i, settings->gifs[i].y);
        fprintf(file, "gif%d_size=%d\n", i, settings->gifs[i].size);
        fprintf(file, "gif%d_z=%d\n", i, settings->gifs[i].zOrder);
    }
    
    fclose(file);
    return 1;
}

// 자동 실행 등록
int Settings_SetAutoStart(int enable) {
    HKEY hKey;
    const wchar_t* keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* valueName = L"MusicWidget";
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return 0;
    }
    
    int result = 0;
    
    if (enable) {
        // 현재 실행 파일 경로 가져오기
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        
        // 레지스트리에 등록
        if (RegSetValueExW(hKey, valueName, 0, REG_SZ, 
                           (const BYTE*)exePath, 
                           (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS) {
            result = 1;
        }
    } else {
        // 레지스트리에서 제거
        if (RegDeleteValueW(hKey, valueName) == ERROR_SUCCESS) {
            result = 1;
        }
    }
    
    RegCloseKey(hKey);
    return result;
}

// 자동 실행 등록 여부 확인
int Settings_IsAutoStartEnabled(void) {
    HKEY hKey;
    const wchar_t* keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* valueName = L"MusicWidget";
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return 0;
    }
    
    wchar_t value[MAX_PATH];
    DWORD size = sizeof(value);
    DWORD type;
    
    int result = (RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)value, &size) == ERROR_SUCCESS);
    
    RegCloseKey(hKey);
    return result;
}
