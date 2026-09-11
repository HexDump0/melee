/*
 * PROBE-ONLY no-op stubs for the S0b HSD_JObjLoadJoint harness.
 *
 * The compiled HSD object system keeps its display/material/particle methods
 * reachable through class method tables, so linking HSD_JObjLoadJoint drags in
 * the GX/TEV/LObj/bytecode surface even though the bind-pose load never calls
 * it.  This file satisfies those symbols with no-ops so the S0b probe can run
 * and measure joint/matrix parity.
 *
 * DO NOT link this into a product target.  S1/S2 replace these with the real
 * platform backends (OS, GX HLE, TExp, LObj); anything here that turns out to
 * be called during load/pose must be implemented for real in
 * native/decomp/sdk_math.c or the platform layer.
 */
#include <stdlib.h>

#define STUB(name) long name(void) { return 0; }

STUB(GXBegin)
STUB(GXCallDisplayList)
STUB(GXClearVtxDesc)
STUB(GXInitTexObj)
STUB(GXInitTexObjCI)
STUB(GXInitTexObjLOD)
STUB(GXInitTlutObj)
STUB(GXLoadNrmMtxImm)
STUB(GXLoadPosMtxImm)
STUB(GXLoadTexMtxImm)
STUB(GXLoadTexObj)
STUB(GXLoadTlut)
STUB(GXSetArray)
STUB(GXSetCurrentMtx)
STUB(GXSetTexCoordGen2)
STUB(GXSetVtxAttrFmt)
STUB(GXSetVtxDesc)
STUB(HSD_ByteCodeEval)
STUB(HSD_FObjRemoveAll)
STUB(HSD_GetNbBits)
STUB(HSD_HashSearch)
STUB(HSD_Index2PosNrmMtx)
STUB(HSD_JObjDispSub)
STUB(HSD_JObjMakePositionMtx)
STUB(HSD_LObjGetCurrentByType)
STUB(HSD_LObjGetLightMaskDiffuse)
STUB(HSD_LObjGetLightVector)
STUB(_HSD_mkEnvelopeModelNodeMtx)
STUB(HSD_PerfCountEnvelopeBlending)
STUB(HSD_PerfCurrentStat)
STUB(HSD_SetMaterialColor)
STUB(HSD_SetMaterialShininess)
STUB(HSD_SetupRenderModeWithCustomPE)
STUB(HSD_SetupTevStage)
STUB(HSD_StateAssignTev)
STUB(HSD_StateInitTev)
STUB(HSD_StateRegisterTexGen)
STUB(HSD_StateSetCullMode)
STUB(HSD_TExpAlphaIn)
STUB(HSD_TExpAlphaOp)
STUB(HSD_TExpCnst)
STUB(HSD_TExpColorIn)
STUB(HSD_TExpColorOp)
STUB(HSD_TExpCompile)
STUB(HSD_TExpFreeList)
STUB(HSD_TExpFreeTevDesc)
STUB(HSD_TExpGetType)
STUB(HSD_TExpOrder)
STUB(HSD_TExpSetupTev)
STUB(HSD_TExpTev)
STUB(OSCheckHeap)

void HSD_Panic(void)
{
    abort();
}
