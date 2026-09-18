#include <pebble.h>

#define STOP_LIMIT 5
#define HOME_ROWS 3
#define REFRESH_INTERVAL_MS (60 * 1000)
#define ANIMATION_INTERVAL_MS 80
#define SIGNAL_STEP_TICKS 7

typedef struct {
  char route_name[16];
  char route_long_name[40];
  char headsign[40];
  char stop_name[48];
  char mode_name[24];
  char updated[14];
  char error[80];
  char stop_names[STOP_LIMIT][38];
  char home_route_names[HOME_ROWS][16];
  char home_headsigns[HOME_ROWS][40];
  char home_stop_names[HOME_ROWS][48];
  int32_t departures[3];
  int32_t stop_times[STOP_LIMIT];
  int32_t home_departures[HOME_ROWS];
  uint32_t home_colors[HOME_ROWS];
  uint32_t home_text_colors[HOME_ROWS];
  uint32_t route_color;
  uint32_t text_color;
  int line_index;
  int line_count;
  int mode_code;
  int direction_index;
  int direction_count;
  int departure_index;
  int stop_count;
  int home_count;
  int home_selected;
  int page_index;
  int page_count;
  bool live[3];
  bool home_live[HOME_ROWS];
  bool home_favorite[HOME_ROWS];
  bool favorite;
  bool loading;
  bool detail_active;
} TransitData;

static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_refresh_timer;
static AppTimer *s_animation_timer;
static TransitData s_transit;
static int s_detail_page;
static int s_signal_frame;
static int s_animation_tick;
static int s_slide_offset;
static int s_pending_slide_direction;
static int s_marker_y = 62;
static int s_marker_target_y = 62;
static bool s_marker_ready;

static GColor color_from_hex(uint32_t rgb) {
#ifdef PBL_COLOR
  return (GColor){.argb = (uint8_t)(0xC0 | ((rgb >> 18) & 0x30) |
      ((rgb >> 12) & 0x0C) | ((rgb >> 6) & 0x03))};
#else
  return GColorBlack;
#endif
}

static GColor route_color(void) { return color_from_hex(s_transit.route_color); }
static GColor route_text_color(void) { return color_from_hex(s_transit.text_color); }

static void draw_text(GContext *ctx, const char *text, GRect rect, GFont font,
                      GColor color, GTextAlignment alignment) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, rect, GTextOverflowModeTrailingEllipsis,
                     alignment, NULL);
}

static void format_eta(int32_t epoch, char *buffer, size_t size) {
  if (epoch <= 0) {
    snprintf(buffer, size, "--");
    return;
  }
  int32_t seconds = epoch - (int32_t)time(NULL);
  if (seconds <= 30) {
    snprintf(buffer, size, "NOW");
    return;
  }
  int minutes = (seconds + 59) / 60;
  if (minutes < 60) {
    snprintf(buffer, size, "%d min", minutes);
    return;
  }
  time_t value = (time_t)epoch;
  struct tm *local = localtime(&value);
  strftime(buffer, size, clock_is_24h_style() ? "%H:%M" : "%I:%M", local);
  if (!clock_is_24h_style() && buffer[0] == '0') {
    memmove(buffer, buffer + 1, strlen(buffer));
  }
}

static void draw_live_signal(GContext *ctx, GPoint center, GColor color, int frame) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(center.x - 5, center.y + 5), 2);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);
  int phase = frame % 6;
  int visible = phase < 4 ? phase : 6 - phase;
  if (visible < 1) { visible = 1; }
  if (visible >= 1) {
    graphics_draw_arc(ctx, GRect(center.x - 10, center.y, 11, 11),
                      GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(360));
  }
  if (visible >= 2) {
    graphics_draw_arc(ctx, GRect(center.x - 13, center.y - 3, 17, 17),
                      GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(360));
  }
  if (visible >= 3) {
    graphics_draw_arc(ctx, GRect(center.x - 16, center.y - 6, 23, 23),
                      GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(270), DEG_TO_TRIGANGLE(360));
  }
}

static void draw_spinner(GContext *ctx, GPoint center) {
  static const int8_t x[] = {0, 5, 7, 5, 0, -5, -7, -5};
  static const int8_t y[] = {-7, -5, 0, 5, 7, 5, 0, -5};
  for (int i = 0; i < 8; i++) {
    int age = (i - s_signal_frame + 16) % 8;
    graphics_context_set_fill_color(ctx, age < 2 ? GColorWhite : GColorDarkGray);
    graphics_fill_circle(ctx, GPoint(center.x + x[i], center.y + y[i]), age < 2 ? 2 : 1);
  }
}

