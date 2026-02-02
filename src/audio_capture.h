/*
 * audio_capture.h - WASAPI Audio Capture
 */

#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SPECTRUM_BARS 16

// 스펙트럼 데이터
typedef struct {
    float bars[SPECTRUM_BARS];  // 0.0 ~ 1.0 범위
} SpectrumData;

// 초기화 / 정리
int AudioCapture_Init(void);
void AudioCapture_Cleanup(void);

// 스펙트럼 데이터 가져오기
int AudioCapture_GetSpectrum(SpectrumData* data);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_CAPTURE_H
