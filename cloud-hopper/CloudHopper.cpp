#include "CloudHopper.h"

#include "components/motor/MotorController.h"

#include <cstdio>

using namespace Pinetime::Applications::Screens;

namespace {
  // per frame (20 ms), in 1/256 px
  constexpr int32_t gravity = 64;         // 0.25 px/frame^2
  constexpr int32_t bounceSpeed = -1613;  // -6.3 px/frame: reaches 1613^2 / (2 * 64) / 256 = 79 px above a cloud
  constexpr int32_t steerAccel = 141;     // 0.55 px/frame^2
  constexpr int32_t maxSteer = 870;       // 3.4 px/frame
  constexpr int32_t frictionNum = 210;    // speed *= 210/256 (0.82) per frame when not steering
  constexpr uint32_t holdMs = 70;      // touch counts as held if seen this recently
  constexpr uint32_t retryDelayMs = 600;

  uint32_t best = 0; // kept while the watch is running

  const lv_color_t plainColour = LV_COLOR_WHITE;
  const lv_color_t driftColour = lv_color_hex(0xbde0fe);
  const lv_color_t puffColour = lv_color_hex(0x9aa5b1);

  lv_obj_t* Rect(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_color_t colour, lv_coord_t radius) {
    lv_obj_t* o = lv_obj_create(parent, nullptr);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_local_bg_color(o, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, colour);
    lv_obj_set_style_local_radius(o, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, radius);
    lv_obj_set_style_local_border_width(o, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_click(o, false);
    return o;
  }

  // A dark rounded panel that sizes itself around a centred label, so text stays readable over the game.
  lv_obj_t* Panel(lv_obj_t** label) {
    lv_obj_t* panel = lv_cont_create(lv_scr_act(), nullptr);
    lv_cont_set_layout(panel, LV_LAYOUT_CENTER);
    lv_cont_set_fit(panel, LV_FIT_TIGHT);
    lv_obj_set_style_local_bg_color(panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x0b1526));
    lv_obj_set_style_local_bg_opa(panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_80);
    lv_obj_set_style_local_radius(panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 10);
    lv_obj_set_style_local_border_width(panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_pad_top(panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_pad_bottom(panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_pad_left(panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 10);
    lv_obj_set_style_local_pad_right(panel, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 10);
    lv_obj_set_click(panel, false);
    *label = lv_label_create(panel, nullptr);
    lv_label_set_align(*label, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_style_local_text_color(*label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    return panel;
  }
}

CloudHopper::CloudHopper(Controllers::MotorController& motor, System::SystemTask& systemTask)
  : motor {motor}, wakeLock(systemTask) {
  lv_obj_t* sky = Rect(lv_scr_act(), LV_HOR_RES, LV_VER_RES, lv_color_hex(0x3a7bd5), 0);
  lv_obj_set_pos(sky, 0, 0);

  for (auto& c : clouds) {
    c.obj = Rect(lv_scr_act(), cloudW, cloudH, plainColour, LV_RADIUS_CIRCLE);
  }

  // The blob: a jelly body with two eyes.
  blob = Rect(lv_scr_act(), blobW, blobH, lv_color_hex(0x52d273), 9);
  lv_obj_set_style_local_border_width(blob, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
  lv_obj_set_style_local_border_color(blob, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x2e8b57));
  for (lv_coord_t ex : {5, 12}) {
    lv_obj_t* eye = Rect(blob, 5, 6, LV_COLOR_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(eye, ex, 4);
    lv_obj_t* pupil = Rect(eye, 3, 3, lv_color_hex(0x1b1b1b), LV_RADIUS_CIRCLE);
    lv_obj_set_pos(pupil, 1, 2);
  }

  scorePanel = Panel(&scoreLabel);
  messagePanel = Panel(&message);

  Reset();
  refreshTask = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

CloudHopper::~CloudHopper() {
  lv_task_del(refreshTask);
  lv_obj_clean(lv_scr_act());
}

uint32_t CloudHopper::Random() {
  // xorshift32
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

int32_t CloudHopper::Gap() {
  // Vertical distance to the next cloud: grows with height but is always reachable. A bounce
  // rises bounceSpeed^2 / (2 * gravity) = 79 px; the largest gap here is 48 + 27 = 75 px.
  const uint32_t level = height / (600 * FP) > 6 ? 6 : height / (600 * FP);
  const uint32_t lo = 30 + level * 3;
  const uint32_t span = 16 + level * 2;
  return static_cast<int32_t>(lo + Random() % span) * FP;
}

void CloudHopper::PlaceCloud(Cloud& c, int32_t cy) {
  c.y = cy;
  c.x = static_cast<int32_t>(Random() % (LV_HOR_RES - cloudW)) * FP;
  c.gone = false;
  const uint32_t roll = Random() % 100;
  const uint32_t level = height / (600 * FP);
  // drifting and one-bounce "puff" clouds appear as you climb
  if (level >= 1 && roll < 12 + level * 2) {
    c.kind = Kind::Drifting;
    c.vx = (Random() & 1) ? FP : -FP;
  } else if (level >= 2 && roll > 88 - level) {
    c.kind = Kind::Puff;
    c.vx = 0;
  } else {
    c.kind = Kind::Plain;
    c.vx = 0;
  }
  lv_color_t colour = c.kind == Kind::Plain ? plainColour : c.kind == Kind::Drifting ? driftColour : puffColour;
  lv_obj_set_style_local_bg_color(c.obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, colour);
  lv_obj_set_hidden(c.obj, false);
  lv_obj_set_pos(c.obj, static_cast<lv_coord_t>(c.x / FP), static_cast<lv_coord_t>(c.y / FP));
}

void CloudHopper::ShowMessage(const char* text) {
  if (text == nullptr) {
    lv_obj_set_hidden(messagePanel, true);
    return;
  }
  lv_label_set_text(message, text);
  lv_obj_align(messagePanel, nullptr, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_hidden(messagePanel, false);
}

void CloudHopper::UpdateScore() {
  lv_label_set_text_fmt(scoreLabel, "%lu m", static_cast<unsigned long>(height / (10 * FP)));
  lv_obj_align(scorePanel, nullptr, LV_ALIGN_IN_TOP_LEFT, 4, 4);
}

void CloudHopper::Reset() {
  state = State::Ready;
  height = 0;
  vx = 0;
  vy = 0;
  if (rng == 0) {
    rng = lv_tick_get() | 1;
  }
  // a wide, safe first cloud right under the blob, the rest stacked above it
  int32_t cy = (LV_VER_RES - 24) * FP;
  for (size_t i = 0; i < clouds.size(); i++) {
    PlaceCloud(clouds[i], cy);
    if (i == 0) {
      clouds[i].x = (LV_HOR_RES - cloudW) / 2 * FP;
      clouds[i].kind = Kind::Plain;
      clouds[i].vx = 0;
      lv_obj_set_style_local_bg_color(clouds[i].obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, plainColour);
      lv_obj_set_pos(clouds[i].obj, static_cast<lv_coord_t>(clouds[i].x / FP), static_cast<lv_coord_t>(cy / FP));
    }
    cy -= Gap();
  }
  x = (LV_HOR_RES - blobW) / 2 * FP;
  y = clouds[0].y - blobH * FP;
  lv_obj_set_pos(blob, static_cast<lv_coord_t>(x / FP), static_cast<lv_coord_t>(y / FP));
  UpdateScore();
  ShowMessage("CLOUD HOPPER\n\nHold left or right\nto steer\n\nTap to start");
}

void CloudHopper::Fall() {
  state = State::Fallen;
  fellAt = lv_tick_get();
  if (height / (10 * FP) > best) {
    best = height / (10 * FP);
  }
  motor.RunForDuration(60);
  wakeLock.Release();
  static char text[64];
  snprintf(text, sizeof(text), "You fell!\n%lu m  (best %lu m)\n\nTap to try again", static_cast<unsigned long>(height / (10 * FP)),
           static_cast<unsigned long>(best));
  ShowMessage(text);
}

bool CloudHopper::OnTouchEvent(uint16_t tx, uint16_t /*ty*/) {
  const bool newPress = lv_tick_elaps(lastTouch) > holdMs;
  lastTouch = lv_tick_get();
  touchX = tx;
  if (newPress) {
    steering = state == State::Playing;
    Press();
  }
  return true;
}

void CloudHopper::Press() {
  if (state == State::Ready) {
    rng ^= lastTouch;
    state = State::Playing;
    wakeLock.Lock();
    ShowMessage(nullptr);
    vy = bounceSpeed;
  } else if (state == State::Fallen && lv_tick_elaps(fellAt) > retryDelayMs) {
    Reset();
  }
}

bool CloudHopper::OnTouchEvent(TouchEvents event) {
  // A very short tap may end before the touch position is ever read; the tap gesture is the backup
  // for starting and retrying (steering always needs a held touch).
  if (event == TouchEvents::Tap && state != State::Playing && lv_tick_elaps(lastTouch) > 300) {
    lastTouch = lv_tick_get() - holdMs; // counts for de-duplication, but not as a held (steering) touch
    Press();
    return true;
  }
  // Steering means holding and sliding; don't let that turn into "leave the app". Use the button to exit.
  return state == State::Playing || event == TouchEvents::Tap;
}

void CloudHopper::Refresh() {
  // Physics runs in fixed 20 ms steps based on the clock, so the game runs at the same speed
  // however long each frame takes to draw. A long pause (e.g. screen off) is not caught up.
  uint32_t elapsed = lv_tick_elaps(lastStep);
  lastStep = lv_tick_get();
  stepBudget += elapsed > 100 ? 100 : elapsed;
  while (stepBudget >= stepMs) {
    stepBudget -= stepMs;
    Step();
  }
}

void CloudHopper::Step() {
  if (state != State::Playing) {
    return;
  }

  // steering
  if (steering && lv_tick_elaps(lastTouch) < holdMs) {
    vx += touchX < LV_HOR_RES / 2 ? -steerAccel : steerAccel;
    vx = vx > maxSteer ? maxSteer : (vx < -maxSteer ? -maxSteer : vx);
  } else {
    vx = vx * frictionNum / 256;
  }
  x += vx;
  // wrap around the sides
  if (x < -blobW * FP) {
    x += (LV_HOR_RES + blobW) * FP;
  } else if (x > LV_HOR_RES * FP) {
    x -= (LV_HOR_RES + blobW) * FP;
  }

  const int32_t prevBottom = y + blobH * FP;
  vy += gravity;
  y += vy;

  // land on a cloud only while falling, and only if we crossed its top this frame
  if (vy > 0) {
    for (auto& c : clouds) {
      if (c.gone) {
        continue;
      }
      const int32_t bottom = y + blobH * FP;
      if (prevBottom <= c.y + 2 * FP && bottom >= c.y && x + (blobW - 4) * FP > c.x && x + 4 * FP < c.x + cloudW * FP) {
        y = c.y - blobH * FP;
        vy = bounceSpeed;
        if (c.kind == Kind::Puff) {
          c.gone = true;
          lv_obj_set_hidden(c.obj, true);
        }
        break;
      }
    }
  }

  // scroll the world down when climbing above the camera line
  if (y < cameraLine * FP) {
    const int32_t shift = cameraLine * FP - y;
    y = cameraLine * FP;
    height += static_cast<uint32_t>(shift);
    int32_t topmost = LV_VER_RES * FP;
    for (const auto& c : clouds) {
      topmost = c.y < topmost ? c.y : topmost;
    }
    for (auto& c : clouds) {
      c.y += shift;
    }
    topmost += shift;
    for (auto& c : clouds) {
      if (c.y > LV_VER_RES * FP) {
        topmost -= Gap();
        PlaceCloud(c, topmost);
      }
    }
    UpdateScore();
  }

  for (auto& c : clouds) {
    if (c.kind == Kind::Drifting && !c.gone) {
      c.x += c.vx;
      if (c.x < 0 || c.x > (LV_HOR_RES - cloudW) * FP) {
        c.vx = -c.vx;
      }
    }
    lv_obj_set_pos(c.obj, static_cast<lv_coord_t>(c.x / FP), static_cast<lv_coord_t>(c.y / FP));
  }
  lv_obj_set_pos(blob, static_cast<lv_coord_t>(x / FP), static_cast<lv_coord_t>(y / FP));

  if (y > LV_VER_RES * FP) {
    Fall();
  }
}