static void draw_vehicle_icon(GContext *ctx, GPoint p, int mode, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);

  if (mode == 4) {
    // Ferry: stacked cabin, bow, and two small waves.
    graphics_draw_rect(ctx, GRect(p.x - 8, p.y - 7, 16, 7));
    graphics_draw_rect(ctx, GRect(p.x - 12, p.y, 24, 5));
    graphics_draw_line(ctx, GPoint(p.x - 14, p.y + 5), GPoint(p.x - 8, p.y + 11));
    graphics_draw_line(ctx, GPoint(p.x - 8, p.y + 11), GPoint(p.x + 8, p.y + 11));
    graphics_draw_line(ctx, GPoint(p.x + 8, p.y + 11), GPoint(p.x + 14, p.y + 5));
    graphics_draw_line(ctx, GPoint(p.x - 13, p.y + 14), GPoint(p.x - 7, p.y + 12));
    graphics_draw_line(ctx, GPoint(p.x - 7, p.y + 12), GPoint(p.x, p.y + 14));
    graphics_draw_line(ctx, GPoint(p.x, p.y + 14), GPoint(p.x + 7, p.y + 12));
    graphics_draw_line(ctx, GPoint(p.x + 7, p.y + 12), GPoint(p.x + 13, p.y + 14));
    graphics_draw_line(ctx, GPoint(p.x, p.y - 7), GPoint(p.x, p.y - 12));
    return;
  }
  if (mode == 5 || mode == 6 || mode == 7) {
    // Cable car / gondola silhouette inspired by the supplied pixel references.
    graphics_draw_line(ctx, GPoint(p.x - 15, p.y - 12), GPoint(p.x + 15, p.y - 12));
    graphics_fill_rect(ctx, GRect(p.x - 2, p.y - 12, 5, 7), 0, GCornerNone);
    graphics_draw_round_rect(ctx, GRect(p.x - 11, p.y - 5, 23, 19), 4);
    graphics_draw_rect(ctx, GRect(p.x - 7, p.y - 1, 6, 8));
    graphics_draw_rect(ctx, GRect(p.x + 2, p.y - 1, 6, 8));
    graphics_fill_circle(ctx, GPoint(p.x - 6, p.y + 10), 2);
    graphics_fill_circle(ctx, GPoint(p.x + 7, p.y + 10), 2);
    return;
  }
  if (mode == 1) {
    // Metro train emerging from a tunnel.
    graphics_draw_arc(ctx, GRect(p.x - 15, p.y - 14, 30, 30),
                      GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(180), DEG_TO_TRIGANGLE(360));
    graphics_draw_round_rect(ctx, GRect(p.x - 10, p.y - 7, 21, 20), 4);
    graphics_draw_rect(ctx, GRect(p.x - 6, p.y - 3, 5, 7));
    graphics_draw_rect(ctx, GRect(p.x + 2, p.y - 3, 5, 7));
    graphics_fill_circle(ctx, GPoint(p.x - 6, p.y + 9), 2);
    graphics_fill_circle(ctx, GPoint(p.x + 7, p.y + 9), 2);
    graphics_draw_line(ctx, GPoint(p.x - 14, p.y + 15), GPoint(p.x + 14, p.y + 15));
    return;
  }

  if (mode == 9) {
    graphics_draw_line(ctx, GPoint(p.x - 15, p.y - 10), GPoint(p.x + 15, p.y - 10));
    graphics_draw_round_rect(ctx, GRect(p.x - 12, p.y - 6, 25, 15), 6);
    graphics_draw_rect(ctx, GRect(p.x - 8, p.y - 3, 6, 6));
    graphics_draw_rect(ctx, GRect(p.x + 3, p.y - 3, 6, 6));
    graphics_fill_rect(ctx, GRect(p.x - 2, p.y + 9, 5, 7), 0, GCornerNone);
    return;
  }

  // Bus, rail, light rail, and trolleybus share a crisp front-facing body.
  graphics_draw_round_rect(ctx, GRect(p.x - 11, p.y - 12, 23, 25), mode == 3 ? 3 : 6);
  graphics_draw_rect(ctx, GRect(p.x - 7, p.y - 7, 6, 8));
  graphics_draw_rect(ctx, GRect(p.x + 2, p.y - 7, 6, 8));
  graphics_fill_rect(ctx, GRect(p.x - 7, p.y + 5, 4, 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(p.x + 4, p.y + 5, 4, 3), 0, GCornerNone);
  graphics_fill_circle(ctx, GPoint(p.x - 6, p.y + 13), 2);
  graphics_fill_circle(ctx, GPoint(p.x + 7, p.y + 13), 2);
  if (mode == 0 || mode == 8) {
    graphics_draw_line(ctx, GPoint(p.x - 6, p.y - 12), GPoint(p.x, p.y - 17));
    graphics_draw_line(ctx, GPoint(p.x, p.y - 17), GPoint(p.x + 6, p.y - 12));
  }
  if (mode == 2) {
    graphics_draw_line(ctx, GPoint(p.x - 7, p.y + 16), GPoint(p.x - 2, p.y + 12));
    graphics_draw_line(ctx, GPoint(p.x + 8, p.y + 16), GPoint(p.x + 3, p.y + 12));
  }
}

