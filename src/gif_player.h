/*
 * gif_player.h - GIF Animation Player
 */

#ifndef GIF_PLAYER_H
#define GIF_PLAYER_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_GIFS 10

// GIF 항목 정보
typedef struct {
    int x;
    int y;
    int width;
    int height;
} GifEntry;

// GIF 플레이어 초기화 (assets 폴더에서 config.txt 읽기)
int GifPlayer_Init(void);

// 정리
void GifPlayer_Cleanup(void);

// 다음 프레임으로 (재생 중일 때만 호출)
void GifPlayer_NextFrame(void);

// 모든 GIF 그리기
void GifPlayer_Draw(HDC hdc);

// GIF 로드 여부
int GifPlayer_IsLoaded(void);

// 로드된 GIF 수
int GifPlayer_GetCount(void);

// 재생 상태 설정
void GifPlayer_SetPlaying(int playing);

// 모든 GIF 창 표시/숨기기
void GifPlayer_ShowAll(void);
void GifPlayer_HideAll(void);

// 모든 GIF 창 최상위로 (게임 위에 표시)
void GifPlayer_BringToTop(void);

// 클릭 투과 모드 설정 (게임용)
void GifPlayer_SetClickThrough(int enable);

// 속도 배율 설정/가져오기 (1.0 = 원본 속도, 2.0 = 2배속)
void GifPlayer_SetSpeedMultiplier(float multiplier);
float GifPlayer_GetSpeedMultiplier(void);

// GIF 위치/크기 가져오기
int GifPlayer_GetPosition(int index, int* x, int* y, int* size);

// GIF 위치/크기 설정
int GifPlayer_SetPosition(int index, int x, int y, int size);

// GIF Z-order 가져오기/설정 (순서 저장/복원용)
int GifPlayer_GetZOrder(int index);
void GifPlayer_ApplyZOrder(int* zOrderArray, int count);

// 동적 GIF 추가 (외부에서 호출 가능)
int GifPlayer_AddGif(const wchar_t* filePath);

// 폴더 감시 시작/중지
void GifPlayer_StartFolderWatch(void);
void GifPlayer_StopFolderWatch(void);

// 대기 중인 GIF 처리 (타이머에서 호출)
void GifPlayer_ProcessPendingGifs(void);

#ifdef __cplusplus
}
#endif

#endif // GIF_PLAYER_H
