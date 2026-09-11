# S1 boot triage log (P-604)

Captured 2026-09-11 from the repo root with:

```sh
./build/native/melee_decomp_boot --boot-frames 10 --boot-timeout 30 \
    --boot-log /tmp/boot.log
```

The run is deterministic; this is the raw `--boot-log` output.

```
[boot] S1 boot skeleton: compiled decomp main() with stubbed OS/DVD/GX/VI
[boot] budget: frames=10 stub_limit=5000000 timeout=30s
[boot] real  gm_main (game)
[boot] real  OSInit (os)
[boot] real  VIInit (vi)
[boot] real  DVDInit (dvd)
[boot] real  PADInit (pad)
[boot] real  CARDInit (card)
[boot] real  OSInitAlarm (os)
[boot] frame 1
[boot] stub  PADRead (pad)
[boot] stub  CARDProbeEx (card)
[boot] stub  DVDConvertPathToEntrynum (dvd)
[boot] stub  GXInit (gx)
[boot] real  VIInit (vi)
[boot] stub  VIConfigure (vi)
[boot] stub  VISetBlack (vi)
[boot] stub  VIFlush (vi)
[boot] stub  GXSetCopyFilter (gx)
[boot] stub  GXSetDispCopyGamma (gx)
[boot] stub  GXSetColorUpdate (gx)
[boot] stub  GXSetAlphaUpdate (gx)
[boot] stub  GXSetZMode (gx)
[boot] stub  GXSetCopyClear (gx)
[boot] stub  GXSetCopyClamp (gx)
[boot] stub  GXSetDispCopySrc (gx)
[boot] stub  GXSetDispCopyYScale (gx)
[boot] stub  GXSetDispCopyDst (gx)
[boot] stub  GXCopyDisp (gx)
[boot] stub  GXPixModeSync (gx)
[boot] stub  GXInitLightPos (gx)
[boot] stub  GXInitLightDir (gx)
[boot] stub  GXInitLightAttn (gx)
[boot] stub  GXInitLightColor (gx)
[boot] stub  GXLoadLightObjImm (gx)
[boot] stub  GXClearVtxDesc (gx)
[boot] frame 2
[boot] real  HSD_LogInit (hsd)
[boot] stub  GXSetMisc (gx)
[boot] real  ARInit (ar)
[boot] stub  ARQInit (ar)
[boot] stub  AIInit (ax)
[boot] stub  AXDriver_8038E498 (ax)
[boot] stub  AXInit (ax)
[boot] stub  AISetDSPSampleRate (ax)
[boot] real  ARAlloc (ar)
[boot] stub  ARQPostRequest (ar)
[boot] real  ARAlloc (ar)
[boot] stub  AXRegisterCallback (ax)
[boot] stub  AISetStreamVolLeft (ax)
[boot] stub  AISetStreamVolRight (ax)
[boot] real  ARAlloc (ar)
[boot] stub  AXDriver_8038E37C (ax)
[boot] stub  HSD_AudioGetAuxHeapSize (ax)
[boot] stub  AXDriver_8038E30C (ax)
[boot] stub  PADSetSpec (pad)
[boot] real  PADInit (pad)
[boot] stub  PADSetSamplingRate (pad)
[boot] stub  OSCreateAlarm (os)
[boot] stub  OSSetPeriodicAlarm (os)
[boot] real  ARAlloc (ar)
[boot] stub  ARFree (ar)
[boot] stub  CARDProbe (card)
[boot] stub  AXDriver_8038D914 (ax)
[boot] stub  HSD_AudioSFXKeyOffAll (ax)
[boot] stub  DVDFastOpen (dvd)
[boot] stub  DVDReadAsyncPrio (dvd)
[boot] stub  DVDGetDriveStatus (dvd)
[boot] stub  AXDriver_8038E6C0 (ax)
[boot] stub  GXInvalidateVtxCache (gx)
[boot] stub  GXInvalidateTexAll (gx)
[boot] stub  GXSetPixelFmt (gx)
[boot] stub  GXSetFieldMode (gx)
[boot] stub  HSD_SisLib_803A84BC (hsd)
[boot] stub  GXSetDrawDone (gx)
[boot] stub  VISetNextFrameBuffer (vi)
[boot] frame 3
[boot] frame 4
[boot] frame 5
[boot] frame 6
[boot] frame 7
[boot] frame 8
[boot] frame 9
[boot] frame 10
[boot] STOP: frame budget reached
[boot] ------------------------------------------------
[boot] summary: frames=10 stub_calls=232 unique=57
[boot]   os    2
[boot]   gx    146
[boot]   vi    21
[boot]   dvd   14
[boot]   pad   3
[boot]   card  4
[boot]   ax    30
[boot]   ar    3
[boot]   hsd   9
[boot] first-hit order (real backends):
[boot]   real  gm_main                      (game) x1
[boot]   real  OSInit                       (os) x1
[boot]   real  VIInit                       (vi) x2
[boot]   real  DVDInit                      (dvd) x1
[boot]   real  PADInit                      (pad) x2
[boot]   real  CARDInit                     (card) x1
[boot]   real  OSInitAlarm                  (os) x1
[boot]   real  HSD_LogInit                  (hsd) x1
[boot]   real  ARInit                       (ar) x1
[boot]   real  ARAlloc                      (ar) x4
[boot] first-hit order (stubs):
[boot]     1. PADRead                      (pad) x1
[boot]     2. CARDProbeEx                  (card) x1
[boot]     3. DVDConvertPathToEntrynum     (dvd) x3
[boot]     4. GXInit                       (gx) x1
[boot]     5. VIConfigure                  (vi) x2
[boot]     6. VISetBlack                   (vi) x2
[boot]     7. VIFlush                      (vi) x9
[boot]     8. GXSetCopyFilter              (gx) x9
[boot]     9. GXSetDispCopyGamma           (gx) x9
[boot]    10. GXSetColorUpdate             (gx) x2
[boot]    11. GXSetAlphaUpdate             (gx) x2
[boot]    12. GXSetZMode                   (gx) x2
[boot]    13. GXSetCopyClear               (gx) x9
[boot]    14. GXSetCopyClamp               (gx) x9
[boot]    15. GXSetDispCopySrc             (gx) x9
[boot]    16. GXSetDispCopyYScale          (gx) x9
[boot]    17. GXSetDispCopyDst             (gx) x9
[boot]    18. GXCopyDisp                   (gx) x9
[boot]    19. GXPixModeSync                (gx) x9
[boot]    20. GXInitLightPos               (gx) x1
[boot]    21. GXInitLightDir               (gx) x1
[boot]    22. GXInitLightAttn              (gx) x1
[boot]    23. GXInitLightColor             (gx) x1
[boot]    24. GXLoadLightObjImm            (gx) x8
[boot]    25. GXClearVtxDesc               (gx) x1
[boot]    26. GXSetMisc                    (gx) x1
[boot]    27. ARQInit                      (ar) x1
[boot]    28. AIInit                       (ax) x1
[boot]    29. AXDriver_8038E498            (ax) x1
[boot]    30. AXInit                       (ax) x1
[boot]    31. AISetDSPSampleRate           (ax) x1
[boot]    32. ARQPostRequest               (ar) x1
[boot]    33. AXRegisterCallback           (ax) x1
[boot]    34. AISetStreamVolLeft           (ax) x3
[boot]    35. AISetStreamVolRight          (ax) x3
[boot]    36. AXDriver_8038E37C            (ax) x2
[boot]    37. HSD_AudioGetAuxHeapSize      (ax) x2
[boot]    38. AXDriver_8038E30C            (ax) x2
[boot]    39. PADSetSpec                   (pad) x1
[boot]    40. PADSetSamplingRate           (pad) x1
[boot]    41. OSCreateAlarm                (os) x1
[boot]    42. OSSetPeriodicAlarm           (os) x1
[boot]    43. ARFree                       (ar) x1
[boot]    44. CARDProbe                    (card) x3
[boot]    45. AXDriver_8038D914            (ax) x4
[boot]    46. HSD_AudioSFXKeyOffAll        (ax) x1
[boot]    47. DVDFastOpen                  (dvd) x1
[boot]    48. DVDReadAsyncPrio             (dvd) x1
[boot]    49. DVDGetDriveStatus            (dvd) x9
[boot]    50. AXDriver_8038E6C0            (ax) x8
[boot]    51. GXInvalidateVtxCache         (gx) x9
[boot]    52. GXInvalidateTexAll           (gx) x9
[boot]    53. GXSetPixelFmt                (gx) x9
[boot]    54. GXSetFieldMode               (gx) x9
[boot]    55. HSD_SisLib_803A84BC          (hsd) x9
[boot]    56. GXSetDrawDone                (gx) x8
[boot]    57. VISetNextFrameBuffer         (vi) x8
```