static void draw_error(GContext *ctx, GRect bounds) {
  if (!s_transit.error[0]) { return; }
  graphics_context_set_fill_color(ctx, GColorBulgarianRose);
  graphics_fill_rect(ctx, GRect(0, bounds.size.h - 31, bounds.size.w, 31), 0, GCornerNone);
  draw_text(ctx, s_transit.error, GRect(6, bounds.size.h - 30, bounds.size.w - 12, 28),
            fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorWhite,
            GTextAlignmentCenter);
}

static void draw_home(GContext *ctx, GRect bounds) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  for (int i = 0; i < s_transit.home_count && i < HOME_ROWS; i++) {
    int y = i * 76 + s_slide_offset;
    GColor accent = color_from_hex(s_transit.home_colors[i]);
    GColor ink = color_from_hex(s_transit.home_text_colors[i]);
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_rect(ctx, GRect(0, y, bounds.size.w, 76), 0, GCornerNone);

    graphics_context_set_fill_color(ctx, ink);
    graphics_fill_circle(ctx, GPoint(23, y + 28), 18);
    draw_text(ctx, s_transit.home_route_names[i], GRect(7, y + 14, 33, 28),
              fonts_get_system_font(strlen(s_transit.home_route_names[i]) > 3 ?
                FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_24_BOLD),
              accent, GTextAlignmentCenter);

    draw_text(ctx, s_transit.home_headsigns[i], GRect(47, y + 7, 105, 25),
              fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), ink,
              GTextAlignmentLeft);
    draw_text(ctx, s_transit.home_stop_names[i], GRect(47, y + 31, 108, 22),
              fonts_get_system_font(FONT_KEY_GOTHIC_14), ink,
              GTextAlignmentLeft);

    char eta[14];
    format_eta(s_transit.home_departures[i], eta, sizeof(eta));
    draw_text(ctx, eta, GRect(151, y + 10, 43, 30),
              fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), ink,
              GTextAlignmentRight);
    if (s_transit.home_live[i]) {
      draw_live_signal(ctx, GPoint(184, y + 48), ink, s_signal_frame);
    } else {
      draw_text(ctx, "S", GRect(174, y + 42, 18, 18),
                fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ink,
                GTextAlignmentCenter);
    }
    if (s_transit.home_favorite[i]) {
      draw_text(ctx, "◆", GRect(4, y + 2, 14, 16),
                fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ink,
                GTextAlignmentLeft);
    }

    graphics_context_set_stroke_color(ctx, ink);
    if (i == s_transit.home_selected) {
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_round_rect(ctx, GRect(2, y + 2, bounds.size.w - 4, 72), 7);
    } else {
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_line(ctx, GPoint(5, y + 75), GPoint(bounds.size.w - 5, y + 75));
    }
  }

  if (s_transit.home_count > 0) {
    int marker_y = s_marker_y + s_slide_offset;
    GColor marker_color = color_from_hex(
      s_transit.home_text_colors[s_transit.home_selected]);
    graphics_context_set_fill_color(ctx, marker_color);
    graphics_fill_circle(ctx, GPoint(44, marker_y), 3);
    graphics_fill_rect(ctx, GRect(40, marker_y - 1, 4, 3), 1, GCornersAll);
  }

  char page[24];
  snprintf(page, sizeof(page), "%d/%d", s_transit.page_index + 1,
           s_transit.page_count > 0 ? s_transit.page_count : 1);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(158, 209, 36, 17), 4, GCornersAll);
  draw_text(ctx, page, GRect(159, 207, 33, 18),
            fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GColorWhite,
            GTextAlignmentRight);
  if (s_transit.loading) { draw_spinner(ctx, GPoint(190, 9)); }
  draw_error(ctx, bounds);
}

