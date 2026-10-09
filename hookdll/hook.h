#pragma once

#include <windows.h>
#include <cstdint>

using Encode_t = int (__fastcall*)(void*, int16_t*, uint64_t, int, uint8_t*);

extern Encode_t g_orig_encode;

// ─── CORE GAIN ────────────────────────────────────────────────────────────────
extern "C" volatile float g_gain;               // mic gain multiplier (1..300)
extern "C" volatile float g_rage_gain;          // rage mode extra multiplier (1..30)
extern "C" volatile float g_gainfade_target;    // smooth fade target for g_gain
extern "C" volatile float g_gainfade_speed;     // lerp speed (0..1 per frame)
extern "C" volatile float g_stereofade_target;  // smooth fade target for stereo width
extern "C" volatile float g_stereofade_speed;   // lerp speed for stereo width

// ─── METERS (read-only, written by DSP) ───────────────────────────────────────
extern "C" volatile float g_vunits;             // VU meter (RMS 0..1)
extern "C" volatile float g_energy;             // current frame energy
extern "C" volatile int   g_packets;            // total packet count
extern "C" volatile float g_spoof_db;           // fake dB display value (-60..0)

// ─── BITRATE ──────────────────────────────────────────────────────────────────
extern "C" volatile int   g_target_kbps;

// ─── EXPLOIT DB ───────────────────────────────────────────────────────────────
extern "C" volatile bool  g_exploit_db;         // trigger NOP patch on discord_voice.node dB cap

// ─── AUDIO & ENCODING FLAGS ───────────────────────────────────────────────────
extern "C" volatile bool  g_int16_limiter;      // soft tanh limiter instead of hard clamp
extern "C" volatile bool  g_enable_clipping;    // allow hard clipping (bypass limiter)
extern "C" volatile bool  g_high_pass_filter;   // 80 Hz 1-pole HPF
extern "C" volatile bool  g_above_head;         // HRTF above-head panning preset
extern "C" volatile bool  g_in_head;            // HRTF in-head panning preset
extern "C" volatile bool  g_noise_gate_enable;  // enable noise gate
extern "C" volatile bool  g_agc_enable;         // automatic gain control
extern "C" volatile bool  g_vad_enable;         // voice activity detection
extern "C" volatile float g_noise_gate_threshold;
extern "C" volatile float g_gate_amount;        // noise gate strength (0..1)
extern "C" volatile float g_agc_amount;         // AGC target level (0..1)
extern "C" volatile float g_vad_threshold;      // VAD energy threshold

// Opus / codec params (passed to Opus encoder via maxb heuristic)
extern "C" volatile int   g_pacsize;            // packet size override (0=auto)
extern "C" volatile int   g_audio_payload;      // payload type
extern "C" volatile int   g_complexity;         // opus complexity 0..10
extern "C" volatile int   g_lsb_depth;          // opus lsb depth 8..24
extern "C" volatile int   g_encoding_method;    // 0=SILK 1=HYBRID 2=CELT
extern "C" volatile int   g_audio_mode;         // mode index
extern "C" volatile int   g_audio_application;  // 0=VOIP 1=AUDIO 2=LOWDELAY
extern "C" volatile float g_voice_ratio;        // voice/music ratio 0..1
extern "C" volatile int   g_bandwidth;          // 0=narrow 1=medium 2=wide 3=superwide 4=full

// ─── PITCH / HAAS / STEREO ────────────────────────────────────────────────────
extern "C" volatile float g_pitch_semitones;
extern "C" volatile float g_haas_delay_ms;
extern "C" volatile float g_haas_depth;
extern "C" volatile float g_stereo_width;
extern "C" volatile float g_stereo_side_gain;

// ─── ECHO ─────────────────────────────────────────────────────────────────────
extern "C" volatile float g_echo_delay_ms;
extern "C" volatile float g_echo_feedback;
extern "C" volatile float g_echo_level;

