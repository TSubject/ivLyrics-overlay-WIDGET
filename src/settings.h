/*
 * settings.h - Settings Manager
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_GIFS 10

// 설정 구조체
typedef struct {
    // 메인 위젯 위치
    int widgetX;
    int widgetY;
    
    // GIF 위치 및 크기
    int gifCount;
    struct {
        int x;
        int y;
        int size;
        int zOrder;  // Z-order (0 = 맨 뒤, 높을수록 앞)
    } gifs[MAX_GIFS];
    
    // GIF 속도 배율
    float gifSpeedMultiplier;
    
    // 자동 실행 여부
    int autoStart;
} AppSettings;

// 설정 로드 (파일에서)
int Settings_Load(AppSettings* settings);

// 설정 저장 (파일로)
int Settings_Save(const AppSettings* settings);

// 자동 실행 등록/해제
int Settings_SetAutoStart(int enable);
int Settings_IsAutoStartEnabled(void);

// 설정 파일 경로 가져오기
void Settings_GetFilePath(wchar_t* path, int maxLen);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H
