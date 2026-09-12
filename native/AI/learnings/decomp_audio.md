# Audio: the AX stack, the mixer, and the asset formats (S5)

Read this before touching `native/audio/`, `axfx_port.c`, or the audio
converters.  ADR-0013 is the design; this file is the implementation map and
the format reference.

## Layers

```
game (compiled src/)            host platform (native/)
  lbaudio_ax.c  policy/banks     platform/dvd.c   .ssm/.sem/.hps conversion
  synth.c       SFX/HPS driver   platform/ar.c    16 MB ARAM (byte offsets)
  axdriver.c    SM command clock audio/ax_hle.c   AXOut replacement + pump
  ax.h API  ──────────────────── audio/ax_mixer.c software DSP mixer
  extern/dolphin ax/{AX,AXAlloc,AXAux,AXSPB,AXVPB,AXCL}.c   (compiled verbatim)
  extern/dolphin axfx/{axfx,delay}.c + decomp/axfx/axfx_port.c
```

`AXOut.c` (DSP task + AI DMA) is the only SDK file not compiled; its
replacement keeps the exact frame order:

```
__AXSyncPBs -> __AXPrintStudio -> __AXGetCommandListAddress
  -> __AXServiceCallbackStack -> __AXProcessAux -> user callback
  -> __AXNextFrame -> ax_mixer_frame -> sink
```

The audio clock is 160 stereo samples (5 ms at 32 kHz), 200 frames/s.  It is
derived from VI: `ax_hle_pump_video_frame` pumps 10 audio frames per 3
retraces, in thirds to avoid drift.  The DSP shadow (`__AXPB`, private to
AXVPB.c) is the mixer's input; `__AXServiceVPB`'s `sync == 0` path copies the
mixer's write-back (state, `ve.currentVolume`, `currentAddress`) into the user
PB that the game polls.

## ARAM addressing: `byte * 2 + 2`

`ARAlloc` returns ARAM byte offsets from 0x4000.  The game turns every voice
address into `aram_offset * 2 + 2` (`HSD_SynthInit`, `HSD_Synth_8038B120`,
`HSD_Synth_8038ADD0`), so the mixer reads `byte = (addr - 2) / 2`.
The `+ 2` term is the game's own convention (`.ssm` records store `2` for
"byte 0"; `.hps` pages use `page * 0x20000 + i * 0x10000 + 2`).

## DSP-ADPCM (format 0)

The AX ucode decodes the SDK's **8-byte / 14-sample** DSP-ADPCM, not 9-byte /
16-sample frames (that is a different Nintendo codec).  Per frame:

```
header      = byte 0
scale       = 1 << (header & 0x0F)
coef_index  = header >> 4            (masked to 7: the PB table has 8 pairs)
coef1/2     = pb->adpcm.a[coef_index][0/1]
sample[i]   = ((nibble_s8[i] * scale) << 11) + 1024
              + coef1*hist1 + coef2*hist2) >> 11     (clamped to s16)
nibble order: sample 0 = high nibble of byte 1, then alternating
```

`hist1`/`hist2` are seeded from `pb->adpcm.yn1/yn2`; on a loop wrap they are
reloaded from `pb->adpcmLoop.loop_yn1/loop_yn2`.  Getting the frame size or
the header nibble order wrong turns music into loud noise (playable
diagnostic: spectral flatness 0.27 -> 0.007 once fixed, see gotcha G-097).

The mixer also implements PCM formats 10/25 (HPS page PCM) and ratio SRC with
a 16.16 fractional position and linear interpolation.  When the game writes a
new `currentAddress` to a live voice (stream page start, voice reuse) the
mixer restarts the decoder there: shadow `currentAddress != write_addr` is the
trigger, because `__AXServiceVPB` overwrites our write-back with any value the
game sends.

## Asset formats

### `.ssm` sample banks (`platform/ssm.c`)

```
+0x00 u32 table_size         +0x04 u32 sample_bytes
+0x08 u32 group_count        +0x0C u32 base
+0x10 groups: { u32 n; u32 rate; n * 0x40-byte entries }
+0x10+table_size samples (DSP-ADPCM)
```

Runtime entry `E` (what `HSD_Synth_80389334` reads) maps to file `group - 8`:
`E+0x08` = n (voice count), `E+0x0C` = rate (sample rate -> `x14`), `E+0x10`
= `AXPBADDR`, `E+0x20` = `AXPBADPCM`, `E+0x48` = `AXPBADPCMLOOP`.  **Every
entry field is a u16**; the loop/end/current addresses are Hi/Lo u16 pairs and
must be swapped as two u16s, never as a u32 (a u32 swap reverses Hi and Lo and
every SFX plays from a garbage address).  The converter is stateful: feed it
the DVD reads in order, each with its own destination buffer; it resumes
mid-record and bails rather than guessing when a read is missed.

The retail file's group records overlap (dest advances 8 bytes more per group
than the source); `synth.c` uses `memmove` under `PORT_PC`.

### `smash2.sem` command table (`platform/sem.c`)

Five sections `{ u32 count; count * u32 }` followed by command streams, all
big-endian; `AXDriver_8038DA70` rebases sections 1/3/4 into the loaded buffer.
Every command-stream word is `[type:8][param:24]` and `AXDriver_8038C6C0`
consumes it as a host u32, so the converter swaps each 4-byte word of every
section-3 chunk (the chunk list bounds them; each stream ends with type 14/15).

### `.hps` streamed music (`platform/hps.c`, `synth.c`)

```
+0x00 stream header: u32 magic[2], u32 rate, u32 voices,
        then per voice AXPBADDR (8 u16) + AXPBADPCM (20 u16)
+0x80 page table:  u32 x0 (page stride, 0x10000 AX units),
                   u32 x4 (per-voice data size), u32 x8 (next table, -1=end)
+0xA0 page data (x0 bytes)
```

Pages are loaded by DevCom (type 0x21 for tables, 0x22 for the header via the
internal buffer, 0x23 for page data over ARQ) and converted in the DVD
backend.  Voice `i` of page `p` plays `base + p*0x20000 + i*0x10000 + 2`; the
game advances pages from the mixer's `currentAddress` write-back.

## Aux effects and the studio

`__AXProcessAux` runs the registered aux A/B callbacks on the CPU-side buffer
of a triple buffer; the mixer fills the aux sends from `AXPBMix` and adds the
command list's aux output buffers to the main mix.  `reverb_std` is ported to C
in `native/decomp/axfx/axfx_port.c` (the only asm in that file);
`AXFXReverbHi`/`AXFXChorus` are never registered by Melee and report init
failure.  `delay.c` compiles as-is.

## Verification

- `ctest audio` (`tests/test_audio.c`): disc-free synthetic ADPCM, SRC,
  looping, state write-back, and `.ssm` conversion.
- `ctest decomp_audio` (`tests/audio_determinism.sh`): two 300-frame matches
  must render byte-identical PCM and the same FNV-1a hash.
- `melee_decomp_boot --audio-dump out.wav` writes the deterministic 32 kHz
  stereo mix and logs `[boot] audio: frames=N hash=...`.
- Quality probe (no listening needed): spectral flatness < 0.05 and positive
  stereo correlation mean music, not noise.
- ASan/UBSan: `build/native-asan` 300-frame match is clean.
