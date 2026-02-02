/*
 * audio_capture.cpp - WASAPI Loopback Audio Capture
 */

#include "audio_capture.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <math.h>

#pragma comment(lib, "ole32.lib")

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000
#define SAMPLE_BUFFER_SIZE 2048
#define PI 3.14159265358979323846

// 전역 변수
static IMMDeviceEnumerator* g_pEnumerator = NULL;
static IMMDevice* g_pDevice = NULL;
static IAudioClient* g_pAudioClient = NULL;
static IAudioCaptureClient* g_pCaptureClient = NULL;
static WAVEFORMATEX* g_pwfx = NULL;
static bool g_initialized = false;

// 샘플 버퍼
static float g_sampleBuffer[SAMPLE_BUFFER_SIZE];
static int g_sampleIndex = 0;

// FFT 결과
static float g_fftOutput[SAMPLE_BUFFER_SIZE / 2];

// 간단한 DFT (FFT 대신 사용, 성능보다 간단함 우선)
static void SimpleDFT(float* input, float* output, int n) {
    int numBins = n / 2;
    for (int k = 0; k < numBins; k++) {
        float real = 0.0f;
        float imag = 0.0f;
        for (int t = 0; t < n; t++) {
            float angle = 2.0f * (float)PI * k * t / n;
            real += input[t] * cosf(angle);
            imag -= input[t] * sinf(angle);
        }
        output[k] = sqrtf(real * real + imag * imag) / n;
    }
}

// 주파수 대역별로 그룹화
static void GroupIntoBars(float* fftData, int fftSize, SpectrumData* spectrum) {
    int binsPerBar = (fftSize / 2) / SPECTRUM_BARS;
    if (binsPerBar < 1) binsPerBar = 1;
    
    for (int i = 0; i < SPECTRUM_BARS; i++) {
        float sum = 0.0f;
        int start = i * binsPerBar;
        int end = start + binsPerBar;
        if (end > fftSize / 2) end = fftSize / 2;
        
        for (int j = start; j < end; j++) {
            sum += fftData[j];
        }
        
        float avg = sum / binsPerBar;
        
        // 로그 스케일 적용 및 정규화 (원래 민감도)
        float db = 20.0f * log10f(avg + 0.0001f);
        float normalized = (db + 60.0f) / 60.0f;  // -60dB ~ 0dB -> 0.0 ~ 1.0
        
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        
        // 스무딩 (이전 값과 혼합)
        spectrum->bars[i] = spectrum->bars[i] * 0.7f + normalized * 0.3f;
    }
}

extern "C" {

int AudioCapture_Init(void) {
    HRESULT hr;
    
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return 0;
    }
    
    // 디바이스 열거자 생성
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&g_pEnumerator
    );
    if (FAILED(hr)) return 0;
    
    // 기본 출력 디바이스 가져오기
    hr = g_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &g_pDevice);
    if (FAILED(hr)) return 0;
    
    // 오디오 클라이언트 생성
    hr = g_pDevice->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&g_pAudioClient
    );
    if (FAILED(hr)) return 0;
    
    // 믹스 포맷 가져오기
    hr = g_pAudioClient->GetMixFormat(&g_pwfx);
    if (FAILED(hr)) return 0;
    
    // 루프백 모드로 초기화
    hr = g_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        REFTIMES_PER_SEC,
        0,
        g_pwfx,
        NULL
    );
    if (FAILED(hr)) return 0;
    
    // 캡처 클라이언트 가져오기
    hr = g_pAudioClient->GetService(
        __uuidof(IAudioCaptureClient), (void**)&g_pCaptureClient
    );
    if (FAILED(hr)) return 0;
    
    // 캡처 시작
    hr = g_pAudioClient->Start();
    if (FAILED(hr)) return 0;
    
    // 버퍼 초기화
    memset(g_sampleBuffer, 0, sizeof(g_sampleBuffer));
    memset(g_fftOutput, 0, sizeof(g_fftOutput));
    g_sampleIndex = 0;
    
    g_initialized = true;
    return 1;
}

void AudioCapture_Cleanup(void) {
    if (g_pAudioClient) {
        g_pAudioClient->Stop();
    }
    
    if (g_pCaptureClient) {
        g_pCaptureClient->Release();
        g_pCaptureClient = NULL;
    }
    if (g_pAudioClient) {
        g_pAudioClient->Release();
        g_pAudioClient = NULL;
    }
    if (g_pDevice) {
        g_pDevice->Release();
        g_pDevice = NULL;
    }
    if (g_pEnumerator) {
        g_pEnumerator->Release();
        g_pEnumerator = NULL;
    }
    if (g_pwfx) {
        CoTaskMemFree(g_pwfx);
        g_pwfx = NULL;
    }
    
    g_initialized = false;
}

int AudioCapture_GetSpectrum(SpectrumData* data) {
    if (!data || !g_initialized || !g_pCaptureClient) {
        return 0;
    }
    
    UINT32 packetLength = 0;
    HRESULT hr;
    
    hr = g_pCaptureClient->GetNextPacketSize(&packetLength);
    if (FAILED(hr)) return 0;
    
    while (packetLength != 0) {
        BYTE* pData;
        UINT32 numFramesAvailable;
        DWORD flags;
        
        hr = g_pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
        if (FAILED(hr)) break;
        
        // 샘플 데이터를 버퍼에 복사
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData) {
            float* floatData = (float*)pData;
            int channels = g_pwfx->nChannels;
            
            for (UINT32 i = 0; i < numFramesAvailable && g_sampleIndex < SAMPLE_BUFFER_SIZE; i++) {
                // 스테레오를 모노로 변환
                float sample = 0.0f;
                for (int ch = 0; ch < channels; ch++) {
                    sample += floatData[i * channels + ch];
                }
                sample /= channels;
                
                g_sampleBuffer[g_sampleIndex++] = sample;
            }
        }
        
        hr = g_pCaptureClient->ReleaseBuffer(numFramesAvailable);
        if (FAILED(hr)) break;
        
        hr = g_pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) break;
    }
    
    // 버퍼가 가득 찼으면 FFT 수행
    if (g_sampleIndex >= SAMPLE_BUFFER_SIZE) {
        SimpleDFT(g_sampleBuffer, g_fftOutput, SAMPLE_BUFFER_SIZE);
        GroupIntoBars(g_fftOutput, SAMPLE_BUFFER_SIZE, data);
        g_sampleIndex = 0;
    }
    
    return 1;
}

} // extern "C"
