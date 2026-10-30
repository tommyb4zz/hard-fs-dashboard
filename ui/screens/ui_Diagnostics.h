/* Additional diagnostic views use the existing black/red table and Tomorrow fonts. */
#ifndef UI_DIAGNOSTICS_H
#define UI_DIAGNOSTICS_H
#include "lvgl/lvgl.h"
extern lv_obj_t *ui_DetailScreen;
extern lv_obj_t *ui_FaultScreen;
extern lv_obj_t *ui_DetailValues[4];
extern lv_obj_t *ui_DetailStates[4];
extern lv_obj_t *ui_FaultSummary;
extern lv_obj_t *ui_FaultRows[4];
/** Draw the repository's red four-row diagnostic grid; columns are fixed layout. */
void ui_diagnostics_grid(lv_obj_t *screen, const char *const headers[3], const char *const rows[4]);
/** Create a bounded, styled diagnostic text label. */
lv_obj_t *ui_diagnostics_label(lv_obj_t *screen, const char *text, int x, int y, int width);
/** Initialize detail/history screens and touch navigation; call once per ui_init. */
void ui_diagnostics_init(void);
/** Tear down additional screens and clear their handles. */
void ui_diagnostics_destroy(void);
/** Refresh four newest-first events on the selected history page. */
void ui_fault_history_refresh(void);
#endif
