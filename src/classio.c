#include <pebble.h>
#include "feature_accel_discs.h"
#include "round_math.h"

#define MATH_PI 3.141592653589793238462
#define NUM_DISCS 20
#define DISC_DENSITY 0.25
#define ACCEL_RATIO 0.05
#define ACCEL_STEP_MS 50

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_battery_layer;
static TextLayer *s_connection_layer;
static TextLayer *s_output_layer;
static Layer *s_disc_layer;


#define TAP_NOT_DATA false
static Disc disc_array[NUM_DISCS];
static GRect window_frame;

static double disc_calc_mass(Disc *disc) {
  return MATH_PI * disc->radius * disc->radius * DISC_DENSITY;
}

static void disc_init(Disc *disc) {
  static double next_radius = 3;

  GRect frame = window_frame;
  disc->pos.x = frame.size.w / 2;
  disc->pos.y = frame.size.h / 2;
  disc->vel.x = 0;
  disc->vel.y = 0;
  disc->radius = next_radius;
  disc->mass = disc_calc_mass(disc);
#ifdef PBL_COLOR
  disc->color = GColorFromRGB(rand() % 255, rand() % 255, rand() % 255);
#endif
  next_radius += 0.5;
}

static void disc_apply_force(Disc *disc, Vec2d force) {
  disc->vel.x += force.x / disc->mass;
  disc->vel.y += force.y / disc->mass;
}

static void disc_apply_accel(Disc *disc, AccelData accel) {
  disc_apply_force(disc, (Vec2d) {
    .x = accel.x * ACCEL_RATIO,
    .y = -accel.y * ACCEL_RATIO
  });
}

static void disc_update(Disc *disc) {
  double e = PBL_IF_ROUND_ELSE(0.7, 0.5);

  // update disc position
  disc->pos.x += disc->vel.x;
  disc->pos.y += disc->vel.y;

#ifdef PBL_ROUND
  // -1 accounts for how pixels are drawn onto the screen. Pebble round has a 180x180 pixel screen.
  // Since this is an even number, the centre of the screen is a line separating two side by side
  // pixels. Thus, if you were to draw a pixel at (90, 90), it would show up on the bottom right
  // pixel from the center point of the screen.
  Vec2d circle_center = (Vec2d) { .x = window_frame.size.w / 2 - 1,
                                  .y = window_frame.size.h / 2 - 1 };

  if ((square(circle_center.x - disc->pos.x) + square(circle_center.y - disc->pos.y)) > square(circle_center.x - disc->radius)) {
    // Check to see whether disc is within screen radius
    Vec2d norm = subtract(disc->pos, circle_center);
    if (get_length(norm) > (circle_center.x - disc->radius)) {
      norm = set_length(norm, (circle_center.x - disc->radius), get_length(norm));
      disc->pos = add(circle_center, norm);
    }
    disc->vel = multiply(find_reflection_velocity(circle_center, disc), e);
  }
#else
  if ((disc->pos.x - disc->radius < 0 && disc->vel.x < 0)
    || (disc->pos.x + disc->radius > window_frame.size.w && disc->vel.x > 0)) {
    disc->vel.x = -disc->vel.x * e;
  }

  if ((disc->pos.y - disc->radius < 0 && disc->vel.y < 0)
    || (disc->pos.y + disc->radius > window_frame.size.h && disc->vel.y > 0)) {
    disc->vel.y = -disc->vel.y * e;
  }
#endif
}

static void disc_draw(GContext *ctx, Disc *disc) {
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(disc->color, GColorWhite));

  graphics_fill_circle(ctx, GPoint(disc->pos.x, disc->pos.y), disc->radius);
}

static void disc_layer_update_callback(Layer *me, GContext *ctx) {
  for (int i = 0; i < NUM_DISCS; i++) {
    disc_draw(ctx, &disc_array[i]);
  }
}

static void timer_callback(void *data) {
  AccelData accel = (AccelData) { .x = 0, .y = 0, .z = 0 };
  accel_service_peek(&accel);

  for (int i = 0; i < NUM_DISCS; i++) {
    Disc *disc = &disc_array[i];
    disc_apply_accel(disc, accel);
    disc_update(disc);
  }

  layer_mark_dirty(s_disc_layer);

  app_timer_register(ACCEL_STEP_MS, timer_callback, NULL);
}


static void handle_battery(BatteryChargeState charge_state) {
  static char battery_text[] = "100% charged !";

  if (charge_state.is_charging) {
    snprintf(battery_text, sizeof(battery_text), "charging");
  } else {
    snprintf(battery_text, sizeof(battery_text), "%d%% charged", charge_state.charge_percent);
  }
  text_layer_set_text(s_battery_layer, battery_text);
}

static void handle_second_tick(struct tm* tick_time, TimeUnits units_changed) {
  // Needs to be static because it's used by the system later.
  static char s_time_text[] = "00:00:00";

  strftime(s_time_text, sizeof(s_time_text), "%T", tick_time);
  text_layer_set_text(s_time_layer, s_time_text);
}

