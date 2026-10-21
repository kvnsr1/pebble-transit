#include <pebble.h>
#include "presentation.h"

#define STOP_LIMIT 5
#define HOME_ROWS 3
#define HOME_TOP 19
#define ROW_HEIGHT 64
#define FOOTER_TOP 212
#define REFRESH_INTERVAL_MS (60 * 1000)
#define ANIMATION_INTERVAL_MS 40

typedef enum { SCREEN_NEARBY, SCREEN_BOARD, SCREEN_STOPS } Screen;
typedef enum { STATE_LOADING, STATE_READY, STATE_EMPTY, STATE_KEY, STATE_LOCATION,
               STATE_SERVICE, STATE_PHONE } DataState;
typedef struct {
  char route[16];
  char headsign[160];
  char stop[176];
  int32_t departure;
  uint32_t color, text_color;
  bool live, favorite;
} HomeRow;
typedef struct {
  char route_name[48], headsign[160], stop_name[176], error[160];
  char stop_names[STOP_LIMIT][176];
  HomeRow home[HOME_ROWS];
  int32_t departures[3], stop_times[STOP_LIMIT], updated_at;
  uint32_t route_color, text_color;
  int line_index, line_count, direction_index, direction_count, departure_index;
  int stop_count, home_count, home_selected, home_start;
  int stop_live_flags, stop_estimate_flags;
  DataState state;
  bool live[3], favorite, loading, cached;
} TransitData;

static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_refresh_timer, *s_animation_timer, *s_startup_timer;
static TransitData s_transit;
static Screen s_screen;
static HomeRow s_outgoing_row;
static int s_outgoing_slot, s_list_offset, s_selected_stop;
static int s_marker_y = HOME_TOP + ROW_HEIGHT / 2;
static int s_marker_target = HOME_TOP + ROW_HEIGHT / 2;
static bool s_connected, s_phone_unreachable, s_received;
typedef struct { uint32_t key; int8_t value; } Request;
static Request s_requests[8];
static unsigned s_request_count;
static bool s_sending;

