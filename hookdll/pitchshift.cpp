#include "pitchshift.h"
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PitchShiftState::PitchShiftState() {
    Reset();
}

void PitchShiftState::Reset() {
    memset(gInFIFO, 0, sizeof(gInFIFO));
    memset(gOutFIFO, 0, sizeof(gOutFIFO));
    memset(gFFTworksp, 0, sizeof(gFFTworksp));
    memset(gLastPhase, 0, sizeof(gLastPhase));
    memset(gSumPhase, 0, sizeof(gSumPhase));
    memset(gOutputAccum, 0, sizeof(gOutputAccum));
    memset(gAnaFreq, 0, sizeof(gAnaFreq));
    memset(gAnaMagn, 0, sizeof(gAnaMagn));
    memset(gSynFreq, 0, sizeof(gSynFreq));
    memset(gSynMagn, 0, sizeof(gSynMagn));
    gRover = 0;
}

static void smbFft(float *fftBuffer, long fftFrameSize, long sign) {
    float wr, wi, arg, *p1, *p2, temp;
    float tr, ti, ur, ui, *p1r, *p1i, *p2r, *p2i;
    long i, bitm, j, le, le1, k;

    for (i = 2; i < 2 * fftFrameSize - 2; i += 2) {
        for (bitm = 2, j = 0; bitm < 2 * fftFrameSize; bitm <<= 1) {
            if (i & bitm) j++;
            j <<= 1;
        }
        if (i < j) {
            p1 = fftBuffer + i; p2 = fftBuffer + j;
            temp = *p1; *(p1++) = *p2; *(p2++) = temp;
            temp = *p1; *p1 = *p2; *p2 = temp;
        }
    }
    for (k = 0, le = 2; k < (long)(log((double)fftFrameSize) / log(2.0) + 0.5); k++) {
        le <<= 1;
        le1 = le >> 1;
        ui = 0.0f;
        ur = 1.0f;
        arg = (float)(M_PI / (le1 >> 1));
        wi = (float)sin(arg * sign);
        wr = (float)cos(arg);
        for (j = 0; j < le1; j += 2) {
            p1r = fftBuffer + j; p1i = p1r + 1;
            p2r = p1r + le1; p2i = p2r + 1;
            for (i = j; i < 2 * fftFrameSize; i += le) {
                tr = *p2r * ur - *p2i * ui;
                ti = *p2r * ui + *p2i * ur;
                *p2r = *p1r - tr; *p2i = *p1i - ti;
                *p1r += tr; *p1i += ti;
                p1r += le; p1i += le;
                p2r += le; p2i += le;
            }
            tr = ur * wr - ui * wi;
            ui = ur * wi + ui * wr;
            ur = tr;
        }
    }
}

void PitchShift(float pitchShift, long numSampsToProcess, long fftFrameSize, long osamp, float sampleRate, const float *indata, float *outdata, PitchShiftState &state) {
    double magn, phase, tmp, window, real, imag;
    double freqPerBin, expct;
    long i, k, qpd, index, inFifoLatency, stepSize, fftFrameSize2;

    fftFrameSize2 = fftFrameSize / 2;
    stepSize = fftFrameSize / osamp;
    freqPerBin = sampleRate / (double)fftFrameSize;
    expct = 2.0 * M_PI * (double)stepSize / (double)fftFrameSize;
    inFifoLatency = fftFrameSize - stepSize;
    if (state.gRover == 0) state.gRover = inFifoLatency;

    for (i = 0; i < numSampsToProcess; i++) {
        state.gInFIFO[state.gRover] = indata[i];
        outdata[i] = state.gOutFIFO[state.gRover - inFifoLatency];
        state.gRover++;

        if (state.gRover >= fftFrameSize) {
            state.gRover = inFifoLatency;

            for (k = 0; k < fftFrameSize; k++) {
                window = -0.5 * cos(2.0 * M_PI * (double)k / (double)fftFrameSize) + 0.5;
                state.gFFTworksp[2 * k] = (float)(state.gInFIFO[k] * window);
                state.gFFTworksp[2 * k + 1] = 0.0f;
            }

            smbFft(state.gFFTworksp, fftFrameSize, -1);

            for (k = 0; k <= fftFrameSize2; k++) {
                real = state.gFFTworksp[2 * k];
                imag = state.gFFTworksp[2 * k + 1];

                magn = 2.0 * sqrt(real * real + imag * imag);
                phase = atan2(imag, real);

                tmp = phase - state.gLastPhase[k];
                state.gLastPhase[k] = (float)phase;

                tmp -= (double)k * expct;

                qpd = (long)(tmp / M_PI);
                if (qpd >= 0) qpd += (qpd & 1);
                else qpd -= (qpd & 1);
                tmp -= M_PI * (double)qpd;

                tmp = osamp * tmp / (2.0 * M_PI);
                tmp = (double)k * freqPerBin + tmp * freqPerBin;

                state.gAnaMagn[k] = (float)magn;
                state.gAnaFreq[k] = (float)tmp;
            }

            memset(state.gSynMagn, 0, fftFrameSize * sizeof(float));
            memset(state.gSynFreq, 0, fftFrameSize * sizeof(float));
            for (k = 0; k <= fftFrameSize2; k++) {
                index = (long)(k * pitchShift);
                if (index <= fftFrameSize2) {
                    state.gSynMagn[index] += state.gAnaMagn[k];
                    state.gSynFreq[index] = state.gAnaFreq[k] * pitchShift;
                }
            }

            for (k = 0; k <= fftFrameSize2; k++) {
                magn = state.gSynMagn[k];
                tmp = state.gSynFreq[k];

                tmp -= (double)k * freqPerBin;
                tmp /= freqPerBin;
                tmp = 2.0 * M_PI * tmp / osamp;
                tmp += (double)k * expct;

                state.gSumPhase[k] += (float)tmp;
                phase = state.gSumPhase[k];

                state.gFFTworksp[2 * k] = (float)(magn * cos(phase));
                state.gFFTworksp[2 * k + 1] = (float)(magn * sin(phase));
            }

            for (k = fftFrameSize + 2; k < 2 * fftFrameSize; k++) state.gFFTworksp[k] = 0.0f;

            smbFft(state.gFFTworksp, fftFrameSize, 1);

            for (k = 0; k < fftFrameSize; k++) {
                window = -0.5 * cos(2.0 * M_PI * (double)k / (double)fftFrameSize) + 0.5;
                state.gOutputAccum[k] += (float)(2.0 * window * state.gFFTworksp[2 * k] / (fftFrameSize2 * osamp));
            }

            for (k = 0; k < stepSize; k++) state.gOutFIFO[k] = state.gOutputAccum[k] * 1.35f;

            memmove(state.gOutputAccum, state.gOutputAccum + stepSize, fftFrameSize * sizeof(float));
            for (k = 0; k < inFifoLatency; k++) state.gInFIFO[k] = state.gInFIFO[k + stepSize];
        }
    }
}
