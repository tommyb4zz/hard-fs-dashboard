/*
 * Pack and malfunction-history screens beside the SquareLine screens, plus their touch
 * navigation. They are built at runtime from application code after ui_init(), in the same
 * black/red table style and Tomorrow fonts, so a SquareLine re-export cannot remove them.
 */
#ifndef APP_DIAGNOSTICS_UI_H
#define APP_DIAGNOSTICS_UI_H
#include "lvgl/lvgl.h"

#define APP_PACK_ROW_COUNT 4U
#define APP_HISTORY_ROWS_PER_PAGE 4U
/* Pack table rows, in display order. */
#define APP_PACK_ROW_VOLTAGE 0U
#define APP_PACK_ROW_SUMMED 1U
#define APP_PACK_ROW_CURRENT 2U
#define APP_PACK_ROW_SOC 3U

extern lv_obj_t *app_ui_pack_screen;
extern lv_obj_t *app_ui_history_screen;
extern lv_obj_t *app_ui_pack_values[APP_PACK_ROW_COUNT];
extern lv_obj_t *app_ui_pack_states[APP_PACK_ROW_COUNT];
extern lv_obj_t *app_ui_history_journal;
extern lv_obj_t *app_ui_history_summary;
extern lv_obj_t *app_ui_history_status;
extern lv_obj_t *app_ui_history_rows[APP_HISTORY_ROWS_PER_PAGE];

/** Build the extra screens and buttons once after ui_init(); repeated calls do nothing. */
void app_diagnostics_ui_init(void);
/** Delete the extra screens before ui_destroy() and clear every handle. */
void app_diagnostics_ui_destroy(void);
/** Show the newest-first page of the malfunction history; skips work when nothing changed. */
void app_diagnostics_ui_refresh_history(void);
/** Return a stable name for the active screen: main, debug, pack, faults or other. */
const char *app_diagnostics_ui_screen_name(void);
#endif