static void handle_bluetooth(bool connected) {
  text_layer_set_text(s_connection_layer, connected ? "connected" : "disconnected");
}

static void data_handler(AccelData *data, uint32_t num_samples) {
  // Long lived buffer
  static char s_buffer[128];

  // Compose string of all data
  snprintf(s_buffer, sizeof(s_buffer), 
    "N X,Y,Z\n0 %d,%d,%d\n1 %d,%d,%d\n2 %d,%d,%d", 
    data[0].x, data[0].y, data[0].z, 
    data[1].x, data[1].y, data[1].z, 
    data[2].x, data[2].y, data[2].z
  );

  //Show the data
  text_layer_set_text(s_output_layer, s_buffer);
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  switch (axis) {
  case ACCEL_AXIS_X:
    if (direction > 0) {
      text_layer_set_text(s_output_layer, "X axis positive.");
    } else {
      text_layer_set_text(s_output_layer, "X axis negative.");
    }
    break;
  case ACCEL_AXIS_Y:
    if (direction > 0) {
      text_layer_set_text(s_output_layer, "Y axis positive.");
    } else {
      text_layer_set_text(s_output_layer, "Y axis negative.");
    }
    break;
  case ACCEL_AXIS_Z:
    if (direction > 0) {
      text_layer_set_text(s_output_layer, "Z axis positive.");
    } else {
      text_layer_set_text(s_output_layer, "Z axis negative.");
    }
    break;
  }
}



static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  s_time_layer = text_layer_create(GRect(0, 10, bounds.size.w, 34));
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  s_connection_layer = text_layer_create(GRect(0, 5+40, bounds.size.w, 34));
  text_layer_set_text_color(s_connection_layer, GColorWhite);
  text_layer_set_background_color(s_connection_layer, GColorClear);
  text_layer_set_font(s_connection_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_connection_layer, GTextAlignmentLeft);
#if defined(PBL_SDK_2)
  handle_bluetooth(bluetooth_connection_service_peek());
#elif defined(PBL_SDK_3)
  handle_bluetooth(connection_service_peek_pebble_app_connection());
#endif

  s_battery_layer = text_layer_create(GRect(0, (20+40), bounds.size.w, 34));
  text_layer_set_text_color(s_battery_layer, GColorWhite);
  text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentRight);
  text_layer_set_text(s_battery_layer, "100% charged");

  // Ensures time is displayed immediately (will break if NULL tick event accessed).
  // (This is why it's a good idea to have a separate routine to do the update itself.)
  time_t now = time(NULL);
  struct tm *current_time = localtime(&now);
  handle_second_tick(current_time, SECOND_UNIT);

  tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
  battery_state_service_subscribe(handle_battery);

#if defined(PBL_SDK_2)
  bluetooth_connection_service_subscribe(handle_bluetooth);
#elif defined(PBL_SDK_3)
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = handle_bluetooth
  });
#endif

  
  
  handle_battery(battery_state_service_peek());
  //-------------------
  
   // Create output TextLayer
  s_output_layer = text_layer_create(GRect(0, (40+40), bounds.size.w, 80));
  text_layer_set_text_color(s_output_layer, GColorWhite);
  text_layer_set_background_color(s_output_layer, GColorClear);
  text_layer_set_font(s_output_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_output_layer, GTextAlignmentLeft);
  text_layer_set_text(s_output_layer, "No data yet.");
  
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_connection_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_output_layer));
  
  //-------------------------------
  
  GRect frame = window_frame = layer_get_frame(window_layer);

  s_disc_layer = layer_create(frame);
  layer_set_update_proc(s_disc_layer, disc_layer_update_callback);
  layer_add_child(window_layer, s_disc_layer);

  for (int i = 0; i < NUM_DISCS; i++) {
    disc_init(&disc_array[i]);
  }
  
}

static void main_window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
#if defined(PBL_SDK_2)
  bluetooth_connection_service_unsubscribe();
#elif defined(PBL_SDK_3)
  connection_service_unsubscribe();
#endif
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_connection_layer);
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_output_layer);
  
  layer_destroy(s_disc_layer);
}


static void init() {
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);
  
  // Use tap service? If not, use data service
  if (TAP_NOT_DATA) {
    // Subscribe to the accelerometer tap service
    accel_tap_service_subscribe(tap_handler);
  } else {
    // Subscribe to the accelerometer data service
    int num_samples = 1;
    accel_data_service_subscribe( num_samples, data_handler);

    // Choose update rate
    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
  }
  //---------------------------
  accel_data_service_subscribe(0, NULL);

  app_timer_register(ACCEL_STEP_MS, timer_callback, NULL);
  
  
}

static void deinit() {
  window_destroy(s_main_window);
  

  if (TAP_NOT_DATA) {
    accel_tap_service_unsubscribe();
  } else {
    accel_data_service_unsubscribe();
  }
  
    accel_data_service_unsubscribe();

}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