static void draw_detail_header(GContext *ctx, GRect bounds, const char *label) {
  GColor accent = route_color();
  GColor ink = route_text_color();
  graphics_context_set_fill_color(ctx, accent);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, 53), 0, GCornerNone);
  draw_vehicle_icon(ctx, GPoint(18, 27), s_transit.mode_code, ink);
  draw_text(ctx, s_transit.route_name, GRect(37, 4, 58, 26),
            fonts_get_system_font(strlen(s_transit.route_name) > 4 ?
              FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_24_BOLD), ink,
            GTextAlignmentLeft);
  draw_text(ctx, label, GRect(96, 5, 94, 18),
            fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ink,
            GTextAlignmentRight);
  draw_text(ctx, s_transit.headsign, GRect(40, 28, 150, 20),
            fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ink,
            GTextAlignmentLeft);
}

static void draw_page_rail(GContext *ctx, int active) {
  for (int i = 0; i < 2; i++) {
    graphics_context_set_fill_color(ctx, i == active ? route_color() : GColorLightGray);
    graphics_fill_circle(ctx, GPoint(193, 106 + i * 17), i == active ? 3 : 2);
  }
}

static void draw_departures_page(GContext *ctx, GRect bounds) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  draw_detail_header(ctx, bounds, "NEXT 3");
  draw_text(ctx, s_transit.stop_name, GRect(8, 56, bounds.size.w - 16, 20),
            fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorDarkGray,
            GTextAlignmentCenter);

  for (int i = 0; i < 3; i++) {
    int y = 79 + i * 40;
    bool selected = i == s_transit.departure_index;
    GColor fill = selected ? route_color() : GColorLightGray;
    GColor ink = selected ? route_text_color() : GColorBlack;
    graphics_context_set_fill_color(ctx, fill);
    graphics_fill_rect(ctx, GRect(10, y, 178, 34), 8, GCornersAll);
    char ordinal[4];
    snprintf(ordinal, sizeof(ordinal), "%d", i + 1);
    draw_text(ctx, ordinal, GRect(18, y + 6, 20, 20),
              fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ink,
              GTextAlignmentCenter);
    char eta[16];
    format_eta(s_transit.departures[i], eta, sizeof(eta));
    draw_text(ctx, eta, GRect(45, y + 1, 96, 30),
              fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), ink,
              GTextAlignmentLeft);
    if (s_transit.live[i]) {
      draw_live_signal(ctx, GPoint(170, y + 11), ink, s_signal_frame);
    } else {
      draw_text(ctx, "S", GRect(157, y + 7, 20, 18),
                fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), ink,
                GTextAlignmentCenter);
    }
  }
  draw_text(ctx, "SELECT next  •  UP/DOWN stops", GRect(7, 204, 184, 19),
            fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorDarkGray,
            GTextAlignmentCenter);
  draw_page_rail(ctx, 0);
  draw_error(ctx, bounds);
}

