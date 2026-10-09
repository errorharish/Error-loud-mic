#define _USE_MATH_DEFINES
#include "hook.h"
#include "pitchshift.h"
#include "MinHook.h"
#include <cstring>
#include <cstdlib>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include <xmmintrin.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

// ══════════════════════════════════════════════════════════════════════════════
//  GLOBAL VARIABLES
// ══════════════════════════════════════════════════════════════════════════════

Encode_t g_orig_encode = nullptr;

// Core gain
extern "C" volatile float g_gain                = 10.0f;
extern "C" volatile float g_rage_gain            = 1.0f;
extern "C" volatile float g_gainfade_target      = 10.0f;
extern "C" volatile float g_gainfade_speed       = 0.0f;
extern "C" volatile float g_stereofade_target    = 1.0f;
extern "C" volatile float g_stereofade_speed     = 0.0f;

// Meters (GUI reads these)
extern "C" volatile float g_vunits               = 0.0f;
extern "C" volatile float g_energy               = 0.0f;
extern "C" volatile int   g_packets              = 0;
extern "C" volatile float g_spoof_db             = 0.0f;

// Bitrate / ExploitDB
extern "C" volatile int   g_target_kbps          = 0;
extern "C" volatile bool  g_exploit_db           = false;

// Audio flags
extern "C" volatile bool  g_int16_limiter        = true;
extern "C" volatile bool  g_enable_clipping      = false;
extern "C" volatile bool  g_high_pass_filter     = false;
extern "C" volatile bool  g_above_head           = false;
extern "C" volatile bool  g_in_head              = false;
extern "C" volatile bool  g_noise_gate_enable    = false;
extern "C" volatile bool  g_agc_enable           = false;
extern "C" volatile bool  g_vad_enable           = false;
extern "C" volatile float g_noise_gate_threshold = 0.0f;
extern "C" volatile float g_gate_amount          = 1.0f;
extern "C" volatile float g_agc_amount           = 0.3f;
extern "C" volatile float g_vad_threshold        = 0.01f;

// Codec params
extern "C" volatile int   g_pacsize              = 0;
extern "C" volatile int   g_audio_payload        = 111;
extern "C" volatile int   g_complexity           = 10;
extern "C" volatile int   g_lsb_depth            = 16;
extern "C" volatile int   g_encoding_method      = 1;
extern "C" volatile int   g_audio_mode           = 0;
extern "C" volatile int   g_audio_application    = 0;
extern "C" volatile float g_voice_ratio          = 1.0f;
extern "C" volatile int   g_bandwidth            = 4;

// Pitch / Haas / Stereo
extern "C" volatile float g_pitch_semitones      = 0.0f;
extern "C" volatile float g_haas_delay_ms        = 25.0f;
extern "C" volatile float g_haas_depth           = 0.8f;
extern "C" volatile float g_stereo_width         = 1.0f;
extern "C" volatile float g_stereo_side_gain     = 1.0f;

// Echo
extern "C" volatile float g_echo_delay_ms        = 0.0f;
extern "C" volatile float g_echo_feedback        = 0.0f;
extern "C" volatile float g_echo_level           = 1.0f;

// 5-Band EQ
extern "C" volatile float g_eq_bass              = 0.0f;
extern "C" volatile float g_eq_lowmid            = 0.0f;
extern "C" volatile float g_eq_mid               = 0.0f;
extern "C" volatile float g_eq_highmid           = 0.0f;
extern "C" volatile float g_eq_treble            = 0.0f;
extern "C" volatile bool  g_show_eq_vis          = false;

// Effects
extern "C" volatile float g_wider                = 1.0f;
extern "C" volatile float g_pan                  = 0.0f;
extern "C" volatile float g_distortion           = 0.0f;
extern "C" volatile float g_bit_crusher          = 16.0f;
extern "C" volatile float g_delay_ms             = 0.0f;

// Reverb
extern "C" volatile bool  g_reverb_enable        = false;
extern "C" volatile float g_reverb_room_size     = 0.5f;
extern "C" volatile float g_reverb_damping       = 0.5f;
extern "C" volatile float g_reverb_wet           = 0.33f;
extern "C" volatile float g_reverb_dry           = 0.4f;
extern "C" volatile float g_reverb_width         = 1.0f;
extern "C" volatile float g_reverb_decay         = 2.0f;
extern "C" volatile float g_reverb_predelay      = 20.0f;

// Chorus
extern "C" volatile float g_chorus_strength      = 0.5f;
extern "C" volatile float g_chorus_rate          = 1.0f;
extern "C" volatile float g_chorus_depth         = 5.0f;
extern "C" volatile float g_chorus_mix           = 0.0f;

// Noise generators
extern "C" volatile float g_white_noise          = 0.0f;
extern "C" volatile float g_pink_noise           = 0.0f;
extern "C" volatile float g_brown_noise          = 0.0f;
extern "C" volatile float g_green_noise          = 0.0f;

