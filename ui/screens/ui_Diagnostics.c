/* Live detail and immutable fault-history pages; layout follows the generated debug table. */
#include "ui_Diagnostics.h"
#include "../ui.h"
#include "app_fault_history.h"
#include <stdio.h>
lv_obj_t *ui_DetailScreen, *ui_FaultScreen;
lv_obj_t *ui_DetailValues[4], *ui_DetailStates[4];
lv_obj_t *ui_FaultSummary, *ui_FaultRows[4];
static uint32_t history_page;

/** Create plain bounded text in the existing diagnostic font/color system. */
lv_obj_t *ui_diagnostics_label(lv_obj_t *screen, const char *text, int x, int y, int width) {
  lv_obj_t *label = lv_label_create(screen);
  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, width);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, &ui_font_TomorrowBoldItalic18, 0);
  return label;
}
/** Build cells at the same 10..470 x 12..258 grid coordinates as DebugFrame. */
void ui_diagnostics_grid(lv_obj_t *screen,
                         const char *const headers[3],
                         const char *const rows[4]) {
  const int x[] = {10, 166, 268, 370, 470};
  for (unsigned row = 0; row < 5; ++row) {
    for (unsigned col = 0; col < 4; ++col) {
      if (headers[1][0] == '\0' && col == 2)
        continue;
      unsigned end = headers[1][0] == '\0' && col == 1 ? 3 : col + 1;
      lv_obj_t *cell = lv_obj_create(screen);
      lv_obj_remove_style_all(cell);
      lv_obj_set_pos(cell, x[col], row == 0 ? 12 : 50 + (int)(row - 1) * 52);
      lv_obj_set_size(cell, x[end] - x[col], row == 0 ? 38 : 52);
      lv_obj_set_style_border_color(cell, lv_color_hex(0xA00000), 0);
      lv_obj_set_style_border_width(cell, 1, 0);
      lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    if (row != 0)
      ui_diagnostics_label(screen, rows[row - 1], 20, 65 + (int)(row - 1) * 52, 144);
  }
  for (unsigned col = 0; col < 3; ++col)
    ui_diagnostics_label(screen, headers[col], x[col + 1] + 12, 22, x[col + 2] - x[col + 1] - 12);
}
/** Route touch navigation through explicit buttons rather than recreating live screen trees. */
static void navigate(lv_event_t *event) {
  lv_obj_t *target = (lv_obj_t *)lv_event_get_user_data(event);
  if (target == ui_FaultScreen)
    ui_fault_history_refresh();
  lv_disp_load_scr(target);
}
/** Add a text button using the spare first header cell or the history page footer. */
static void
nav_button(lv_obj_t *screen, const char *text, int x, int y, int width, lv_obj_t *target) {
  lv_obj_t *button = lv_obj_create(screen);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, 38);
  lv_obj_set_style_bg_color(button, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0xA00000), 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_t *label = ui_diagnostics_label(button, "", 0, 0, width - 4);
  lv_obj_set_style_text_font(label, &ui_font_TomorrowBoldItalic12, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  lv_label_set_text(label, text);
  lv_obj_add_event_cb(button, navigate, LV_EVENT_CLICKED, target);
}
/** Page through retained events, clamping page changes at both ends. */
static void page_history(lv_event_t *event) {
  uint32_t count = app_fault_history_count();
  if (lv_event_get_user_data(event) != NULL) {
    if (history_page > 0)
      history_page--;
  } else if ((history_page + 1U) * 4U < count)
    history_page++;
  ui_fault_history_refresh();
}
/** Make a black fixed-size root using the repository's display dimensions. */
static lv_obj_t *new_screen(void) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  return screen;
}
/** Create all additional objects once; actual values are filled by the runtime binding. */
void ui_diagnostics_init(void) {
  static const char *const headers[] = {"VALUE", "", "STATE"};
  static const char *const rows[] = {"Pack V", "Sum V", "SOC %", "Logs"};
  ui_DetailScreen = new_screen();
  ui_diagnostics_grid(ui_DetailScreen, headers, rows);
  for (unsigned i = 0; i < 4; ++i) {
    ui_DetailValues[i] = ui_diagnostics_label(ui_DetailScreen, "--", 176, 66 + (int)i * 52, 185);
    ui_DetailStates[i] = ui_diagnostics_label(ui_DetailScreen, "--", 378, 66 + (int)i * 52, 88);
    lv_obj_set_style_text_font(ui_DetailStates[i], &ui_font_TomorrowBoldItalic12, 0);
  }
  ui_FaultScreen = new_screen();
  ui_FaultSummary = ui_diagnostics_label(ui_FaultScreen, "", 14, 55, 452);
  lv_obj_set_style_text_font(ui_FaultSummary, &ui_font_TomorrowBoldItalic12, 0);
  for (unsigned i = 0; i < 4; ++i) {
    ui_FaultRows[i] = ui_diagnostics_label(ui_FaultScreen, "", 14, 80 + (int)i * 38, 452);
    lv_obj_set_style_text_font(ui_FaultRows[i], &ui_font_TomorrowBoldItalic12, 0);
  }
  nav_button(ui_DebugScreen, "PACK", 10, 12, 76, ui_DetailScreen);
  nav_button(ui_DebugScreen, "FAULTS", 86, 12, 80, ui_FaultScreen);
  nav_button(ui_DetailScreen, "CELLS", 10, 12, 76, ui_DebugScreen);
  nav_button(ui_DetailScreen, "FAULTS", 86, 12, 80, ui_FaultScreen);
  nav_button(ui_FaultScreen, "CELLS", 10, 12, 76, ui_DebugScreen);
  nav_button(ui_FaultScreen, "PACK", 86, 12, 80, ui_DetailScreen);
  nav_button(ui_FaultScreen, "MAIN", 166, 12, 76, ui_MainScreen);
  nav_button(ui_FaultScreen, "NEWER", 10, 228, 100, ui_FaultScreen);
  lv_obj_t *newer = lv_obj_get_child(ui_FaultScreen, -1);
  lv_obj_remove_event_cb(newer, navigate);
  lv_obj_add_event_cb(newer, page_history, LV_EVENT_CLICKED, ui_FaultScreen);
  nav_button(ui_FaultScreen, "OLDER", 370, 228, 100, ui_FaultScreen);
  lv_obj_t *older = lv_obj_get_child(ui_FaultScreen, -1);
  lv_obj_remove_event_cb(older, navigate);
  lv_obj_add_event_cb(older, page_history, LV_EVENT_CLICKED, NULL);
  lv_obj_update_layout(ui_DebugScreen);
  lv_obj_update_layout(ui_DetailScreen);
  lv_obj_update_layout(ui_FaultScreen);
  history_page = 0;
  ui_fault_history_refresh();
}
/** Fill the history page from copied event records, preserving raised and recovered entries. */
void ui_fault_history_refresh(void) {
  if (ui_FaultScreen == NULL)
    return;
  uint32_t count = app_fault_history_count();
  if (history_page * 4U >= count)
    history_page = count == 0 ? 0 : (count - 1U) / 4U;
  lv_label_set_text_fmt(ui_FaultSummary,
                        "%lu events | page %lu/%lu | %s | pending %lu | lost %lu",
                        (unsigned long)count,
                        (unsigned long)history_page + 1U,
                        (unsigned long)(count == 0 ? 1U : (count + 3U) / 4U),
                        app_fault_history_durable() ? "SAVED" : "CHECK STORAGE",
                        (unsigned long)app_fault_history_pending(),
                        (unsigned long)app_fault_history_dropped());
  for (unsigned row = 0; row < 4; ++row) {
    uint32_t offset = history_page * 4U + row;
    app_fault_event_t event;
    if (offset >= count) {
      lv_label_set_text(ui_FaultRows[row], row == 0 ? "No malfunctions recorded" : "");
    } else if (!app_fault_history_get(count - 1U - offset, &event)) {
      lv_label_set_text(ui_FaultRows[row], "Event read failed - check storage");
    } else {
      const char *state = event.state == APP_FAULT_STATE_ACTIVE    ? "FAULT"
                          : event.state == APP_FAULT_STATE_HEALTHY ? "CLEAR"
                                                                   : "UNKNOWN";
      lv_label_set_text_fmt(ui_FaultRows[row],
                            "#%lu B%lu %lus %s\n%s [%lu]",
                            (unsigned long)event.sequence,
                            (unsigned long)event.session_id,
                            (unsigned long)(event.timestamp_ms / 1000U),
                            state,
                            app_fault_source_name(event.source),
                            (unsigned long)event.detail);
      lv_obj_set_style_text_color(ui_FaultRows[row],
                                  lv_color_hex(event.state == APP_FAULT_STATE_ACTIVE    ? 0xFF5555
                                               : event.state == APP_FAULT_STATE_HEALTHY ? 0x52BA00
                                                                                        : 0xBBBBBB),
                                  0);
    }
  }
}
/** Delete roots and their owned labels; pending history is independent of screen lifetime. */
void ui_diagnostics_destroy(void) {
  if (ui_DetailScreen != NULL)
    lv_obj_del(ui_DetailScreen);
  if (ui_FaultScreen != NULL)
    lv_obj_del(ui_FaultScreen);
  ui_DetailScreen = ui_FaultScreen = ui_FaultSummary = NULL;
  for (unsigned i = 0; i < 4; ++i)
    ui_DetailValues[i] = ui_DetailStates[i] = ui_FaultRows[i] = NULL;
}