static void draw_stops_page(GContext *ctx, GRect bounds) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  char header[40];
  char eta[16];
  format_eta(s_transit.departures[s_transit.departure_index], eta, sizeof(eta));
  snprintf(header, sizeof(header), "TRIP %d • %s", s_transit.departure_index + 1, eta);
  draw_detail_header(ctx, bounds, header);

  if (s_transit.stop_count == 0) {
    if (s_transit.loading) {
      draw_spinner(ctx, GPoint(100, 112));
      draw_text(ctx, "Loading trip stops…", GRect(15, 128, 170, 24),
                fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorDarkGray,
                GTextAlignmentCenter);
    } else {
      draw_text(ctx, "Stop times unavailable", GRect(15, 100, 170, 28),
                fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorDarkGray,
                GTextAlignmentCenter);
    }
  } else {
    graphics_context_set_stroke_color(ctx, route_color());
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(18, 67), GPoint(18, 204));
    for (int i = 0; i < s_transit.stop_count && i < STOP_LIMIT; i++) {
      int y = 59 + i * 30;
      graphics_context_set_fill_color(ctx, i == 0 ? route_color() : GColorWhite);
      graphics_fill_circle(ctx, GPoint(18, y + 10), i == 0 ? 5 : 4);
      graphics_context_set_stroke_color(ctx, route_color());
      graphics_draw_circle(ctx, GPoint(18, y + 10), 4);
      draw_text(ctx, s_transit.stop_names[i], GRect(30, y, 113, 25),
                fonts_get_system_font(i == 0 ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_14),
                GColorBlack, GTextAlignmentLeft);
      char stop_eta[14];
      format_eta(s_transit.stop_times[i], stop_eta, sizeof(stop_eta));
      draw_text(ctx, stop_eta, GRect(144, y + 2, 44, 20),
                fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), route_color(),
                GTextAlignmentRight);
    }
  }
  draw_text(ctx, "UP/DOWN board  •  SELECT next", GRect(7, 207, 184, 17),
            fonts_get_system_font(FONT_KEY_GOTHIC_14), GColorDarkGray,
            GTextAlignmentCenter);
  draw_page_rail(ctx, 1);
  draw_error(ctx, bounds);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  if (!s_transit.detail_active) { draw_home(ctx, bounds); }
  else if (s_detail_page == 0) { draw_departures_page(ctx, bounds); }
  else { draw_stops_page(ctx, bounds); }
}

static void animation_timer_callback(void *context) {
  s_animation_tick++;
  if (s_animation_tick % SIGNAL_STEP_TICKS == 0) {
    s_signal_frame = (s_signal_frame + 1) % 24;
  }
  int slide_step = abs(s_slide_offset) / 3;
  if (slide_step < 2) { slide_step = 2; }
  if (s_slide_offset > 0) {
    s_slide_offset -= slide_step;
    if (s_slide_offset < 0) { s_slide_offset = 0; }
  }
  if (s_slide_offset < 0) {
    s_slide_offset += slide_step;
    if (s_slide_offset > 0) { s_slide_offset = 0; }
  }
  int marker_delta = s_marker_target_y - s_marker_y;
  if (marker_delta != 0) {
    int marker_step = abs(marker_delta) / 3;
    if (marker_step < 2) { marker_step = 2; }
    if (marker_delta > 0) {
      s_marker_y += marker_step;
      if (s_marker_y > s_marker_target_y) { s_marker_y = s_marker_target_y; }
    } else {
      s_marker_y -= marker_step;
      if (s_marker_y < s_marker_target_y) { s_marker_y = s_marker_target_y; }
    }
  }
  if (s_canvas) { layer_mark_dirty(s_canvas); }
  s_animation_timer = app_timer_register(ANIMATION_INTERVAL_MS, animation_timer_callback, NULL);
}

static void send_request(uint32_t key, int8_t value) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_int8(out, key, value);
    app_message_outbox_send();
  }
}

static void move_selection(int delta) {
  s_pending_slide_direction = delta;
  s_transit.error[0] = '\0';
  send_request(MESSAGE_KEY_REQUEST_ROW_DELTA, delta);
  layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  if (s_transit.detail_active) {
    s_detail_page = (s_detail_page + 1) % 2;
    layer_mark_dirty(s_canvas);
  } else { move_selection(-1); }
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  if (s_transit.detail_active) {
    s_detail_page = (s_detail_page + 1) % 2;
    layer_mark_dirty(s_canvas);
  } else { move_selection(1); }
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  s_transit.error[0] = '\0';
  if (s_transit.detail_active) {
    s_transit.loading = true;
    send_request(MESSAGE_KEY_REQUEST_DEPARTURE, 1);
  } else {
    send_request(MESSAGE_KEY_REQUEST_DIRECTION, 1);
  }
  layer_mark_dirty(s_canvas);
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_transit.detail_active) { return; }
  s_transit.detail_active = true;
  s_detail_page = 0;
  s_transit.departure_index = 0;
  s_transit.stop_count = 0;
  s_transit.loading = true;
  send_request(MESSAGE_KEY_REQUEST_DETAILS, 1);
  vibes_short_pulse();
  layer_mark_dirty(s_canvas);
}

