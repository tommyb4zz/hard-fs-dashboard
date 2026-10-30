/* Pack and malfunction-history screens built beside the SquareLine screens at runtime. */
#include "app_diagnostics_ui.h"

#include <stdio.h>

#include "app_fault_history.h"
#include "app_fault_monitor.h"
#include "ui/ui.h"

#define APP_DIAG_GRID_RED 0xA00000U
#define APP_DIAG_OK_COLOR 0x52BA00U
#define APP_DIAG_FAULT_COLOR 0xFF5555U
#define APP_DIAG_WARN_COLOR 0xFFB000U
#define APP_DIAG_NEUTRAL_COLOR 0xBBBBBBU

lv_obj_t *app_ui_pack_screen, *app_ui_history_screen;
lv_obj_t *app_ui_pack_values[APP_PACK_ROW_COUNT], *app_ui_pack_states[APP_PACK_ROW_COUNT];
lv_obj_t *app_ui_history_journal, *app_ui_history_summary, *app_ui_history_status;
lv_obj_t *app_ui_history_rows[APP_HISTORY_ROWS_PER_PAGE];
static uint32_t history_page;
/* What the history page currently shows, so unchanged refreshes skip storage reads. */
static struct {
  bool valid;
  uint32_t count, page, pending, dropped;
  app_fault_journal_state_t journal;
} history_shown;

/** Create plain bounded text in the existing diagnostic font/color system. */
static lv_obj_t *app_diag_label(lv_obj_t *parent, const char *text, int x, int y, int width) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, width);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, &ui_font_TomorrowBoldItalic18, 0);
  return label;
}
/**
 * Draw the debug table's red grid at the same 10..470 x 12..258 coordinates as the
 * SquareLine DebugFrame image: a 38 px header and four 52 px rows. An empty middle header
 * merges the two value columns.
 */
