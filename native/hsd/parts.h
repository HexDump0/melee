#ifndef MELEE_NATIVE_HSD_PARTS_H
#define MELEE_NATIVE_HSD_PARTS_H

#include <stddef.h>

#include "hsd/model.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Applies a fighter's static part visibility to a decoded model.
 *
 * Melee stores the initial DObj visibility in ftData<Char> (Pl<Char>.dat), in
 * the FtPartsDesc/FtPartsVis tables that HSD_PObjSetupMtx's caller
 * (ftParts_800749CC) uses.  Every DObj referenced by those tables is hidden on
 * load, then the neutral variant (variant 0 of slot 0) is shown.  This is what
 * hides the alternate face expressions in the real game before the Wait
 * animation runs.
 *
 * Returns 0 when visibility was applied, 1 when the archive has no visibility
 * data (the model stays fully visible), and -1 on a malformed archive.
 */
int parts_apply(const char *disc_image, const char *model_file,
                     HsdModel *model, int slot, int variant, char *error,
                     size_t error_size);

/* Marks every drawable object visible. */
void parts_show_all(HsdModel *model);

/* Number of hidden drawable objects. */
size_t parts_hidden_count(const HsdModel *model);

#ifdef __cplusplus
}
#endif
#endif