static void up_long_click(ClickRecognizerRef recognizer, void *context) {
  if (s_transit.detail_active) { return; }
  s_transit.favorite = !s_transit.favorite;
  if (s_transit.home_selected >= 0 && s_transit.home_selected < HOME_ROWS) {
    s_transit.home_favorite[s_transit.home_selected] = s_transit.favorite;
  }
  send_request(MESSAGE_KEY_REQUEST_TOGGLE_FAVORITE, 1);
  vibes_short_pulse();
  layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef recognizer, void *context) {
  if (s_transit.detail_active) {
    s_transit.detail_active = false;
    s_detail_page = 0;
    send_request(MESSAGE_KEY_REQUEST_DETAILS, 2);
    layer_mark_dirty(s_canvas);
  } else {
    window_stack_pop(true);
  }
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_long_click_subscribe(BUTTON_ID_UP, 650, up_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 650, select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

static void refresh_timer_callback(void *context) {
  send_request(MESSAGE_KEY_REQUEST_REFRESH, 2);
  s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, refresh_timer_callback, NULL);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (s_canvas) { layer_mark_dirty(s_canvas); }
}

static void copy_tuple(DictionaryIterator *iter, uint32_t key, char *dest, size_t size) {
  Tuple *tuple = dict_find(iter, key);
  if (tuple && tuple->type == TUPLE_CSTRING) {
    snprintf(dest, size, "%s", tuple->value->cstring);
  }
}

static int32_t tuple_int(DictionaryIterator *iter, uint32_t key, int32_t fallback) {
  Tuple *tuple = dict_find(iter, key);
  return tuple ? tuple->value->int32 : fallback;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  int previous_page_index = s_transit.page_index;
  int previous_home_selected = s_transit.home_selected;
  copy_tuple(iter, MESSAGE_KEY_ROUTE_NAME, s_transit.route_name, sizeof(s_transit.route_name));
  copy_tuple(iter, MESSAGE_KEY_ROUTE_LONG_NAME, s_transit.route_long_name, sizeof(s_transit.route_long_name));
  copy_tuple(iter, MESSAGE_KEY_HEADSIGN, s_transit.headsign, sizeof(s_transit.headsign));
  copy_tuple(iter, MESSAGE_KEY_STOP_NAME, s_transit.stop_name, sizeof(s_transit.stop_name));
  copy_tuple(iter, MESSAGE_KEY_MODE_NAME, s_transit.mode_name, sizeof(s_transit.mode_name));
  copy_tuple(iter, MESSAGE_KEY_UPDATED_AT, s_transit.updated, sizeof(s_transit.updated));
  copy_tuple(iter, MESSAGE_KEY_ERROR_MESSAGE, s_transit.error, sizeof(s_transit.error));

  s_transit.line_index = tuple_int(iter, MESSAGE_KEY_LINE_INDEX, s_transit.line_index);
  s_transit.line_count = tuple_int(iter, MESSAGE_KEY_LINE_COUNT, s_transit.line_count);
  s_transit.mode_code = tuple_int(iter, MESSAGE_KEY_MODE_CODE, s_transit.mode_code);
  s_transit.route_color = (uint32_t)tuple_int(iter, MESSAGE_KEY_ROUTE_COLOR, s_transit.route_color);
  s_transit.text_color = (uint32_t)tuple_int(iter, MESSAGE_KEY_ROUTE_TEXT_COLOR, s_transit.text_color);
  s_transit.favorite = tuple_int(iter, MESSAGE_KEY_IS_FAVORITE, s_transit.favorite) != 0;
  s_transit.departures[0] = tuple_int(iter, MESSAGE_KEY_ETA_1, s_transit.departures[0]);
  s_transit.departures[1] = tuple_int(iter, MESSAGE_KEY_ETA_2, s_transit.departures[1]);
  s_transit.departures[2] = tuple_int(iter, MESSAGE_KEY_ETA_3, s_transit.departures[2]);
  s_transit.live[0] = tuple_int(iter, MESSAGE_KEY_LIVE_1, s_transit.live[0]) != 0;
  s_transit.live[1] = tuple_int(iter, MESSAGE_KEY_LIVE_2, s_transit.live[1]) != 0;
  s_transit.live[2] = tuple_int(iter, MESSAGE_KEY_LIVE_3, s_transit.live[2]) != 0;
  s_transit.loading = tuple_int(iter, MESSAGE_KEY_IS_LOADING, s_transit.loading) != 0;
  s_transit.direction_index = tuple_int(iter, MESSAGE_KEY_DIRECTION_INDEX, s_transit.direction_index);
  s_transit.direction_count = tuple_int(iter, MESSAGE_KEY_DIRECTION_COUNT, s_transit.direction_count);
  s_transit.detail_active = tuple_int(iter, MESSAGE_KEY_DETAIL_ACTIVE, s_transit.detail_active) != 0;
  s_transit.departure_index = tuple_int(iter, MESSAGE_KEY_DEPARTURE_INDEX, s_transit.departure_index);
  s_transit.stop_count = tuple_int(iter, MESSAGE_KEY_STOP_COUNT, s_transit.stop_count);
  s_transit.home_count = tuple_int(iter, MESSAGE_KEY_HOME_COUNT, s_transit.home_count);
  s_transit.home_selected = tuple_int(iter, MESSAGE_KEY_HOME_SELECTED, s_transit.home_selected);
  s_transit.page_index = tuple_int(iter, MESSAGE_KEY_PAGE_INDEX, s_transit.page_index);
  s_transit.page_count = tuple_int(iter, MESSAGE_KEY_PAGE_COUNT, s_transit.page_count);
  if (s_transit.departure_index < 0 || s_transit.departure_index > 2) {
    s_transit.departure_index = 0;
  }
  if (s_transit.stop_count < 0) { s_transit.stop_count = 0; }
  if (s_transit.stop_count > STOP_LIMIT) { s_transit.stop_count = STOP_LIMIT; }
  if (s_transit.home_count < 0) { s_transit.home_count = 0; }
  if (s_transit.home_count > HOME_ROWS) { s_transit.home_count = HOME_ROWS; }
  if (s_transit.home_selected < 0 || s_transit.home_selected >= s_transit.home_count) {
    s_transit.home_selected = 0;
  }
  s_marker_target_y = s_transit.home_selected * 76 + 62;
  if (!s_marker_ready || s_transit.page_index != previous_page_index) {
    s_marker_y = s_marker_target_y;
    s_marker_ready = true;
  } else if (s_transit.home_selected == previous_home_selected) {
    s_marker_y = s_marker_target_y;
  }
  if (s_transit.page_index != previous_page_index && s_pending_slide_direction != 0) {
    s_slide_offset = s_pending_slide_direction > 0 ? 228 : -228;
  }
  s_pending_slide_direction = 0;

  const uint32_t name_keys[STOP_LIMIT] = {
    MESSAGE_KEY_STOP_1_NAME, MESSAGE_KEY_STOP_2_NAME, MESSAGE_KEY_STOP_3_NAME,
    MESSAGE_KEY_STOP_4_NAME, MESSAGE_KEY_STOP_5_NAME
  };
  const uint32_t time_keys[STOP_LIMIT] = {
    MESSAGE_KEY_STOP_1_TIME, MESSAGE_KEY_STOP_2_TIME, MESSAGE_KEY_STOP_3_TIME,
    MESSAGE_KEY_STOP_4_TIME, MESSAGE_KEY_STOP_5_TIME
  };
  for (int i = 0; i < STOP_LIMIT; i++) {
    copy_tuple(iter, name_keys[i], s_transit.stop_names[i], sizeof(s_transit.stop_names[i]));
    s_transit.stop_times[i] = tuple_int(iter, time_keys[i], s_transit.stop_times[i]);
  }

  const uint32_t home_route_keys[HOME_ROWS] = {
    MESSAGE_KEY_HOME_1_ROUTE, MESSAGE_KEY_HOME_2_ROUTE, MESSAGE_KEY_HOME_3_ROUTE
  };
  const uint32_t home_headsign_keys[HOME_ROWS] = {
    MESSAGE_KEY_HOME_1_HEADSIGN, MESSAGE_KEY_HOME_2_HEADSIGN, MESSAGE_KEY_HOME_3_HEADSIGN
  };
  const uint32_t home_stop_keys[HOME_ROWS] = {
    MESSAGE_KEY_HOME_1_STOP, MESSAGE_KEY_HOME_2_STOP, MESSAGE_KEY_HOME_3_STOP
  };
  const uint32_t home_eta_keys[HOME_ROWS] = {
    MESSAGE_KEY_HOME_1_ETA, MESSAGE_KEY_HOME_2_ETA, MESSAGE_KEY_HOME_3_ETA
  };
  const uint32_t home_live_keys[HOME_ROWS] = {
    MESSAGE_KEY_HOME_1_LIVE, MESSAGE_KEY_HOME_2_LIVE, MESSAGE_KEY_HOME_3_LIVE
  };
  const uint32_t home_color_keys[HOME_ROWS] = {
    MESSAGE_KEY_HOME_1_COLOR, MESSAGE_KEY_HOME_2_COLOR, MESSAGE_KEY_HOME_3_COLOR
  };
  const uint32_t home_text_color_keys[HOME_ROWS] = {
    MESSAGE_KEY_HOME_1_TEXT_COLOR, MESSAGE_KEY_HOME_2_TEXT_COLOR,
    MESSAGE_KEY_HOME_3_TEXT_COLOR
  };
  const uint32_t home_favorite_keys[HOME_ROWS] = {
    MESSAGE_KEY_HOME_1_FAVORITE, MESSAGE_KEY_HOME_2_FAVORITE,
    MESSAGE_KEY_HOME_3_FAVORITE
  };
  for (int i = 0; i < HOME_ROWS; i++) {
    copy_tuple(iter, home_route_keys[i], s_transit.home_route_names[i],
               sizeof(s_transit.home_route_names[i]));
    copy_tuple(iter, home_headsign_keys[i], s_transit.home_headsigns[i],
               sizeof(s_transit.home_headsigns[i]));
    copy_tuple(iter, home_stop_keys[i], s_transit.home_stop_names[i],
               sizeof(s_transit.home_stop_names[i]));
    s_transit.home_departures[i] = tuple_int(iter, home_eta_keys[i],
                                             s_transit.home_departures[i]);
    s_transit.home_live[i] = tuple_int(iter, home_live_keys[i],
                                       s_transit.home_live[i]) != 0;
    s_transit.home_colors[i] = (uint32_t)tuple_int(iter, home_color_keys[i],
                                                   s_transit.home_colors[i]);
    s_transit.home_text_colors[i] = (uint32_t)tuple_int(iter,
      home_text_color_keys[i], s_transit.home_text_colors[i]);
    s_transit.home_favorite[i] = tuple_int(iter, home_favorite_keys[i],
                                            s_transit.home_favorite[i]) != 0;
  }
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  memset(&s_transit, 0, sizeof(s_transit));
  snprintf(s_transit.route_name, sizeof(s_transit.route_name), "TRANSIT");
  snprintf(s_transit.route_long_name, sizeof(s_transit.route_long_name), "Nearby lines");
  snprintf(s_transit.headsign, sizeof(s_transit.headsign), "Finding your location");
  snprintf(s_transit.stop_name, sizeof(s_transit.stop_name), "Open settings on your phone");
  snprintf(s_transit.mode_name, sizeof(s_transit.mode_name), "Pebble Transit");
  snprintf(s_transit.updated, sizeof(s_transit.updated), "--");
  s_transit.route_color = 0x29A66A;
  s_transit.text_color = 0xFFFFFF;
  s_transit.line_count = 1;
  s_transit.home_count = 1;
  s_transit.page_count = 1;
  s_transit.home_colors[0] = s_transit.route_color;
  s_transit.home_text_colors[0] = s_transit.text_color;
  snprintf(s_transit.home_route_names[0], sizeof(s_transit.home_route_names[0]), "T");
  snprintf(s_transit.home_headsigns[0], sizeof(s_transit.home_headsigns[0]), "Nearby lines");
  snprintf(s_transit.home_stop_names[0], sizeof(s_transit.home_stop_names[0]),
           "Finding your location");
  s_transit.loading = true;

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload
  });
  window_set_click_config_provider(s_window, click_config);
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_open(2048, 256);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, refresh_timer_callback, NULL);
  s_animation_timer = app_timer_register(ANIMATION_INTERVAL_MS, animation_timer_callback, NULL);
}

static void deinit(void) {
  if (s_refresh_timer) { app_timer_cancel(s_refresh_timer); }
  if (s_animation_timer) { app_timer_cancel(s_animation_timer); }
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