// Stereo Widener (dedicated)
extern "C" volatile bool  g_widener_enable       = false;
extern "C" volatile float g_widener_width        = 1.5f;

// Compressor
extern "C" volatile bool  g_comp_enable          = false;
extern "C" volatile float g_comp_threshold       = -20.0f;
extern "C" volatile float g_comp_ratio           = 4.0f;
extern "C" volatile float g_comp_attack          = 5.0f;
extern "C" volatile bool  g_comp_reset           = false;

// Decoder
extern "C" volatile bool  g_client_detect        = false;
extern "C" volatile bool  g_force_not_natty      = false;
extern "C" volatile bool  g_force_frame_480      = false;
extern "C" volatile bool  g_force_frame_5760     = false;
extern "C" volatile float g_analysis_delay       = 0.0f;
extern "C" volatile float g_db_threshold         = -60.0f;

// Config / UI
extern "C" volatile bool     g_use_particles     = true;
extern "C" volatile int      g_hide_hotkey       = VK_INSERT;
extern "C" volatile int      g_fps_limit         = 60;
extern "C" volatile uint64_t g_runtime_ms        = 0;

// Inflight / hook state
extern "C" volatile LONG  g_inflight             = 0;
bool g_hookActive = false;

// ══════════════════════════════════════════════════════════════════════════════
//  INTERNAL DSP
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// ─── MATH ────────────────────────────────────────────────────────────────────

inline float AntiDenormal(float v) {
    return (fabsf(v) < 1.0e-15f) ? 0.0f : v;
}

