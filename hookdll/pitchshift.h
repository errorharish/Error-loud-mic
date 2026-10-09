#pragma once
#include <cstddef>

#define MAX_FRAME_LENGTH 8192

struct PitchShiftState {
    float gInFIFO[MAX_FRAME_LENGTH];
    float gOutFIFO[MAX_FRAME_LENGTH];
    float gFFTworksp[2 * MAX_FRAME_LENGTH];
    float gLastPhase[MAX_FRAME_LENGTH / 2 + 1];
    float gSumPhase[MAX_FRAME_LENGTH / 2 + 1];
    float gOutputAccum[2 * MAX_FRAME_LENGTH];
    float gAnaFreq[MAX_FRAME_LENGTH];
    float gAnaMagn[MAX_FRAME_LENGTH];
    float gSynFreq[MAX_FRAME_LENGTH];
    float gSynMagn[MAX_FRAME_LENGTH];
    long gRover;

    PitchShiftState();
    void Reset();
};

// pitchShift: 1.0 = normal, 0.5 = 1 octave down, 2.0 = 1 octave up
// fftFrameSize: e.g. 2048 or 1024
// osamp: oversampling factor (e.g. 4 or 8)
void PitchShift(float pitchShift, long numSampsToProcess, long fftFrameSize, long osamp, float sampleRate, const float *indata, float *outdata, PitchShiftState &state);