static GColor color_from_hex(uint32_t rgb) { return (GColor){.argb = transit_palette(rgb)}; }
static GColor ink_for(uint32_t bg, uint32_t requested) { return (GColor){.argb = transit_ink(bg, requested)}; }
static GColor route_color(void) { return color_from_hex(s_transit.route_color); }
static GColor route_ink(void) { return ink_for(s_transit.route_color, s_transit.text_color); }
static const char *route_badge(void) { return s_transit.home[s_transit.home_selected].route; }
static GFont font(const char *key) { return fonts_get_system_font(key); }
static void redraw(void) { if (s_canvas) { layer_mark_dirty(s_canvas); } }
static bool stale(void) {
  return transit_is_stale(s_transit.updated_at, (int32_t)time(NULL),
    s_transit.cached, s_connected && !s_phone_unreachable);
}
static void fill(GContext *ctx, GRect rect, GColor color, int radius) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, rect, radius, GCornersAll);
}
static void text(GContext *ctx, const char *value, GRect rect, const char *key,
                 GColor color, GTextAlignment alignment) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, value, font(key), rect, GTextOverflowModeTrailingEllipsis, alignment, NULL);
}
static void line(GContext *ctx, GPoint from, GPoint to, GColor color, int width) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  graphics_draw_line(ctx, from, to);
}
static void chevron(GContext *ctx, GPoint p, int direction, GColor color) {
  line(ctx, GPoint(p.x - 3, p.y), GPoint(p.x, p.y + 3 * direction), color, 2);
  line(ctx, GPoint(p.x, p.y + 3 * direction), GPoint(p.x + 3, p.y), color, 2);
}
static void direction_icon(GContext *ctx, GPoint p, GColor color) {
  line(ctx, GPoint(p.x - 6, p.y - 3), GPoint(p.x + 6, p.y - 3), color, 2);
  line(ctx, GPoint(p.x + 6, p.y - 3), GPoint(p.x + 3, p.y - 6), color, 2);
  line(ctx, GPoint(p.x - 6, p.y + 3), GPoint(p.x + 6, p.y + 3), color, 2);
  line(ctx, GPoint(p.x - 6, p.y + 3), GPoint(p.x - 3, p.y + 6), color, 2);
}
static void pin_icon(GContext *ctx, GPoint p, GColor color) {
  fill(ctx, GRect(p.x - 3, p.y - 5, 7, 3), color, 0);
  fill(ctx, GRect(p.x - 2, p.y - 2, 5, 4), color, 0);
  line(ctx, GPoint(p.x - 4, p.y + 2), GPoint(p.x + 4, p.y + 2), color, 2);
  line(ctx, GPoint(p.x, p.y + 2), GPoint(p.x, p.y + 7), color, 2);
}
static void signal_icon(GContext *ctx, GPoint p, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, p, 1);
  line(ctx, GPoint(p.x, p.y - 4), GPoint(p.x + 2, p.y - 4), color, 1);
  line(ctx, GPoint(p.x + 2, p.y - 4), GPoint(p.x + 4, p.y - 2), color, 1);
  line(ctx, GPoint(p.x + 4, p.y - 2), GPoint(p.x + 4, p.y), color, 1);
  line(ctx, GPoint(p.x, p.y - 7), GPoint(p.x + 3, p.y - 7), color, 1);
  line(ctx, GPoint(p.x + 3, p.y - 7), GPoint(p.x + 7, p.y - 3), color, 1);
  line(ctx, GPoint(p.x + 7, p.y - 3), GPoint(p.x + 7, p.y), color, 1);
}
static void transit_icon(GContext *ctx, GPoint p, GColor color, GColor bg) {
  fill(ctx, GRect(p.x - 16, p.y - 19, 32, 35), color, 7);
  fill(ctx, GRect(p.x - 11, p.y - 12, 9, 12), bg, 1);
  fill(ctx, GRect(p.x + 2, p.y - 12, 9, 12), bg, 1);
  fill(ctx, GRect(p.x - 10, p.y + 6, 5, 3), bg, 0);
  fill(ctx, GRect(p.x + 5, p.y + 6, 5, 3), bg, 0);
  line(ctx, GPoint(p.x - 10, p.y + 21), GPoint(p.x - 5, p.y + 16), color, 2);
  line(ctx, GPoint(p.x + 5, p.y + 16), GPoint(p.x + 10, p.y + 21), color, 2);
}
static void clock_time(int32_t epoch, char *buffer, size_t size, bool suffix) {
  if (epoch <= 0) { snprintf(buffer, size, "--"); return; }
  time_t value = epoch;
  struct tm *local = localtime(&value);
  strftime(buffer, size, clock_is_24h_style() ? "%H:%M" : (suffix ? "%I:%M%p" : "%I:%M"), local);
  if (!clock_is_24h_style()) {
    if (buffer[0] == '0') { memmove(buffer, buffer + 1, strlen(buffer)); }
    if (suffix) {
      size_t length = strlen(buffer);
      buffer[length - 2] = buffer[length - 2] == 'A' ? 'a' : 'p';
      buffer[length - 1] = '\0';
    }
  }
}
static TransitEta eta_text(int32_t epoch, char *buffer, size_t size) {
  TransitEta eta = transit_eta(epoch, (int32_t)time(NULL));
  if (eta.kind == ETA_MINUTES) { snprintf(buffer, size, "%d", eta.minutes); }
  else if (eta.kind == ETA_NOW) { snprintf(buffer, size, "NOW"); }
  else if (eta.kind == ETA_LEFT) { snprintf(buffer, size, "LEFT"); }
  else if (eta.kind == ETA_CLOCK) { clock_time(epoch, buffer, size, true); }
  else { snprintf(buffer, size, "--"); }
  return eta;
}
static const char *prediction_label(bool live, int32_t departure) {
  TransitEta eta = transit_eta(departure, (int32_t)time(NULL));
  if (eta.kind == ETA_NONE || eta.kind == ETA_LEFT) { return ""; }
  return stale() ? "Saved" : (live ? "Live" : "Sched");
}
static const char *status_label(void) {
  if (!s_connected) { return "Phone disconnected"; }
  if (s_phone_unreachable) { return "Phone not responding"; }
  if (s_transit.loading) { return "Updating departures..."; }
  if (s_transit.error[0]) {
    if (s_transit.state == STATE_KEY) { return "API key: phone settings"; }
    if (s_transit.state == STATE_LOCATION) { return "Location unavailable"; }
    return "Offline / saved departures";
  }
  return stale() ? "Saved / waiting for update" : NULL;
}
static void status_footer(GContext *ctx, GRect bounds, const char *hint, GColor bg, GColor ink) {
  const char *status = status_label();
  fill(ctx, GRect(0, FOOTER_TOP, bounds.size.w, bounds.size.h - FOOTER_TOP), bg, 0);
  text(ctx, status ? status : hint, GRect(5, FOOTER_TOP - 2, bounds.size.w - 10, 18),
    FONT_KEY_GOTHIC_14, ink, GTextAlignmentCenter);
}
static void draw_state(GContext *ctx, GRect bounds) {
  GColor green = color_from_hex(0x55ff55);
  text(ctx, "NEARBY", GRect(8, -2, 120, 20), FONT_KEY_GOTHIC_14_BOLD, green, GTextAlignmentLeft);
  transit_icon(ctx, GPoint(bounds.size.w / 2, 61), green, GColorBlack);
  const char *title, *body;
  if (!s_connected || s_phone_unreachable) {
    title = "Connect your phone"; body = "Keep your phone nearby and open the Pebble app.";
  } else if (s_transit.loading || s_transit.state == STATE_LOADING) {
    title = "Finding your ride"; body = "Looking for departures near your phone.";
  } else if (s_transit.state == STATE_KEY) {
    title = "Add your API key"; body = "Open Pebble Transit's settings in the phone app.";
  } else if (s_transit.state == STATE_EMPTY) {
    title = "No nearby routes"; body = "Try a wider radius or enable more transit modes in settings.";
  } else if (s_transit.state == STATE_LOCATION) {
    title = "Where are you?"; body = "Allow location access for the Pebble app on your phone.";
  } else {
    title = "Can't get departures";
    body = s_transit.error[0] ? s_transit.error : "Check your phone connection and try again.";
  }
  text(ctx, title, GRect(8, 91, bounds.size.w - 16, 58), FONT_KEY_GOTHIC_24_BOLD,
    GColorWhite, GTextAlignmentCenter);
  text(ctx, body, GRect(12, 148, bounds.size.w - 24, 58), FONT_KEY_GOTHIC_18,
    GColorLightGray, GTextAlignmentCenter);
  text(ctx, s_transit.loading ? "Locating / connecting" : "SELECT  Try again",
    GRect(8, 211, bounds.size.w - 16, 18), FONT_KEY_GOTHIC_14, green, GTextAlignmentCenter);
}
static void draw_home_row(GContext *ctx, const HomeRow *row, int y) {
  GColor accent = color_from_hex(row->color), ink = ink_for(row->color, row->text_color);
  fill(ctx, GRect(6, y, 188, ROW_HEIGHT - 4), accent, 6);
  text(ctx, row->route, GRect(12, y - 5, 78, 39), FONT_KEY_BITHAM_30_BLACK, ink, GTextAlignmentLeft);
  if (row->favorite) { pin_icon(ctx, GPoint(100, y + 13), ink); }
  text(ctx, row->headsign, GRect(12, y + 27, 132, 24), FONT_KEY_GOTHIC_18_BOLD, ink, GTextAlignmentLeft);
  text(ctx, row->stop, GRect(12, y + 44, 134, 16), FONT_KEY_GOTHIC_14, ink, GTextAlignmentLeft);
  char value[16];
  TransitEta eta = eta_text(row->departure, value, sizeof(value));
  const char *eta_font = eta.kind == ETA_MINUTES ? FONT_KEY_LECO_32_BOLD_NUMBERS :
    FONT_KEY_GOTHIC_18_BOLD;
  text(ctx, value, GRect(144, y - 2, 45, 40), eta_font, ink, GTextAlignmentRight);
  text(ctx, eta.kind == ETA_MINUTES ? "min" : (eta.kind == ETA_CLOCK ? "at" : ""),
    GRect(150, y + 28, 38, 16), FONT_KEY_GOTHIC_14, ink, GTextAlignmentRight);
  text(ctx, prediction_label(row->live, row->departure), GRect(148, y + 44, 40, 16),
    FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentRight);
  if (row->live && !stale() && eta.kind != ETA_NONE && eta.kind != ETA_LEFT) {
    signal_icon(ctx, GPoint(151, y + 39), ink);
  }
}
static void draw_home(GContext *ctx, GRect bounds) {
  if (!s_transit.home_count) { draw_state(ctx, bounds); return; }
  for (int i = 0; i < s_transit.home_count; i++) {
    draw_home_row(ctx, &s_transit.home[i], HOME_TOP + i * ROW_HEIGHT + s_list_offset);
  }
  if (s_list_offset) {
    draw_home_row(ctx, &s_outgoing_row, HOME_TOP + s_outgoing_slot * ROW_HEIGHT + s_list_offset);
  }
  fill(ctx, GRect(0, s_marker_y - 22, 3, 44), GColorWhite, 1);
  fill(ctx, GRect(0, 0, bounds.size.w, HOME_TOP), GColorBlack, 0);
  text(ctx, "NEARBY", GRect(8, -3, 100, 20), FONT_KEY_GOTHIC_14_BOLD, GColorLightGray, GTextAlignmentLeft);
  char position[20];
  snprintf(position, sizeof(position), "%d / %d", s_transit.line_index + 1, s_transit.line_count);
  text(ctx, position, GRect(120, -3, 72, 20), FONT_KEY_GOTHIC_14, GColorLightGray, GTextAlignmentRight);
  status_footer(ctx, bounds, "SELECT departures / HOLD direction", GColorBlack, GColorLightGray);
}
static void draw_board(GContext *ctx, GRect bounds) {
  GColor accent = route_color(), ink = route_ink();
  fill(ctx, bounds, accent, 0);
  text(ctx, "DEPARTURES", GRect(8, -3, 130, 19), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
  text(ctx, route_badge(), GRect(8, 12, 78, 39), FONT_KEY_BITHAM_30_BLACK, ink, GTextAlignmentLeft);
  if (strcmp(route_badge(), s_transit.route_name) != 0) {
    text(ctx, s_transit.route_name, GRect(86, 19, 62, 30), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
  }
  if (s_transit.favorite) { pin_icon(ctx, GPoint(157, 30), ink); }
  if (s_transit.direction_count > 1) { direction_icon(ctx, GPoint(184, 30), ink); }
  GSize direction_size = graphics_text_layout_get_content_size(s_transit.headsign,
    font(FONT_KEY_GOTHIC_24_BOLD), GRect(0, 0, 184, 80), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  text(ctx, s_transit.headsign, GRect(8, 45, 184, 56),
    direction_size.h > 50 ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_24_BOLD, ink, GTextAlignmentLeft);
  text(ctx, s_transit.stop_name, GRect(8, 98, 184, 29), FONT_KEY_GOTHIC_14, ink, GTextAlignmentLeft);
  int selected = s_transit.departure_index;
  char value[16];
  TransitEta eta = eta_text(s_transit.departures[selected], value, sizeof(value));
  if (eta.kind == ETA_NONE) {
    text(ctx, "No departures", GRect(8, 130, 184, 35), FONT_KEY_GOTHIC_24_BOLD, ink, GTextAlignmentLeft);
    text(ctx, "Try the other direction", GRect(8, 164, 184, 25), FONT_KEY_GOTHIC_18, ink, GTextAlignmentLeft);
  } else {
    const char *hero_font = eta.kind == ETA_MINUTES ? FONT_KEY_LECO_60_BOLD_NUMBERS_AM_PM :
      (eta.kind == ETA_CLOCK ? FONT_KEY_BITHAM_30_BLACK : FONT_KEY_BITHAM_42_BOLD);
    text(ctx, value, GRect(7, 119, eta.kind == ETA_MINUTES ? 106 : 184, 77), hero_font, ink, GTextAlignmentLeft);
    char label[32];
    if (selected == 0) { snprintf(label, sizeof(label), "NEXT"); }
    else { snprintf(label, sizeof(label), "TRIP %d", selected + 1); }
    if (eta.kind == ETA_MINUTES) {
      text(ctx, label, GRect(117, 127, 76, 19), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
      text(ctx, "min", GRect(117, 145, 68, 28), FONT_KEY_GOTHIC_24_BOLD, ink, GTextAlignmentLeft);
      text(ctx, prediction_label(s_transit.live[selected], s_transit.departures[selected]),
        GRect(117, 171, 76, 18), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
    } else {
      char status[48];
      snprintf(status, sizeof(status), "%s / %s", label,
        prediction_label(s_transit.live[selected], s_transit.departures[selected]));
      text(ctx, status, GRect(8, 171, 184, 18), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
    }
  }
  line(ctx, GPoint(8, 189), GPoint(192, 189), ink, 1);
  for (int i = 0; i < 2; i++) {
    int index = (selected + i + 1) % 3;
    char next[16], summary[36];
    TransitEta next_eta = eta_text(s_transit.departures[index], next, sizeof(next));
    snprintf(summary, sizeof(summary), "%s%s %s", next, next_eta.kind == ETA_MINUTES ? "m" : "",
      prediction_label(s_transit.live[index], s_transit.departures[index]));
    text(ctx, summary, GRect(8 + i * 96, 188, 91, 24), FONT_KEY_GOTHIC_18_BOLD, ink, GTextAlignmentLeft);
  }
  if (status_label()) { status_footer(ctx, bounds, "", accent, ink); }
  else {
    chevron(ctx, GPoint(10, 221), -1, ink);
    text(ctx, "trip", GRect(18, 210, 37, 18), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
    direction_icon(ctx, GPoint(83, 220), ink);
    text(ctx, "dir", GRect(94, 210, 32, 18), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
    text(ctx, "stops", GRect(145, 210, 38, 18), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
    chevron(ctx, GPoint(189, 218), 1, ink);
  }
}
static void draw_stops(GContext *ctx, GRect bounds) {
  GColor accent = route_color(), ink = route_ink();
  fill(ctx, GRect(0, 0, bounds.size.w, 49), accent, 0);
  text(ctx, route_badge(), GRect(8, -3, 58, 32), FONT_KEY_GOTHIC_24_BOLD, ink, GTextAlignmentLeft);
  text(ctx, s_transit.headsign, GRect(68, 2, 124, 23), FONT_KEY_GOTHIC_18_BOLD, ink, GTextAlignmentLeft);
  char eta[16], trip[48];
  TransitEta value = eta_text(s_transit.departures[s_transit.departure_index], eta, sizeof(eta));
  snprintf(trip, sizeof(trip), "TRIP %d / %s%s / %s", s_transit.departure_index + 1, eta,
    value.kind == ETA_MINUTES ? " min" : "", prediction_label(s_transit.live[s_transit.departure_index], s_transit.departures[s_transit.departure_index]));
  text(ctx, trip, GRect(8, 28, 184, 19), FONT_KEY_GOTHIC_14_BOLD, ink, GTextAlignmentLeft);
  if (!s_transit.stop_count) {
    transit_icon(ctx, GPoint(100, 96), accent, GColorBlack);
    text(ctx, s_transit.loading ? "Finding trip stops" : "Stops unavailable", GRect(8, 126, 184, 32),
      FONT_KEY_GOTHIC_24_BOLD, GColorWhite, GTextAlignmentCenter);
    text(ctx, s_transit.loading ? "Getting this departure's timeline" : "Try another departure with Select",
      GRect(12, 164, 176, 40), FONT_KEY_GOTHIC_18, GColorLightGray, GTextAlignmentCenter);
  } else {
    int start = s_selected_stop > 0 ? s_selected_stop - 1 : 0;
    if (start > s_transit.stop_count - 3) { start = s_transit.stop_count - 3; }
    if (start < 0) { start = 0; }
    int visible = s_transit.stop_count - start;
    if (visible > 3) { visible = 3; }
    int centers[3];
    int y = 53;
    for (int row = 0; row < visible; row++) {
      bool selected = start + row == s_selected_stop;
      centers[row] = y + (selected ? 32 : 22);
      y += selected ? 64 : 44;
    }
    line(ctx, GPoint(16, centers[0]), GPoint(16, centers[visible - 1]), accent, 2);
    y = 53;
    for (int row = 0; row < visible; row++) {
      int i = start + row;
      bool selected = i == s_selected_stop;
      if (selected) { fill(ctx, GRect(25, y, 172, 62), GColorDarkGray, 3); }
      fill(ctx, GRect(12, centers[row] - 4, 9, 9), accent, 4);
      if (selected) { fill(ctx, GRect(14, centers[row] - 2, 5, 5), GColorWhite, 2); }
      int name_width = selected ? 161 : 110;
      GSize name_size = graphics_text_layout_get_content_size(s_transit.stop_names[i],
        font(FONT_KEY_GOTHIC_18), GRect(0, 0, name_width, 120), GTextOverflowModeWordWrap, GTextAlignmentLeft);
      const char *name_font = name_size.h > 42 || (selected && strlen(s_transit.stop_names[i]) > 36) ?
        (selected ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_14) :
        (selected ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_18);
      text(ctx, s_transit.stop_names[i], GRect(30, y - 1, name_width, 44), name_font, GColorWhite, GTextAlignmentLeft);
      char stop_time[16];
      clock_time(s_transit.stop_times[i], stop_time, sizeof(stop_time), true);
      if (selected) {
        text(ctx, "Arrival", GRect(30, y + 43, 80, 18), FONT_KEY_GOTHIC_14, GColorLightGray, GTextAlignmentLeft);
      }
      text(ctx, stop_time, GRect(141, y + (selected ? 43 : 12), 51, 18),
        FONT_KEY_GOTHIC_14_BOLD, GColorWhite, GTextAlignmentRight);
      y += selected ? 64 : 44;
    }
  }
  const char *provenance = stale() ? "Saved stop times" :
    ((s_transit.stop_live_flags & (1 << s_selected_stop)) ? "Live stop time" :
      ((s_transit.stop_estimate_flags & (1 << s_selected_stop)) ? "Estimated from trip" : "Scheduled stop time"));
  char progress[48];
  snprintf(progress, sizeof(progress), "%d/%d / %s", s_selected_stop + 1, s_transit.stop_count, provenance);
  status_footer(ctx, bounds, s_transit.stop_count ? progress : "SELECT next trip / BACK departures", GColorBlack, GColorLightGray);
}
static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  fill(ctx, bounds, GColorBlack, 0);
  if (s_screen == SCREEN_NEARBY) { draw_home(ctx, bounds); }
  else if (s_screen == SCREEN_BOARD) { draw_board(ctx, bounds); }
  else { draw_stops(ctx, bounds); }
}
#ifndef TRANSIT_DISABLE_ANIMATION
static int approach(int value, int target) {
  int delta = target - value;
  if (abs(delta) <= 2) { return target; }
  return value + (delta > 0 ? (delta + 2) / 3 : (delta - 2) / 3);
}
static void animation_tick(void *context) {
  s_animation_timer = NULL;
  s_marker_y = approach(s_marker_y, s_marker_target);
  s_list_offset = approach(s_list_offset, 0);
  redraw();
  if (s_list_offset || s_marker_y != s_marker_target) {
    s_animation_timer = app_timer_register(ANIMATION_INTERVAL_MS, animation_tick, NULL);
  }
}
#endif
static void animate(void) {
#ifdef TRANSIT_DISABLE_ANIMATION
  s_marker_y = s_marker_target;
  s_list_offset = 0;
#else
  if (!s_animation_timer) { s_animation_timer = app_timer_register(ANIMATION_INTERVAL_MS, animation_tick, NULL); }
#endif
}
static void flush_requests(void) {
  if (s_sending || !s_request_count) { return; }
  DictionaryIterator *out = NULL;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result == APP_MSG_OK && out) {
    dict_write_int8(out, s_requests[0].key, s_requests[0].value);
    s_sending = true;
    result = app_message_outbox_send();
  }
  if (result != APP_MSG_OK) {
    s_sending = false;
    s_request_count = 0;
    s_phone_unreachable = true;
    redraw();
  }
}
static void send_request(uint32_t key, int8_t value) {
  if (s_request_count >= sizeof(s_requests) / sizeof(s_requests[0])) { return; }
  s_requests[s_request_count++] = (Request){key, value};
  flush_requests();
}
static void outbox_sent(DictionaryIterator *iter, void *context) {
  s_sending = false;
  if (s_request_count) {
    s_request_count--;
    memmove(s_requests, s_requests + 1, s_request_count * sizeof(Request));
  }
  flush_requests();
}
static void move_route(int delta) {
  if (s_transit.home_count) { send_request(MESSAGE_KEY_REQUEST_ROW_DELTA, delta); }
}
static void up_click(ClickRecognizerRef recognizer, void *context) {
  if (s_screen == SCREEN_NEARBY) { move_route(-1); }
  else if (s_screen == SCREEN_BOARD) { send_request(MESSAGE_KEY_REQUEST_DEPARTURE, 1); }
  else if (s_selected_stop > 0) { s_selected_stop--; redraw(); }
}
static void down_click(ClickRecognizerRef recognizer, void *context) {
  if (s_screen == SCREEN_NEARBY) { move_route(1); }
  else if (s_screen == SCREEN_BOARD) {
    s_screen = SCREEN_STOPS; s_selected_stop = 0; s_transit.loading = true;
    send_request(MESSAGE_KEY_REQUEST_STOPS, 1); redraw();
  } else if (s_selected_stop + 1 < s_transit.stop_count) { s_selected_stop++; redraw(); }
}
static void change_direction(void) {
  if (!s_transit.home_count || s_transit.direction_count < 2) { return; }
  s_transit.stop_count = 0; s_transit.departure_index = 0;
  send_request(MESSAGE_KEY_REQUEST_DIRECTION, s_screen == SCREEN_BOARD ? 2 : 1);
}
static void select_click(ClickRecognizerRef recognizer, void *context) {
  if (s_screen == SCREEN_NEARBY) {
    if (!s_transit.home_count) { s_transit.loading = true; send_request(MESSAGE_KEY_REQUEST_REFRESH, 2); }
    else {
      s_screen = SCREEN_BOARD; s_transit.departure_index = 0; s_transit.stop_count = 0;
      send_request(MESSAGE_KEY_REQUEST_DETAILS, 1);
    }
  } else if (s_screen == SCREEN_BOARD) { change_direction(); }
  else { s_selected_stop = 0; s_transit.loading = true; send_request(MESSAGE_KEY_REQUEST_DEPARTURE, 1); }
  redraw();
}
static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_screen != SCREEN_STOPS) { change_direction(); }
}
static void up_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_screen == SCREEN_STOPS || !s_transit.home_count) { return; }
  send_request(MESSAGE_KEY_REQUEST_TOGGLE_FAVORITE, 1); vibes_short_pulse();
}
static void back_click(ClickRecognizerRef recognizer, void *context) {
  if (s_screen == SCREEN_STOPS) { s_screen = SCREEN_BOARD; send_request(MESSAGE_KEY_REQUEST_STOPS, 2); }
  else if (s_screen == SCREEN_BOARD) { s_screen = SCREEN_NEARBY; send_request(MESSAGE_KEY_REQUEST_DETAILS, 2); }
  else { window_stack_pop(true); return; }
  redraw();
}
static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_long_click_subscribe(BUTTON_ID_UP, 650, up_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 650, select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}
static void refresh_tick(void *context) {
  send_request(MESSAGE_KEY_REQUEST_REFRESH, 2);
  s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, refresh_tick, NULL);
}
static void minute_tick(struct tm *tick_time, TimeUnits units_changed) { redraw(); }
static void connection_changed(bool connected) {
  s_connected = connected;
  if (!connected) { s_transit.loading = false; }
  else { s_phone_unreachable = false; send_request(MESSAGE_KEY_REQUEST_REFRESH, 2); }
  redraw();
}
static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  s_sending = false; s_request_count = 0;
  s_phone_unreachable = true; s_transit.loading = false; redraw();
}
static void startup_timeout(void *context) {
  s_startup_timer = NULL;
  if (!s_received) { s_phone_unreachable = true; s_transit.loading = false; redraw(); }
}
static void copy_tuple(DictionaryIterator *iter, uint32_t key, char *dest, size_t size) {
  Tuple *tuple = dict_find(iter, key);
  if (tuple && tuple->type == TUPLE_CSTRING) { transit_copy_text(dest, size, tuple->value->cstring); }
}
static int32_t tuple_int(DictionaryIterator *iter, uint32_t key, int32_t fallback) {
  Tuple *tuple = dict_find(iter, key);
  return tuple && (tuple->type == TUPLE_INT || tuple->type == TUPLE_UINT) ? tuple->value->int32 : fallback;
}
static void inbox_received(DictionaryIterator *iter, void *context) {
  int old_start = s_transit.home_start, old_selected = s_transit.home_selected;
  s_outgoing_row = s_transit.home[0]; s_outgoing_slot = -1;
  if (tuple_int(iter, MESSAGE_KEY_HOME_START, old_start) < old_start) {
    s_outgoing_row = s_transit.home[HOME_ROWS - 1]; s_outgoing_slot = HOME_ROWS;
  }
  s_received = true; s_phone_unreachable = false;
  copy_tuple(iter, MESSAGE_KEY_ROUTE_NAME, s_transit.route_name, sizeof(s_transit.route_name));
  copy_tuple(iter, MESSAGE_KEY_HEADSIGN, s_transit.headsign, sizeof(s_transit.headsign));
  copy_tuple(iter, MESSAGE_KEY_STOP_NAME, s_transit.stop_name, sizeof(s_transit.stop_name));
  copy_tuple(iter, MESSAGE_KEY_ERROR_MESSAGE, s_transit.error, sizeof(s_transit.error));
  s_transit.line_index = tuple_int(iter, MESSAGE_KEY_LINE_INDEX, s_transit.line_index);
  s_transit.line_count = tuple_int(iter, MESSAGE_KEY_LINE_COUNT, s_transit.line_count);
  s_transit.route_color = tuple_int(iter, MESSAGE_KEY_ROUTE_COLOR, s_transit.route_color);
  s_transit.text_color = tuple_int(iter, MESSAGE_KEY_ROUTE_TEXT_COLOR, s_transit.text_color);
  s_transit.favorite = tuple_int(iter, MESSAGE_KEY_IS_FAVORITE, s_transit.favorite);
  s_transit.loading = tuple_int(iter, MESSAGE_KEY_IS_LOADING, s_transit.loading);
  s_transit.direction_index = tuple_int(iter, MESSAGE_KEY_DIRECTION_INDEX, s_transit.direction_index);
  s_transit.direction_count = tuple_int(iter, MESSAGE_KEY_DIRECTION_COUNT, s_transit.direction_count);
  s_transit.departure_index = tuple_int(iter, MESSAGE_KEY_DEPARTURE_INDEX, s_transit.departure_index);
  s_transit.stop_count = tuple_int(iter, MESSAGE_KEY_STOP_COUNT, s_transit.stop_count);
  s_transit.home_count = tuple_int(iter, MESSAGE_KEY_HOME_COUNT, s_transit.home_count);
  s_transit.home_selected = tuple_int(iter, MESSAGE_KEY_HOME_SELECTED, s_transit.home_selected);
  s_transit.home_start = tuple_int(iter, MESSAGE_KEY_HOME_START, s_transit.home_start);
  s_transit.updated_at = tuple_int(iter, MESSAGE_KEY_DATA_UPDATED_AT, s_transit.updated_at);
  s_transit.state = tuple_int(iter, MESSAGE_KEY_DATA_STATE, s_transit.state);
  s_transit.cached = tuple_int(iter, MESSAGE_KEY_DATA_CACHED, s_transit.cached);
  s_transit.stop_live_flags = tuple_int(iter, MESSAGE_KEY_STOP_LIVE_FLAGS, s_transit.stop_live_flags);
  s_transit.stop_estimate_flags = tuple_int(iter, MESSAGE_KEY_STOP_ESTIMATE_FLAGS, s_transit.stop_estimate_flags);
  if (s_transit.departure_index < 0 || s_transit.departure_index > 2) { s_transit.departure_index = 0; }
  if (s_transit.stop_count < 0) { s_transit.stop_count = 0; }
  if (s_transit.stop_count > STOP_LIMIT) { s_transit.stop_count = STOP_LIMIT; }
  if (s_transit.home_count < 0) { s_transit.home_count = 0; }
  if (s_transit.home_count > HOME_ROWS) { s_transit.home_count = HOME_ROWS; }
  if (s_transit.home_selected < 0 || s_transit.home_selected >= s_transit.home_count) { s_transit.home_selected = 0; }
  if (s_selected_stop >= s_transit.stop_count) { s_selected_stop = 0; }
  if (!s_transit.home_count) { s_screen = SCREEN_NEARBY; }
  const uint32_t eta_keys[] = {MESSAGE_KEY_ETA_1, MESSAGE_KEY_ETA_2, MESSAGE_KEY_ETA_3};
  const uint32_t live_keys[] = {MESSAGE_KEY_LIVE_1, MESSAGE_KEY_LIVE_2, MESSAGE_KEY_LIVE_3};
  for (int i = 0; i < 3; i++) {
    s_transit.departures[i] = tuple_int(iter, eta_keys[i], s_transit.departures[i]);
    s_transit.live[i] = tuple_int(iter, live_keys[i], s_transit.live[i]);
  }
  const uint32_t stop_name_keys[] = {MESSAGE_KEY_STOP_1_NAME, MESSAGE_KEY_STOP_2_NAME,
    MESSAGE_KEY_STOP_3_NAME, MESSAGE_KEY_STOP_4_NAME, MESSAGE_KEY_STOP_5_NAME};
  const uint32_t stop_time_keys[] = {MESSAGE_KEY_STOP_1_TIME, MESSAGE_KEY_STOP_2_TIME,
    MESSAGE_KEY_STOP_3_TIME, MESSAGE_KEY_STOP_4_TIME, MESSAGE_KEY_STOP_5_TIME};
  for (int i = 0; i < STOP_LIMIT; i++) {
    copy_tuple(iter, stop_name_keys[i], s_transit.stop_names[i], sizeof(s_transit.stop_names[i]));
    s_transit.stop_times[i] = tuple_int(iter, stop_time_keys[i], s_transit.stop_times[i]);
  }
  // Original row keys are contiguous, including the previously unused stop name.
  const uint32_t bases[] = {MESSAGE_KEY_HOME_1_ROUTE, MESSAGE_KEY_HOME_2_ROUTE, MESSAGE_KEY_HOME_3_ROUTE};
  for (int i = 0; i < HOME_ROWS; i++) {
    uint32_t base = bases[i]; HomeRow *row = &s_transit.home[i];
    copy_tuple(iter, base, row->route, sizeof(row->route));
    copy_tuple(iter, base + 1, row->headsign, sizeof(row->headsign));
    copy_tuple(iter, base + 2, row->stop, sizeof(row->stop));
    row->departure = tuple_int(iter, base + 3, row->departure);
    row->live = tuple_int(iter, base + 4, row->live);
    row->color = tuple_int(iter, base + 5, row->color);
    row->text_color = tuple_int(iter, base + 6, row->text_color);
    row->favorite = tuple_int(iter, base + 7, row->favorite);
  }
  s_marker_target = HOME_TOP + s_transit.home_selected * ROW_HEIGHT + ROW_HEIGHT / 2;
  int delta = s_transit.home_start - old_start;
  if (abs(delta) == 1 && s_screen == SCREEN_NEARBY) { s_list_offset = delta * ROW_HEIGHT; }
  else if (delta || old_selected == s_transit.home_selected) { s_marker_y = s_marker_target; }
  if (s_list_offset || s_marker_y != s_marker_target) { animate(); }
  redraw();
}
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update); layer_add_child(root, s_canvas);
}
static void window_unload(Window *window) { layer_destroy(s_canvas); s_canvas = NULL; }
static void init(void) {
  s_transit.route_color = 0x29A66A; s_transit.text_color = 0xFFFFFF; s_transit.loading = true;
  s_connected = connection_service_peek_pebble_app_connection();
  s_window = window_create(); window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){.load = window_load, .unload = window_unload});
  window_set_click_config_provider(s_window, click_config); window_stack_push(s_window, true);
  app_message_register_inbox_received(inbox_received); app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);
  app_message_open(4096, 256);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = connection_changed});
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, refresh_tick, NULL);
  s_startup_timer = app_timer_register(35000, startup_timeout, NULL);
}
static void deinit(void) {
  if (s_refresh_timer) { app_timer_cancel(s_refresh_timer); }
  if (s_animation_timer) { app_timer_cancel(s_animation_timer); }
  if (s_startup_timer) { app_timer_cancel(s_startup_timer); }
  tick_timer_service_unsubscribe(); connection_service_unsubscribe(); window_destroy(s_window);
}
int main(void) { init(); app_event_loop(); deinit(); }