// Rational tanh approximation — fast soft limiter
inline float TanhApprox(float x) {
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Soft limiter keeping ±32767 scale
inline float SoftLimit(float x) {
    const float S = 1.0f / 32768.0f;
    return TanhApprox(x * S) * 32767.0f;
}

inline int16_t ClampI16(int32_t v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

inline int16_t OutputSample(float v) {
    if (g_int16_limiter && !g_enable_clipping) return ClampI16((int32_t)SoftLimit(v));
    return ClampI16((int32_t)v);
}

// ─── HOOK SCAN ───────────────────────────────────────────────────────────────

constexpr uintptr_t kEncodeRva = 0x6A8F10;
const uint8_t kPrologue[16] = {
    0x56,0x57,0x48,0x83,0xEC,0x28,0xBF,0xFF,
    0xFF,0xFF,0xFF,0x49,0x81,0xF8,0x80,0x16
};

bool BytesEq(uintptr_t a, const uint8_t* p, size_t n) {
    __try { return memcmp((void*)a, p, n) == 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t Scan(HMODULE m, const uint8_t* p, size_t n) {
    __try {
        auto* dos  = (IMAGE_DOS_HEADER*)m;
        auto* nt   = (IMAGE_NT_HEADERS64*)((uint8_t*)m + dos->e_lfanew);
        uint8_t* base = (uint8_t*)m;
        size_t sz = nt->OptionalHeader.SizeOfImage;
        uintptr_t hit = 0;
        for (size_t i = 0; i + n <= sz; ++i) {
            if (base[i] == p[0] && memcmp(base+i, p, n)==0) {
                if (hit) return 0;
                hit = (uintptr_t)(base+i);
            }
        }
        return hit;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t Resolve(HMODULE m) {
    uintptr_t a = (uintptr_t)m + kEncodeRva;
    if (BytesEq(a, kPrologue, sizeof kPrologue)) return a;
    return Scan(m, kPrologue, sizeof kPrologue);
}

// ─── EXPLOIT DB ──────────────────────────────────────────────────────────────

static bool s_exploitDone = false;
static void TryExploitDB() {
    if (s_exploitDone) return;
    HMODULE voice = GetModuleHandleW(L"discord_voice.node");
    if (!voice) return;
    auto* dos  = (IMAGE_DOS_HEADER*)voice;
    auto* nt   = (IMAGE_NT_HEADERS64*)((uint8_t*)voice + dos->e_lfanew);
    uint8_t* base = (uint8_t*)voice;
    size_t sz = nt->OptionalHeader.SizeOfImage;
    for (size_t i = 0; i + 10 <= sz; ++i) {
        if (base[i]==0x0F && base[i+1]==0x2F &&
            (base[i+7]==0x73 || base[i+7]==0x77)) {
            DWORD old=0;
            VirtualProtect(base+i+7, 2, PAGE_EXECUTE_READWRITE, &old);
            base[i+7] = 0x90;
            base[i+8] = 0x90;
            VirtualProtect(base+i+7, 2, old, &old);
            FlushInstructionCache(GetCurrentProcess(), base+i+7, 2);
            s_exploitDone = true;
            return;
        }
    }
}

// ─── DELAY BUFFERS ───────────────────────────────────────────────────────────

static constexpr size_t K_BUF = 192001; // ~4s @ 48kHz

// Haas — separate circular buffer, stereo
static float s_haasL[K_BUF] = {};
static float s_haasR[K_BUF] = {};
static size_t s_haasWr = 0;

// Echo — separate circular buffer, stereo
static float s_echoL[K_BUF] = {};
static float s_echoR[K_BUF] = {};
static size_t s_echoWr = 0;

// General delay — separate buffer, stereo
static float s_delL[K_BUF] = {};
static float s_delR[K_BUF] = {};
static size_t s_delWr = 0;

// ─── HPF STATE ───────────────────────────────────────────────────────────────

// 1-pole HPF: y[n] = α*(y[n-1] + x[n] - x[n-1])
// α = RC/(RC + 1/fs) ≈ 0.9978 for fc=80Hz, fs=48000
static constexpr float K_HPF_ALPHA = 0.9978f;
static float s_hpfPrevInL = 0.0f, s_hpfPrevInR = 0.0f;
static float s_hpfOutL    = 0.0f, s_hpfOutR    = 0.0f;

// ─── BIQUAD EQ ───────────────────────────────────────────────────────────────

struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    double z1L=0,z2L=0,z1R=0,z2R=0;
    float prevGain = -9999.0f;

    void CalcPeak(float freq, float gainDB, float Q, float sr) {
        if (fabsf(gainDB) < 0.01f) {
            b0=1;b1=0;b2=0;a1=0;a2=0; prevGain=gainDB; return;
        }
        double A  = pow(10.0, gainDB/40.0);
        double w0 = 2.0*M_PI*freq/sr;
        double cosw = cos(w0), sinw = sin(w0);
        double alpha = sinw / (2.0*Q);
        double a0  = 1.0 + alpha/A;
        b0 = (1.0+alpha*A)/a0;
        b1 = (-2.0*cosw)/a0;
        b2 = (1.0-alpha*A)/a0;
        a1 = (-2.0*cosw)/a0;
        a2 = (1.0-alpha/A)/a0;
        prevGain = gainDB;
    }

    inline float ProcL(float x) {
        double y = b0*x + z1L;
        z1L = b1*x - a1*y + z2L;
        z2L = b2*x - a2*y;
        return AntiDenormal((float)y);
    }
    inline float ProcR(float x) {
        double y = b0*x + z1R;
        z1R = b1*x - a1*y + z2R;
        z2R = b2*x - a2*y;
        return AntiDenormal((float)y);
    }
};

static Biquad s_eqBand[5]; // bass, lowmid, mid, highmid, treble
static const float K_EQ_FREQ[5] = {80.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f};

static void RebuildEQ(float sr) {
    volatile float gains[5] = { g_eq_bass, g_eq_lowmid, g_eq_mid, g_eq_highmid, g_eq_treble };
    for (int i=0;i<5;++i) s_eqBand[i].CalcPeak(K_EQ_FREQ[i], (float)gains[i], 1.0f, sr);
}

static inline bool EQDirty() {
    volatile float gains[5] = { g_eq_bass, g_eq_lowmid, g_eq_mid, g_eq_highmid, g_eq_treble };
    for (int i=0;i<5;++i) if (fabsf((float)gains[i] - s_eqBand[i].prevGain) > 0.01f) return true;
    return false;
}

// ─── SCHROEDER REVERB ────────────────────────────────────────────────────────

struct CombF {
    static constexpr size_t SZ = 24001;
    float buf[SZ] = {};
    size_t pos = 0, delay = 1557;
    float  fb = 0.84f, damp = 0.4f, fstore = 0.0f;

    float Proc(float x) {
        float out = buf[pos];
        fstore = out*(1.0f-damp) + fstore*damp;
        AntiDenormal(fstore);
        buf[pos] = x + fstore*fb;
        if (++pos >= delay) pos = 0;
        return out;
    }
};

struct AlpF {
    static constexpr size_t SZ = 2048;
    float buf[SZ] = {};
    size_t pos = 0, delay = 225;
    float fb = 0.5f;

    float Proc(float x) {
        float b  = buf[pos];
        float out = -x + b;
        buf[pos] = x + b*fb;
        if (++pos >= delay) pos = 0;
        return AntiDenormal(out);
    }
};

static constexpr size_t K_COMB_DELAY[4] = {1557,1617,1491,1422};
static constexpr size_t K_COMB_SPREAD   = 23;
static constexpr size_t K_ALP_DELAY[2]  = {225,556};

struct Reverb {
    CombF combL[4], combR[4];
    AlpF  alpL[2],  alpR[2];

    float pdBuf[48000] = {};
    size_t pdWr = 0;
    float lastRoom = -1, lastDamp = -1;

    void Rebuild(float room, float damp) {
        for (int i=0;i<4;++i) {
            size_t d = (size_t)(K_COMB_DELAY[i] * (0.68f + room*0.32f));
            if (d >= CombF::SZ) d = CombF::SZ-1;
            combL[i].delay = d;
            combR[i].delay = d + K_COMB_SPREAD;
            if (combR[i].delay >= CombF::SZ) combR[i].delay = CombF::SZ-1;
            float f = 0.82f + room*0.16f;
            if (f > 0.98f) f = 0.98f;
            combL[i].fb = combR[i].fb = f;
            combL[i].damp = combR[i].damp = damp * 0.45f;
        }
        for (int i=0;i<2;++i) {
            alpL[i].delay = K_ALP_DELAY[i];
            alpR[i].delay = K_ALP_DELAY[i] + K_COMB_SPREAD/2;
            if (alpR[i].delay >= AlpF::SZ) alpR[i].delay = AlpF::SZ-1;
        }
        lastRoom = room; lastDamp = damp;
    }

    void Proc(float iL, float iR, float& oL, float& oR,
              float room, float damp, float wet, float dry, float width, float pdMs) {
        if (room != lastRoom || damp != lastDamp) Rebuild(room, damp);
        size_t pd = (size_t)(pdMs * 48.0f);
        if (pd >= 48000) pd = 47999;
        pdBuf[pdWr] = (iL+iR)*0.5f;
        float mono = pdBuf[(pdWr+48000-pd)%48000];
        pdWr = (pdWr+1)%48000;

        float sL=0,sR=0;
        for (int i=0;i<4;++i) { sL+=combL[i].Proc(mono); sR+=combR[i].Proc(mono); }
        for (int i=0;i<2;++i) { sL=alpL[i].Proc(sL); sR=alpR[i].Proc(sR); }

        float w1 = wet*(width*0.5f+0.5f), w2 = wet*(0.5f-width*0.5f);
        oL = sL*w1 + sR*w2 + iL*dry;
        oR = sR*w1 + sL*w2 + iR*dry;
    }
};
static Reverb s_reverb;

// ─── COMPRESSOR ──────────────────────────────────────────────────────────────

struct Compressor {
    float env = 0.0f, gain = 1.0f;

    float Proc(float x, float threshDB, float ratio, float attackMs, float sr) {
        float lvl = fabsf(x) / 32768.0f;
        float tc  = expf(-1.0f/(attackMs*0.001f*sr));
        env = (lvl > env) ? lvl : env*tc + lvl*(1.0f-tc);
        env = AntiDenormal(env);
        if (env < 1.0e-7f) return x;
        float edB = 20.0f*log10f(env);
        float g = 1.0f;
        if (edB > threshDB) {
            float excess = edB - threshDB;
            g = powf(10.0f, (excess/ratio - excess)/20.0f);
        }
        gain = gain*0.9995f + g*0.0005f;
        return x * gain;
    }
    void Reset() { env=0.0f; gain=1.0f; }
};
static Compressor s_compL, s_compR;

// ─── CHORUS ──────────────────────────────────────────────────────────────────

struct Chorus {
    static constexpr size_t SZ = 96001;
    float bufL[SZ]={}, bufR[SZ]={};
    size_t wr = 0;
    float phase = 0.0f;

    void Proc(float& L, float& R, float rate, float depMs, float mix, float sr) {
        bufL[wr] = L; bufR[wr] = R;
        float lfo  = sinf(phase) * 0.5f + 0.5f; // 0..1
        phase += (2.0f*(float)M_PI*rate)/sr;
        if (phase > 2.0f*(float)M_PI) phase -= 2.0f*(float)M_PI;

        float ds   = depMs * 0.001f * sr * lfo;
        size_t di  = (size_t)ds;
        float  fr  = ds - (float)di;
        size_t r0  = (wr + SZ - di)     % SZ;
        size_t r1  = (wr + SZ - di - 1) % SZ;

        float wL = bufL[r0]*(1.0f-fr) + bufL[r1]*fr;
        float wR = bufR[r0]*(1.0f-fr) + bufR[r1]*fr;
        L = L*(1.0f-mix) + wL*mix;
        R = R*(1.0f-mix) + wR*mix;
        wr = (wr+1)%SZ;
    }
};
static Chorus s_chorus;

// ─── NOISE ───────────────────────────────────────────────────────────────────

struct PinkNoise {
    float b[7]={};
    float Next() {
        float w = ((float)rand()/RAND_MAX)*2.0f-1.0f;
        b[0]=0.99886f*b[0]+w*0.0555179f;
        b[1]=0.99332f*b[1]+w*0.0750759f;
        b[2]=0.96900f*b[2]+w*0.1538520f;
        b[3]=0.86650f*b[3]+w*0.3104856f;
        b[4]=0.55000f*b[4]+w*0.5329522f;
        b[5]=-0.7616f*b[5]-w*0.0168980f;
        float p = b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+w*0.5362f;
        b[6] = w*0.115926f;
        return p * 0.11f;
    }
};
static PinkNoise s_pink;
static float s_brown = 0.0f;

static inline float WhiteN()  { return ((float)rand()/RAND_MAX)*2.0f-1.0f; }
static inline float BrownN()  { float w=WhiteN(); s_brown=(s_brown+0.02f*w)/1.02f; return s_brown*3.5f; }
static inline float GreenN()  {
    // Simple approximation via white filtered through one-pole at ~1kHz  
    static float s=0.0f;
    float w = WhiteN();
    s = s*0.88f + w*0.12f;
    return s * 2.0f;
}

// ─── AGC ─────────────────────────────────────────────────────────────────────

static float s_agcEnv = 0.0f;

// ─── PITCH ───────────────────────────────────────────────────────────────────

static float s_pitchInL[MAX_FRAME_LENGTH]={}, s_pitchOutL[MAX_FRAME_LENGTH]={};
static float s_pitchInR[MAX_FRAME_LENGTH]={}, s_pitchOutR[MAX_FRAME_LENGTH]={};
static PitchShiftState s_pitchL, s_pitchR;
static bool  s_pitchWas = false;

// ─── RUNTIME ─────────────────────────────────────────────────────────────────

static LARGE_INTEGER s_startQPC, s_qpcFreq;

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
//  HOOKED ENCODE
// ══════════════════════════════════════════════════════════════════════════════

int __fastcall Hooked_Encode(void* inst, int16_t* pcm, uint64_t spc,
                             int maxb, uint8_t* out)
{
    InterlockedIncrement(&g_inflight);
    _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ+DAZ: flush denormals

    // Runtime timer
    {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        g_runtime_ms = (uint64_t)((now.QuadPart - s_startQPC.QuadPart)
                                  * 1000ULL / s_qpcFreq.QuadPart);
    }

    if (g_exploit_db) TryExploitDB();

    // Channel count from encoder instance
    uint64_t ch = 1;
    __try {
        if (inst) { int c = *(int*)((uint8_t*)inst+8); if (c==1||c==2) ch=(uint64_t)c; }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ch=1; }

    constexpr float SR = 48000.0f;

    // ── Bitrate → targetMaxb ────────────────────────────────────────────────
    int targetMaxb = maxb;
    {
        int kbps = g_target_kbps;
        if (kbps > 0) {
            float ft = (spc > 0) ? ((float)spc / SR) : 0.020f;
            int bytes = (int)((kbps*1000.0f/8.0f)*ft);
            if (bytes < 10)   bytes = 10;
            if (bytes > maxb) bytes = maxb;
            targetMaxb = bytes;
        }
    }

    // ── EQ rebuild if changed ───────────────────────────────────────────────
    if (EQDirty()) RebuildEQ(SR);

    // ── Compressor reset ────────────────────────────────────────────────────
    if (g_comp_reset) { s_compL.Reset(); s_compR.Reset(); g_comp_reset = false; }

    // ── Smooth GainFade / StereioFade ───────────────────────────────────────
    if (g_gainfade_speed > 0.0001f) {
        float d = (float)g_gainfade_target - (float)g_gain;
        if (fabsf(d) < 0.01f) g_gain = g_gainfade_target;
        else g_gain = (float)g_gain + d * (float)g_gainfade_speed;
    }
    if (g_stereofade_speed > 0.0001f) {
        float d = (float)g_stereofade_target - (float)g_stereo_width;
        if (fabsf(d) < 0.001f) g_stereo_width = g_stereofade_target;
        else g_stereo_width = (float)g_stereo_width + d * (float)g_stereofade_speed;
    }

    if (!pcm) {
        InterlockedDecrement(&g_inflight);
        int r=0;
        __try { r = g_orig_encode(inst,pcm,spc,targetMaxb,out); }
        __except(EXCEPTION_EXECUTE_HANDLER) { r=-1; }
        return r;
    }

    // ── Compute RMS energy for VU meter ─────────────────────────────────────
    {
        float rms = 0.0f;
        uint64_t n = spc * ch;
        for (uint64_t i=0;i<n;++i) { float s=pcm[i]/32768.0f; rms+=s*s; }
        g_vunits = (n>0) ? sqrtf(rms/(float)n) : 0.0f;
        g_energy = (float)g_vunits;
    }

    // ── Noise gate / VAD ────────────────────────────────────────────────────
    bool muted = false;
    if (g_noise_gate_enable && (float)g_vunits < (float)g_noise_gate_threshold * 0.1f) muted = true;
    if (!muted && g_vad_enable && (float)g_vunits < (float)g_vad_threshold)            muted = true;

    if (muted) {
        uint64_t n = spc * ch;
        for (uint64_t i=0;i<n;++i) pcm[i]=0;
        s_hpfPrevInL = s_hpfPrevInR = s_hpfOutL = s_hpfOutR = 0.0f;
    } else {

        // ── Pitch shift ─────────────────────────────────────────────────────
        float pst = (float)g_pitch_semitones;
        if (fabsf(pst) > 0.05f && spc <= MAX_FRAME_LENGTH) {
            float pf = powf(2.0f, pst/12.0f);
            for (uint64_t f=0;f<spc;++f) {
                s_pitchInL[f] = (float)pcm[f*ch];
                s_pitchInR[f] = (ch>1) ? (float)pcm[f*ch+1] : s_pitchInL[f];
            }
            PitchShift(pf,(long)spc,1024,4,SR,s_pitchInL,s_pitchOutL,s_pitchL);
            if (ch>1) PitchShift(pf,(long)spc,1024,4,SR,s_pitchInR,s_pitchOutR,s_pitchR);
            for (uint64_t f=0;f<spc;++f) {
                pcm[f*ch]   = ClampI16((int32_t)s_pitchOutL[f]);
                if (ch>1) pcm[f*ch+1] = ClampI16((int32_t)s_pitchOutR[f]);
            }
            s_pitchWas = true;
        } else if (s_pitchWas) {
            s_pitchL.Reset(); s_pitchR.Reset();
            s_pitchWas = false;
        }

        // ── HPF ─────────────────────────────────────────────────────────────
        if (g_high_pass_filter) {
            for (uint64_t f=0;f<spc;++f) {
                float xL = (float)pcm[f*ch];
                float xR = (ch>1) ? (float)pcm[f*ch+1] : xL;
                float yL = K_HPF_ALPHA*(s_hpfOutL + xL - s_hpfPrevInL);
                float yR = K_HPF_ALPHA*(s_hpfOutR + xR - s_hpfPrevInR);
                s_hpfOutL = yL; s_hpfOutR = yR;
                s_hpfPrevInL = xL; s_hpfPrevInR = xR;
                pcm[f*ch] = ClampI16((int32_t)yL);
                if (ch>1) pcm[f*ch+1] = ClampI16((int32_t)yR);
            }
        }

        // ── Capture local params (avoid volatile re-read each sample) ────────
        const float gain      = (float)g_gain * ((float)g_rage_gain > 1.0f ? (float)g_rage_gain : 1.0f);
        const float haasMs    = (float)g_haas_delay_ms;
        const float haasDepth = (float)g_haas_depth;
        const float stereoW   = (float)g_stereo_width;
        const float sideGain  = (float)g_stereo_side_gain;
        const float echoMs    = (float)g_echo_delay_ms;
        const float echoFb    = (float)g_echo_feedback;
        const float echoLvl   = (float)g_echo_level;
        const float delayMs   = (float)g_delay_ms;
        const float distAmt   = (float)g_distortion;
        const float bitCrush  = (float)g_bit_crusher;
        const float panVal    = (float)g_pan;
        const float wideVal   = (float)g_wider;
        const bool  doReverb  = (bool)g_reverb_enable;
        const bool  doComp    = (bool)g_comp_enable;
        const bool  doWidener = (bool)g_widener_enable;
        const float widW      = (float)g_widener_width;
        const bool  doChorus  = ((float)g_chorus_mix > 0.001f);

        // Delay sizes in samples
        const size_t haasSmp  = (size_t)(haasMs  / 1000.0f * SR);
        const size_t echoSmp  = (size_t)(echoMs  / 1000.0f * SR);
        const size_t genSmp   = (size_t)(delayMs / 1000.0f * SR);

        // Noise levels
        const float wn = (float)g_white_noise;
        const float pn = (float)g_pink_noise;
        const float bn = (float)g_brown_noise;
        const float gn = (float)g_green_noise;
        const bool  doNoise = (wn+pn+bn+gn) > 0.0001f;

        // Pre-compute read indices
        size_t haasRd = (s_haasWr + K_BUF - haasSmp) % K_BUF;
        size_t echoRd = (s_echoWr + K_BUF - echoSmp) % K_BUF;
        size_t genRd  = (s_delWr  + K_BUF - genSmp)  % K_BUF;

        // ════════════════════════════════════════════════════════════════════
        //  PER-SAMPLE LOOP
        // ════════════════════════════════════════════════════════════════════
        for (uint64_t frame = 0; frame < spc; ++frame) {

            // Read PCM as floats
            float inL = (float)pcm[frame*ch];
            float inR = (ch>1) ? (float)pcm[frame*ch+1] : inL;

            // ── 1. GAIN ─────────────────────────────────────────────────────
            inL *= gain;
            inR *= gain;

            // ── 2. HAAS STEREO WIDENER ──────────────────────────────────────
            // Store pre-haas signal (post-gain) into haas ring buffer
            s_haasL[s_haasWr] = inL;
            s_haasR[s_haasWr] = inR;

            if (haasSmp > 0 && haasDepth > 0.001f) {
                float dL = s_haasL[haasRd]; // delayed L
                float dR = s_haasR[haasRd]; // delayed R

                if (ch > 1) {
                    // ┌──────────────────────────────────────────────────────┐
                    // │ True Haas stereo widening:                           │
                    // │   Left  channel += delayed Right × depth             │
                    // │   Right channel += delayed Left  × depth             │
                    // │ This creates a psychoacoustic wide-stereo image.     │
                    // └──────────────────────────────────────────────────────┘
                    inL += dR * haasDepth;
                    inR += dL * haasDepth;
                } else {
                    // Mono: add a delayed copy (creates thickness/echo-width)
                    inL += dL * haasDepth * 0.5f;
                }
            }

            // ── 3. ECHO ─────────────────────────────────────────────────────
            {
                float rawL = inL, rawR = inR; // pre-echo for feedback
                if (echoSmp > 0 && echoLvl > 0.001f) {
                    float eL = s_echoL[echoRd];
                    float eR = s_echoR[echoRd];
                    inL += eL * echoLvl;
                    inR += eR * echoLvl;

                    if (echoFb > 0.001f) {
                        float fbL = rawL + eL*echoFb;
                        float fbR = rawR + eR*echoFb;
                        // Hard clamp echo feedback to prevent runaway
                        if (fbL >  32767.0f) fbL =  32767.0f;
                        if (fbL < -32768.0f) fbL = -32768.0f;
                        if (fbR >  32767.0f) fbR =  32767.0f;
                        if (fbR < -32768.0f) fbR = -32768.0f;
                        s_echoL[s_echoWr] = fbL;
                        s_echoR[s_echoWr] = fbR;
                    } else {
                        s_echoL[s_echoWr] = rawL;
                        s_echoR[s_echoWr] = rawR;
                    }
                } else {
                    s_echoL[s_echoWr] = rawL;
                    s_echoR[s_echoWr] = rawR;
                }
            }

            // ── 4. GENERAL DELAY ────────────────────────────────────────────
            s_delL[s_delWr] = inL;
            s_delR[s_delWr] = inR;
            if (genSmp > 0) {
                inL = (inL + s_delL[genRd]) * 0.5f;
                inR = (inR + s_delR[genRd]) * 0.5f;
            }

            // ── 5. MID/SIDE STEREO WIDTH ────────────────────────────────────
            if (stereoW != 1.0f || sideGain != 1.0f) {
                float mid  = (inL+inR)*0.5f;
                float side = (inL-inR)*0.5f * (stereoW*sideGain);
                inL = mid+side;
                inR = mid-side;
            }

            // ── 6. DEDICATED STEREO WIDENER ─────────────────────────────────
            if (doWidener && fabsf(widW-1.0f) > 0.001f) {
                float mid  = (inL+inR)*0.5f;
                float side = (inL-inR)*0.5f * widW;
                inL = mid+side;
                inR = mid-side;
            }

            // ── 7. WIDER knob ────────────────────────────────────────────────
            if (fabsf(wideVal-1.0f) > 0.001f) {
                float mid  = (inL+inR)*0.5f;
                float side = (inL-inR)*0.5f * wideVal;
                inL = mid+side;
                inR = mid-side;
            }

            // ── 8. PAN ──────────────────────────────────────────────────────
            if (fabsf(panVal) > 0.001f) {
                float panL = (panVal<=0.0f) ? 1.0f : 1.0f-panVal;
                float panR = (panVal>=0.0f) ? 1.0f : 1.0f+panVal;
                inL *= panL; inR *= panR;
            }

            // ── 9. ABOVE/IN HEAD ────────────────────────────────────────────
            if (g_above_head) {
                float mid  = (inL+inR)*0.5f;
                float side = (inL-inR)*0.5f;
                mid *= 0.65f; side *= 1.6f;
                inL = mid+side; inR = mid-side;
            }
            if (g_in_head) {
                float mono = (inL+inR)*0.5f;
                inL = mono; inR = mono;
            }

            // ── 10. NOISE INJECTION ─────────────────────────────────────────
            if (doNoise) {
                float noise = WhiteN()*wn + s_pink.Next()*pn*8.0f
                            + BrownN()*bn*12.0f + GreenN()*gn*10.0f;
                noise *= 32768.0f;
                inL += noise; inR += noise;
            }

            // ── 11. 5-BAND EQ ───────────────────────────────────────────────
            {
                const float SC = 1.0f/32768.0f;
                float nL = inL*SC, nR = inR*SC;
                for (int b=0;b<5;++b) { nL=s_eqBand[b].ProcL(nL); nR=s_eqBand[b].ProcR(nR); }
                inL = nL/SC; inR = nR/SC;
            }

            // ── 12. DISTORTION ──────────────────────────────────────────────
            if (distAmt > 0.001f) {
                float drive = 1.0f + distAmt*40.0f;
                const float SC = 1.0f/32768.0f;
                inL = TanhApprox(inL*SC*drive)/SC;
                inR = TanhApprox(inR*SC*drive)/SC;
            }

            // ── 13. BIT CRUSHER ─────────────────────────────────────────────
            if (bitCrush < 15.9f) {
                float levels = powf(2.0f, bitCrush);
                float sc = 32768.0f / levels;
                inL = floorf(inL/sc)*sc;
                inR = floorf(inR/sc)*sc;
            }

            // ── 14. CHORUS ──────────────────────────────────────────────────
            if (doChorus) {
                s_chorus.Proc(inL, inR,
                    (float)g_chorus_rate, (float)g_chorus_depth,
                    (float)g_chorus_mix, SR);
            }

            // ── 15. AGC ─────────────────────────────────────────────────────
            if (g_agc_enable) {
                float lvl = fabsf(inL) > fabsf(inR) ? fabsf(inL) : fabsf(inR);
                s_agcEnv = s_agcEnv*0.9999f + lvl*0.0001f;
                float target = (float)g_agc_amount * 32768.0f;
                if (s_agcEnv > 1.0f) {
                    float g = target / s_agcEnv;
                    if (g > 20.0f) g = 20.0f;
                    inL *= g; inR *= g;
                }
            }

            // ── 16. COMPRESSOR ──────────────────────────────────────────────
            if (doComp) {
                inL = s_compL.Proc(inL,(float)g_comp_threshold,(float)g_comp_ratio,(float)g_comp_attack,SR);
                inR = s_compR.Proc(inR,(float)g_comp_threshold,(float)g_comp_ratio,(float)g_comp_attack,SR);
            }

            // ── ADVANCE RING BUFFER INDICES ─────────────────────────────────
            s_haasWr = (s_haasWr+1) % K_BUF;
            s_echoWr = (s_echoWr+1) % K_BUF;
            s_delWr  = (s_delWr +1) % K_BUF;
            haasRd   = (haasRd  +1) % K_BUF;
            echoRd   = (echoRd  +1) % K_BUF;
            genRd    = (genRd   +1) % K_BUF;

            // ── WRITE OUTPUT ─────────────────────────────────────────────────
            if (ch == 1) {
                pcm[frame] = OutputSample(inL);
            } else {
                pcm[frame*2]   = OutputSample(inL);
                pcm[frame*2+1] = OutputSample(inR);
            }
        }

        // ── REVERB (whole-frame post-process) ─────────────────────────────
        if (doReverb) {
            for (uint64_t f=0;f<spc;++f) {
                float rL = (float)pcm[f*ch]   / 32768.0f;
                float rR = (ch>1) ? (float)pcm[f*ch+1] / 32768.0f : rL;
                float oL, oR;
                s_reverb.Proc(rL, rR, oL, oR,
                    (float)g_reverb_room_size, (float)g_reverb_damping,
                    (float)g_reverb_wet,       (float)g_reverb_dry,
                    (float)g_reverb_width,     (float)g_reverb_predelay);
                oL *= 32768.0f; oR *= 32768.0f;
                pcm[f*ch]   = OutputSample(oL);
                if (ch>1) pcm[f*ch+1] = OutputSample(oR);
            }
        }
    }

    g_packets++;

    int r = 0;
    __try { r = g_orig_encode(inst, pcm, spc, targetMaxb, out); }
    __except(EXCEPTION_EXECUTE_HANDLER) { r = -1; }

    InterlockedDecrement(&g_inflight);
    return r;
}

// ══════════════════════════════════════════════════════════════════════════════
//  INSTALL / UNINSTALL
// ══════════════════════════════════════════════════════════════════════════════

bool InstallHook() {
    QueryPerformanceFrequency(&s_qpcFreq);
    QueryPerformanceCounter(&s_startQPC);
    srand(GetTickCount());

    HMODULE voice = nullptr;
    for (int i=0; i<120 && !voice; ++i) {
        voice = GetModuleHandleW(L"discord_voice.node");
        if (!voice) Sleep(500);
    }
    if (!voice) return false;

    uintptr_t addr = Resolve(voice);
    if (!addr) return false;

    if (MH_Initialize() != MH_OK) return false;
    if (MH_CreateHook((LPVOID)addr, (LPVOID)&Hooked_Encode,
                      (LPVOID*)&g_orig_encode) != MH_OK) {
        MH_Uninitialize(); return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_Uninitialize(); return false;
    }
    g_hookActive = true;
    return true;
}

void UninstallHook() {
    if (!g_hookActive) return;
    while (g_inflight > 0) Sleep(1);
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_hookActive = false;
}