static void
app_diag_grid(lv_obj_t *screen, const char *const headers[3], const char *const rows[4]) {
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
      lv_obj_set_style_border_color(cell, lv_color_hex(APP_DIAG_GRID_RED), 0);
      lv_obj_set_style_border_width(cell, 1, 0);
      lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    if (row != 0)
      app_diag_label(screen, rows[row - 1], 20, 65 + (int)(row - 1) * 52, 144);
  }
  for (unsigned col = 0; col < 3; ++col)
    app_diag_label(screen, headers[col], x[col + 1] + 12, 22, x[col + 2] - x[col + 1] - 12);
}
/** Load another existing screen; the history page is brought up to date first. */
static void app_diag_navigate(lv_event_t *event) {
  lv_obj_t *target = (lv_obj_t *)lv_event_get_user_data(event);
  if (target == NULL)
    return;
  if (target == app_ui_history_screen)
    app_diagnostics_ui_refresh_history();
  lv_disp_load_scr(target);
}
/** Same gesture and transition as the SquareLine debug screen: long press returns to main. */
static void app_diag_long_press_main(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED)
    _ui_screen_change(&ui_MainScreen, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_MainScreen_screen_init);
}
/** Move one page toward newer (user data non-NULL) or older events, clamped at both ends. */
static void app_diag_page(lv_event_t *event) {
  uint32_t count = app_fault_history_count();
  if (lv_event_get_user_data(event) != NULL) {
    if (history_page > 0U)
      history_page--;
  } else if ((history_page + 1U) * APP_HISTORY_ROWS_PER_PAGE < count) {
    history_page++;
  }
  app_diagnostics_ui_refresh_history();
}
/** Add a bordered text button in a table cell or the history footer. */
static void app_diag_button(lv_obj_t *screen,
                            const char *text,
                            int x,
                            int y,
                            int width,
                            lv_event_cb_t callback,
                            void *user_data) {
  lv_obj_t *button = lv_obj_create(screen);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, 38);
  lv_obj_set_style_bg_color(button, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_style_radius(button, 0, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(APP_DIAG_GRID_RED), 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_t *label = app_diag_label(button, text, 0, 0, width - 4);
  lv_obj_set_style_text_font(label, &ui_font_TomorrowBoldItalic12, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
}
/** Make a black fixed-size root that returns to main on long press. */
static lv_obj_t *app_diag_screen(void) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(screen, app_diag_long_press_main, LV_EVENT_LONG_PRESSED, NULL);
  return screen;
}
/** Create the extra screens once; measurement text is filled by the runtime binding. */
void app_diagnostics_ui_init(void) {
  if (app_ui_pack_screen != NULL || ui_DebugScreen == NULL || ui_MainScreen == NULL)
    return;
  /* The SquareLine export captions this indicator "BSPB"; the device is the BSPD. */
  if (ui_labelBSPD != NULL)
    lv_label_set_text(ui_labelBSPD, "BSPD");
  static const char *const headers[] = {"VALUE", "", "STATE"};
  static const char *const rows[] = {"Pack V", "Sum V", "Current A", "SOC %"};
  app_ui_pack_screen = app_diag_screen();
  app_diag_grid(app_ui_pack_screen, headers, rows);
  for (unsigned i = 0; i < APP_PACK_ROW_COUNT; ++i) {
    app_ui_pack_values[i] = app_diag_label(app_ui_pack_screen, "--", 176, 66 + (int)i * 52, 185);
    app_ui_pack_states[i] = app_diag_label(app_ui_pack_screen, "--", 378, 66 + (int)i * 52, 88);
    lv_obj_set_style_text_font(app_ui_pack_states[i], &ui_font_TomorrowBoldItalic12, 0);
  }
  app_ui_history_screen = app_diag_screen();
  app_ui_history_journal = app_diag_label(app_ui_history_screen, "", 250, 24, 220);
  lv_obj_set_style_text_font(app_ui_history_journal, &ui_font_TomorrowBoldItalic12, 0);
  lv_obj_set_style_text_align(app_ui_history_journal, LV_TEXT_ALIGN_RIGHT, 0);
  app_ui_history_summary = app_diag_label(app_ui_history_screen, "", 14, 54, 452);
  app_ui_history_status = app_diag_label(app_ui_history_screen, "", 14, 70, 452);
  lv_obj_set_style_text_font(app_ui_history_summary, &ui_font_TomorrowBoldItalic12, 0);
  lv_obj_set_style_text_font(app_ui_history_status, &ui_font_TomorrowBoldItalic12, 0);
  lv_obj_set_style_text_color(app_ui_history_status, lv_color_hex(APP_DIAG_NEUTRAL_COLOR), 0);
  for (unsigned i = 0; i < APP_HISTORY_ROWS_PER_PAGE; ++i) {
    app_ui_history_rows[i] = app_diag_label(app_ui_history_screen, "", 14, 90 + (int)i * 34, 452);
    lv_obj_set_style_text_font(app_ui_history_rows[i], &ui_font_TomorrowBoldItalic12, 0);
  }
  /* The SquareLine table's top-left header cell is empty; its buttons fit 10..166 x 12..50. */
  app_diag_button(ui_DebugScreen, "PACK", 10, 12, 76, app_diag_navigate, app_ui_pack_screen);
  app_diag_button(ui_DebugScreen, "FAULTS", 86, 12, 80, app_diag_navigate, app_ui_history_screen);
  app_diag_button(app_ui_pack_screen, "CELLS", 10, 12, 76, app_diag_navigate, ui_DebugScreen);
  app_diag_button(
      app_ui_pack_screen, "FAULTS", 86, 12, 80, app_diag_navigate, app_ui_history_screen);
  app_diag_button(app_ui_history_screen, "CELLS", 10, 12, 76, app_diag_navigate, ui_DebugScreen);
  app_diag_button(app_ui_history_screen, "PACK", 86, 12, 80, app_diag_navigate, app_ui_pack_screen);
  app_diag_button(app_ui_history_screen, "MAIN", 166, 12, 76, app_diag_navigate, ui_MainScreen);
  app_diag_button(
      app_ui_history_screen, "NEWER", 10, 228, 100, app_diag_page, app_ui_history_screen);
  app_diag_button(app_ui_history_screen, "OLDER", 370, 228, 100, app_diag_page, NULL);
  lv_obj_update_layout(ui_DebugScreen);
  lv_obj_update_layout(app_ui_pack_screen);
  lv_obj_update_layout(app_ui_history_screen);
  history_page = 0U;
  history_shown.valid = false;
  app_diagnostics_ui_refresh_history();
}
/** Describe where new events go; anything but SAVED is shown in a warning color. */
static void app_diag_show_journal(app_fault_journal_state_t journal) {
  static const char *const text[] = {"SAVED TO CARD",
                                     "NOT SAVED: NO CARD",
                                     "RETRYING CARD WRITE",
                                     "NOT SAVED: CHECK CARD",
                                     "NOT SAVED: JOURNAL FULL"};
  lv_label_set_text(app_ui_history_journal,
                    (unsigned)journal < sizeof(text) / sizeof(text[0]) ? text[journal] : "?");
  lv_obj_set_style_text_color(app_ui_history_journal,
                              lv_color_hex(journal == APP_FAULT_JOURNAL_SAVED ? APP_DIAG_OK_COLOR
                                           : journal == APP_FAULT_JOURNAL_WRITE_FAILED
                                               ? APP_DIAG_WARN_COLOR
                                               : APP_DIAG_FAULT_COLOR),
                              0);
}
/** Fill one history row from a copied event; returns false when the slot could not be read. */
static bool app_diag_show_event(lv_obj_t *row, uint32_t index) {
  app_fault_event_t event;
  if (!app_fault_history_get(index, &event)) {
    lv_label_set_text_fmt(
        row, "#%lu  unreadable record, kept as found on the card", (unsigned long)index + 1U);
    lv_obj_set_style_text_color(row, lv_color_hex(APP_DIAG_NEUTRAL_COLOR), 0);
    return false;
  }
  char description[96];
  app_fault_describe(&event, description, sizeof(description));
  const char *state = event.state == APP_FAULT_STATE_ACTIVE    ? "FAULT"
                      : event.state == APP_FAULT_STATE_HEALTHY ? "CLEAR"
                                                               : "UNKNOWN";
  lv_label_set_text_fmt(row,
                        "#%lu  boot %lu  %lu.%lus  %s\n%s",
                        (unsigned long)event.sequence,
                        (unsigned long)event.session_id,
                        (unsigned long)(event.timestamp_ms / 1000U),
                        (unsigned long)(event.timestamp_ms % 1000U / 100U),
                        state,
                        description);
  lv_obj_set_style_text_color(
      row,
      lv_color_hex(event.state == APP_FAULT_STATE_ACTIVE    ? APP_DIAG_FAULT_COLOR
                   : event.state == APP_FAULT_STATE_HEALTHY ? APP_DIAG_OK_COLOR
                                                            : APP_DIAG_NEUTRAL_COLOR),
      0);
  return true;
}
/** Render four newest-first copied events; storage is read only when the page changed. */
void app_diagnostics_ui_refresh_history(void) {
  if (app_ui_history_screen == NULL)
    return;
  uint32_t count = app_fault_history_count();
  uint32_t pages =
      count == 0U ? 1U : (count + APP_HISTORY_ROWS_PER_PAGE - 1U) / APP_HISTORY_ROWS_PER_PAGE;
  if (history_page >= pages)
    history_page = pages - 1U;
  app_fault_journal_state_t journal = app_fault_history_journal_state();
  uint32_t pending = app_fault_history_pending(), dropped = app_fault_history_dropped();
  if (history_shown.valid && history_shown.count == count && history_shown.page == history_page &&
      history_shown.pending == pending && history_shown.dropped == dropped &&
      history_shown.journal == journal)
    return;
  app_diag_show_journal(journal);
  lv_label_set_text_fmt(app_ui_history_summary,
                        "%lu events | page %lu/%lu | not saved %lu | lost %lu",
                        (unsigned long)count,
                        (unsigned long)history_page + 1U,
                        (unsigned long)pages,
                        (unsigned long)pending,
                        (unsigned long)dropped);
  bool all_read = true;
  for (unsigned row = 0; row < APP_HISTORY_ROWS_PER_PAGE; ++row) {
    uint32_t offset = history_page * APP_HISTORY_ROWS_PER_PAGE + row;
    if (offset < count) {
      all_read = app_diag_show_event(app_ui_history_rows[row], count - 1U - offset) && all_read;
      continue;
    }
    lv_label_set_text(app_ui_history_rows[row],
                      row == 0U && count == 0U ? "No malfunctions recorded" : "");
    lv_obj_set_style_text_color(app_ui_history_rows[row], lv_color_hex(APP_DIAG_NEUTRAL_COLOR), 0);
  }
  history_shown.valid = all_read; /* Retry unreadable slots on the next refresh. */
  history_shown.count = count;
  history_shown.page = history_page;
  history_shown.pending = pending;
  history_shown.dropped = dropped;
  history_shown.journal = journal;
}
/** Name the active root for host commands, tests and the live simulator. */
const char *app_diagnostics_ui_screen_name(void) {
  lv_obj_t *screen = lv_scr_act();
  if (screen == NULL)
    return "other";
  return screen == ui_MainScreen           ? "main"
         : screen == ui_DebugScreen        ? "debug"
         : screen == app_ui_pack_screen    ? "pack"
         : screen == app_ui_history_screen ? "faults"
                                           : "other";
}
/** Delete the extra roots and their owned labels; history itself is independent of screens. */
void app_diagnostics_ui_destroy(void) {
  if (app_ui_pack_screen != NULL)
    lv_obj_del(app_ui_pack_screen);
  if (app_ui_history_screen != NULL)
    lv_obj_del(app_ui_history_screen);
  app_ui_pack_screen = app_ui_history_screen = NULL;
  app_ui_history_journal = app_ui_history_summary = app_ui_history_status = NULL;
  for (unsigned i = 0; i < APP_PACK_ROW_COUNT; ++i)
    app_ui_pack_values[i] = app_ui_pack_states[i] = NULL;
  for (unsigned i = 0; i < APP_HISTORY_ROWS_PER_PAGE; ++i)
    app_ui_history_rows[i] = NULL;
  history_shown.valid = false;
}
