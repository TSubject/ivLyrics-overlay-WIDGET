/*
 * media_info.h - SMTC Media Information
 */

#ifndef MEDIA_INFO_H
#define MEDIA_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

// 미디어 정보 구조체
typedef struct {
    wchar_t title[256];      // 제목
    wchar_t artist[256];     // 아티스트
    int isPlaying;           // 재생중 여부 (1=재생, 0=일시정지)
    int hasMedia;            // 미디어 있음 여부
    int hasAlbumArt;         // 앨범 아트 있음 여부
    unsigned char* albumArtData;  // 앨범 아트 데이터 (RGBA)
    int albumArtWidth;       // 앨범 아트 너비
    int albumArtHeight;      // 앨범 아트 높이
    double positionSeconds;  // 현재 재생 위치 (초)
    double durationSeconds;  // 전체 길이 (초)
} MediaInfo;

// 초기화 / 정리
int MediaInfo_Init(void);
void MediaInfo_Cleanup(void);

// 현재 재생 정보 가져오기
int MediaInfo_Update(MediaInfo* info);

// 앨범 아트 메모리 해제
void MediaInfo_FreeAlbumArt(MediaInfo* info);

#ifdef __cplusplus
}
#endif

#endif // MEDIA_INFO_H
