/*
 * media_info.cpp - SMTC Media Information (C++ WinRT)
 */

#include "media_info.h"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <string>
#include <vector>

// IMemoryBufferByteAccess 인터페이스 직접 정의
MIDL_INTERFACE("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")
IMemoryBufferByteAccess : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(
        BYTE **value,
        UINT32 *capacity
    ) = 0;
};

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Graphics::Imaging;

// 전역 세션 매니저
static GlobalSystemMediaTransportControlsSessionManager g_sessionManager = nullptr;
static bool g_initialized = false;
static std::wstring g_lastTitle;  // 마지막 제목 (변경 감지용)

extern "C" {

int MediaInfo_Init(void) {
    try {
        winrt::init_apartment();
        
        // 세션 매니저 가져오기
        auto asyncOp = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        g_sessionManager = asyncOp.get();
        
        g_initialized = true;
        return 1;
    }
    catch (...) {
        return 0;
    }
}

void MediaInfo_Cleanup(void) {
    g_sessionManager = nullptr;
    g_initialized = false;
    winrt::uninit_apartment();
}

int MediaInfo_Update(MediaInfo* info) {
    if (!info || !g_initialized || !g_sessionManager) {
        return 0;
    }
    
    // 초기화
    info->title[0] = L'\0';
    info->artist[0] = L'\0';
    info->isPlaying = 0;
    info->hasMedia = 0;
    
    try {
        // 현재 세션 가져오기
        auto session = g_sessionManager.GetCurrentSession();
        if (!session) {
            return 0;
        }
        
        // 미디어 정보 가져오기
        auto mediaPropsAsync = session.TryGetMediaPropertiesAsync();
        auto mediaProps = mediaPropsAsync.get();
        
        if (mediaProps) {
            // 제목
            std::wstring title = mediaProps.Title().c_str();
            if (!title.empty()) {
                wcsncpy_s(info->title, 256, title.c_str(), _TRUNCATE);
            }
            
            // 아티스트
            std::wstring artist = mediaProps.Artist().c_str();
            if (!artist.empty()) {
                wcsncpy_s(info->artist, 256, artist.c_str(), _TRUNCATE);
            }
            
            info->hasMedia = 1;
            
            // 곡이 변경되었을 때만 앨범 아트 다시 로드
            if (title != g_lastTitle) {
                g_lastTitle = title;
                
                // 기존 앨범 아트 해제
                if (info->albumArtData) {
                    free(info->albumArtData);
                    info->albumArtData = nullptr;
                    info->hasAlbumArt = 0;
                }
                
                // 앨범 아트 가져오기
                auto thumbnail = mediaProps.Thumbnail();
                if (thumbnail) {
                    try {
                        auto streamRef = thumbnail.OpenReadAsync().get();
                        if (streamRef) {
                            // 스트림에서 디코더 생성
                            auto decoder = BitmapDecoder::CreateAsync(streamRef).get();
                            if (decoder) {
                                // 소프트웨어 비트맵으로 변환
                                auto softwareBitmap = decoder.GetSoftwareBitmapAsync().get();
                                if (softwareBitmap) {
                                    // BGRA8 형식으로 변환
                                    auto convertedBitmap = SoftwareBitmap::Convert(
                                        softwareBitmap, 
                                        BitmapPixelFormat::Bgra8, 
                                        BitmapAlphaMode::Premultiplied
                                    );
                                    
                                    // 원본 비트맵 즉시 해제
                                    softwareBitmap.Close();
                                    
                                    if (convertedBitmap) {
                                        int width = convertedBitmap.PixelWidth();
                                        int height = convertedBitmap.PixelHeight();
                                        int bufferSize = width * height * 4;
                                        
                                        // 버퍼 생성 및 복사
                                        info->albumArtData = (unsigned char*)malloc(bufferSize);
                                        if (info->albumArtData) {
                                            {
                                                // 스코프로 buffer/reference 수명 제한
                                                auto buffer = convertedBitmap.LockBuffer(BitmapBufferAccessMode::Read);
                                                auto reference = buffer.CreateReference();
                                                
                                                // IMemoryBufferByteAccess로 데이터 접근
                                                auto interop = reference.as<::IMemoryBufferByteAccess>();
                                                uint8_t* dataPtr = nullptr;
                                                uint32_t capacity = 0;
                                                if (SUCCEEDED(interop->GetBuffer(&dataPtr, &capacity)) && dataPtr) {
                                                    memcpy(info->albumArtData, dataPtr, bufferSize);
                                                    info->albumArtWidth = width;
                                                    info->albumArtHeight = height;
                                                    info->hasAlbumArt = 1;
                                                } else {
                                                    free(info->albumArtData);
                                                    info->albumArtData = nullptr;
                                                }
                                                
                                                // reference, buffer는 스코프 종료 시 자동 해제
                                            }
                                        }
                                        
                                        // 변환된 비트맵 해제
                                        convertedBitmap.Close();
                                    }
                                }
                            }
                            
                            // 스트림 닫기
                            streamRef.Close();
                        }
                    }
                    catch (...) {
                        // 앨범 아트 로드 실패 시 정리
                        if (info->albumArtData) {
                            free(info->albumArtData);
                            info->albumArtData = nullptr;
                        }
                        info->hasAlbumArt = 0;
                    }
                }
            }
        }
        
        // 재생 상태
        auto playbackInfo = session.GetPlaybackInfo();
        bool isCurrentlyPlaying = false;
        if (playbackInfo) {
            auto status = playbackInfo.PlaybackStatus();
            isCurrentlyPlaying = (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
            info->isPlaying = isCurrentlyPlaying ? 1 : 0;
        }
        
        // 타임라인 정보 (재생 위치)
        info->positionSeconds = 0.0;
        info->durationSeconds = 0.0;
        auto timelineProps = session.GetTimelineProperties();
        if (timelineProps) {
            auto position = timelineProps.Position();
            auto endTime = timelineProps.EndTime();
            auto lastUpdated = timelineProps.LastUpdatedTime();
            
            double posSeconds = static_cast<double>(position.count()) / 10000000.0;
            info->durationSeconds = static_cast<double>(endTime.count()) / 10000000.0;
            
            // 재생 중이면 마지막 업데이트 이후 경과 시간을 더함
            if (isCurrentlyPlaying) {
                auto now = winrt::clock::now();
                auto elapsed = now - lastUpdated;
                double elapsedSeconds = static_cast<double>(elapsed.count()) / 10000000.0;
                posSeconds += elapsedSeconds;
            }
            
            // 범위 제한
            if (posSeconds < 0) posSeconds = 0;
            if (posSeconds > info->durationSeconds) posSeconds = info->durationSeconds;
            
            info->positionSeconds = posSeconds;
        }
        
        return 1;
    }
    catch (...) {
        return 0;
    }
}

void MediaInfo_FreeAlbumArt(MediaInfo* info) {
    if (info && info->albumArtData) {
        free(info->albumArtData);
        info->albumArtData = nullptr;
        info->hasAlbumArt = 0;
        info->albumArtWidth = 0;
        info->albumArtHeight = 0;
    }
}

} // extern "C"