// ─── 5-BAND EQ ────────────────────────────────────────────────────────────────
extern "C" volatile float g_eq_bass;            // 80 Hz  gain dB  (-12..+12)
extern "C" volatile float g_eq_lowmid;          // 250 Hz gain dB
extern "C" volatile float g_eq_mid;             // 1 kHz  gain dB
extern "C" volatile float g_eq_highmid;         // 4 kHz  gain dB
extern "C" volatile float g_eq_treble;          // 12 kHz gain dB
extern "C" volatile bool  g_show_eq_vis;

// ─── EFFECTS ──────────────────────────────────────────────────────────────────
extern "C" volatile float g_wider;              // additional width knob (0..2)
extern "C" volatile float g_pan;                // stereo pan (-1=L, 0=center, 1=R)
extern "C" volatile float g_distortion;         // waveshaping amount (0..1)
extern "C" volatile float g_bit_crusher;        // bit depth (1..16, 16=off)
extern "C" volatile float g_delay_ms;           // general delay (ms)

// ─── REVERB ───────────────────────────────────────────────────────────────────
extern "C" volatile bool  g_reverb_enable;
extern "C" volatile float g_reverb_room_size;   // 0..1
extern "C" volatile float g_reverb_damping;     // 0..1
extern "C" volatile float g_reverb_wet;         // 0..1
extern "C" volatile float g_reverb_dry;         // 0..1
extern "C" volatile float g_reverb_width;       // 0..1
extern "C" volatile float g_reverb_decay;       // 0.1..10 seconds
extern "C" volatile float g_reverb_predelay;    // 0..200 ms

// ─── CHORUS ───────────────────────────────────────────────────────────────────
extern "C" volatile float g_chorus_strength;    // 0..1
extern "C" volatile float g_chorus_rate;        // LFO Hz (0.1..10)
extern "C" volatile float g_chorus_depth;       // delay depth ms (0..30)
extern "C" volatile float g_chorus_mix;         // wet mix (0..1)

// ─── NOISE GENERATORS ─────────────────────────────────────────────────────────
extern "C" volatile float g_white_noise;        // level 0..1
extern "C" volatile float g_pink_noise;         // level 0..1
extern "C" volatile float g_brown_noise;        // level 0..1
extern "C" volatile float g_green_noise;        // level 0..1

// ─── STEREO WIDENER (dedicated toggle) ────────────────────────────────────────
extern "C" volatile bool  g_widener_enable;
extern "C" volatile float g_widener_width;      // 0..4

// ─── COMPRESSOR ───────────────────────────────────────────────────────────────
extern "C" volatile bool  g_comp_enable;
extern "C" volatile float g_comp_threshold;     // dBFS  (-60..0)
extern "C" volatile float g_comp_ratio;         // 1..20
extern "C" volatile float g_comp_attack;        // ms  (0.1..100)
extern "C" volatile bool  g_comp_reset;         // one-shot: reset envelope state

// ─── DECODER ──────────────────────────────────────────────────────────────────
extern "C" volatile bool  g_client_detect;      // client user detection
extern "C" volatile bool  g_force_not_natty;    // force NOT NATTY flag
extern "C" volatile bool  g_force_frame_480;    // force frame size 480
extern "C" volatile bool  g_force_frame_5760;   // force frame size 5760
extern "C" volatile float g_analysis_delay;     // ms
extern "C" volatile float g_db_threshold;       // dBFS threshold for decoder

// ─── CONFIG / UI ──────────────────────────────────────────────────────────────
extern "C" volatile bool  g_use_particles;
extern "C" volatile int   g_hide_hotkey;        // VK code (default VK_INSERT)
extern "C" volatile int   g_fps_limit;          // target render FPS (15..165)
extern "C" volatile uint64_t g_runtime_ms;      // milliseconds since injection

// ─── INFLIGHT / HOOK STATE ────────────────────────────────────────────────────
extern "C" volatile LONG  g_inflight;
extern bool g_hookActive;

bool InstallHook();
void UninstallHook();
void RunGui();
