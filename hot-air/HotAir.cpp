#include "HotAir.h"

#include "components/motor/MotorController.h"

#include <cstdio>

using namespace Pinetime::Applications::Screens;

extern lv_font_t jetbrains_mono_42;

namespace {
  // per frame (20 ms), in 1/256 px
  constexpr int32_t gravity = 56;       // 0.22 px/frame^2
  constexpr int32_t burnSpeed = -947;   // -3.7 px/frame
  constexpr int32_t maxFallSpeed = 1280; // 5 px/frame
  constexpr int32_t scrollSpeed = 461;  // 1.8 px/frame
  constexpr uint32_t pressGapMs = 80;     // a touch after this long without one is a new press
  constexpr uint32_t retryDelayMs = 600;  // ignore taps right after crashing

  uint16_t best = 0; // kept while the watch is running

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

HotAir::HotAir(Controllers::MotorController& motor, System::SystemTask& systemTask) : motor {motor}, wakeLock(systemTask) {
  lv_obj_t* sky = Rect(lv_scr_act(), LV_HOR_RES, LV_VER_RES, lv_color_hex(0x1d3557), 0);
  lv_obj_set_pos(sky, 0, 0);

  // a few fixed stars
  static constexpr lv_point_t stars[] = {{20, 30}, {75, 62}, {140, 22}, {200, 50}, {170, 110}, {30, 140}, {110, 170}, {215, 160}};
  for (const auto& s : stars) {
    lv_obj_t* star = Rect(lv_scr_act(), 2, 2, lv_color_hex(0x9fb3d1), 1);
    lv_obj_set_pos(star, s.x, s.y);
  }

  for (auto& p : pillars) {
    p.top = Rect(lv_scr_act(), pillarWidth, 10, lv_color_hex(0x8d6b4f), 5);
    p.bottom = Rect(lv_scr_act(), pillarWidth, 10, lv_color_hex(0x8d6b4f), 5);
    for (lv_obj_t* o : {p.top, p.bottom}) {
      lv_obj_set_style_local_border_width(o, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
      lv_obj_set_style_local_border_color(o, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x5c4433));
    }
  }

  lv_obj_t* ground = Rect(lv_scr_act(), LV_HOR_RES, groundHeight, lv_color_hex(0x3f6b3a), 0);
  lv_obj_set_pos(ground, 0, LV_VER_RES - groundHeight);

  // The balloon: a container with the envelope, a flame and the basket.
  balloon = Rect(lv_scr_act(), balloonW, balloonH, LV_COLOR_BLACK, 0);
  lv_obj_set_style_local_bg_opa(balloon, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_t* envelope = Rect(balloon, balloonW, 28, lv_color_hex(0xe76f51), LV_RADIUS_CIRCLE);
  lv_obj_set_pos(envelope, 0, 0);
  lv_obj_t* stripe = Rect(balloon, 8, 28, lv_color_hex(0xf4a261), LV_RADIUS_CIRCLE);
  lv_obj_set_pos(stripe, 10, 0);
  flame = Rect(balloon, 6, 7, lv_color_hex(0xffd166), 3);
  lv_obj_set_pos(flame, 11, 26);
  lv_obj_set_hidden(flame, true);
  lv_obj_t* basket = Rect(balloon, 10, 7, lv_color_hex(0x9c6644), 2);
  lv_obj_set_pos(basket, 9, 33);

  scoreLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(scoreLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_obj_set_style_local_text_color(scoreLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);

  messagePanel = Panel(&message);

  Reset();
  refreshTask = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

HotAir::~HotAir() {
  lv_task_del(refreshTask);
  lv_obj_clean(lv_scr_act());
}

uint32_t HotAir::Random() {
  // xorshift32
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

int16_t HotAir::GapHeight() const {
  // the gap narrows as you go, down to a minimum
  int16_t h = static_cast<int16_t>(88 - score);
  return h < 66 ? 66 : h;
}

void HotAir::PlacePillar(Pillar& p, int32_t x) {
  const int16_t gap = GapHeight();
  const int16_t minY = 30 + gap / 2;
  const int16_t maxY = LV_VER_RES - groundHeight - 20 - gap / 2;
  p.x = x;
  p.gapH = gap;
  p.gapY = static_cast<int16_t>(minY + static_cast<int16_t>(Random() % static_cast<uint32_t>(maxY - minY)));
  p.scored = false;
  const int16_t topH = static_cast<int16_t>(p.gapY - gap / 2);
  const int16_t bottomY = static_cast<int16_t>(p.gapY + gap / 2);
  lv_obj_set_size(p.top, pillarWidth, topH + 6); // extends above the screen so the rounded end is hidden
  lv_obj_set_size(p.bottom, pillarWidth, LV_VER_RES - bottomY);
  lv_obj_set_pos(p.top, static_cast<lv_coord_t>(x / FP), -6);
  lv_obj_set_pos(p.bottom, static_cast<lv_coord_t>(x / FP), bottomY);
}

void HotAir::ShowMessage(const char* text, lv_coord_t yOffset) {
  if (text == nullptr) {
    lv_obj_set_hidden(messagePanel, true);
    return;
  }
  lv_label_set_text(message, text);
  lv_obj_align(messagePanel, nullptr, LV_ALIGN_CENTER, 0, yOffset);
  lv_obj_set_hidden(messagePanel, false);
}

void HotAir::UpdateScore() {
  lv_label_set_text_fmt(scoreLabel, "%u", static_cast<unsigned>(score));
  lv_obj_align(scoreLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 8);
}

void HotAir::Reset() {
  state = State::Ready;
  score = 0;
  y = 90 * FP;
  vy = 0;
  if (rng == 0) {
    rng = lv_tick_get() | 1;
  }
  for (size_t i = 0; i < pillars.size(); i++) {
    PlacePillar(pillars[i], (LV_HOR_RES + 40 + static_cast<int32_t>(i) * pillarSpacing) * FP);
  }
  lv_obj_set_pos(balloon, balloonX, static_cast<lv_coord_t>(y / FP));
  UpdateScore();
  ShowMessage("Tap to fire\nthe burner", 62);
}

void HotAir::Fire() {
  vy = burnSpeed;
  flameFrames = 6;
  lv_obj_set_hidden(flame, false);
}

void HotAir::Crash() {
  state = State::Crashed;
  crashedAt = lv_tick_get();
  if (score > best) {
    best = score;
  }
  motor.RunForDuration(60);
  wakeLock.Release();
  static char text[48];
  snprintf(text, sizeof(text), "Crashed!\nBest: %u\n\nTap to fly again", static_cast<unsigned>(best));
  ShowMessage(text);
}

bool HotAir::OnTouchEvent(uint16_t /*x*/, uint16_t /*y*/) {
  const bool newPress = lv_tick_elaps(lastTouch) > pressGapMs;
  lastTouch = lv_tick_get();
  if (newPress) {
    Press();
  }
  return true;
}

void HotAir::Press() {
  switch (state) {
    case State::Ready:
      rng ^= lastTouch; // mix in the moment of the first tap
      state = State::Flying;
      wakeLock.Lock();
      ShowMessage(nullptr);
      Fire();
      break;
    case State::Flying:
      Fire();
      break;
    case State::Crashed:
      if (lv_tick_elaps(crashedAt) > retryDelayMs) {
        Reset();
      }
      break;
  }
}

bool HotAir::OnTouchEvent(TouchEvents event) {
  // A very short tap may end before the touch position is ever read; the tap gesture is the backup.
  if (event == TouchEvents::Tap && lv_tick_elaps(lastTouch) > 300) {
    lastTouch = lv_tick_get();
    Press();
    return true;
  }
  // While flying, swallow swipes so a hurried tap does not leave the game. Use the button to exit.
  return state == State::Flying || event == TouchEvents::Tap;
}

void HotAir::Refresh() {
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

void HotAir::Step() {
  frame++;
  if (flameFrames > 0 && --flameFrames == 0) {
    lv_obj_set_hidden(flame, true);
  }

  if (state == State::Ready) {
    // gentle bobbing while waiting
    const int32_t t = static_cast<int32_t>((frame / 6) % 8);
    y = (90 + (t < 4 ? t : 8 - t)) * FP;
    lv_obj_set_pos(balloon, balloonX, static_cast<lv_coord_t>(y / FP));
    return;
  }
  const int32_t groundY = (LV_VER_RES - groundHeight - balloonH) * FP;
  if (state == State::Crashed) {
    // fall to the ground
    if (y < groundY) {
      vy = vy + gravity > maxFallSpeed ? maxFallSpeed : vy + gravity;
      y = y + vy > groundY ? groundY : y + vy;
      lv_obj_set_pos(balloon, balloonX, static_cast<lv_coord_t>(y / FP));
    }
    return;
  }

  vy += gravity;
  if (vy > maxFallSpeed) {
    vy = maxFallSpeed;
  }
  y += vy;
  if (y < 0) {
    y = 0;
    vy = 0;
  }
  lv_obj_set_pos(balloon, balloonX, static_cast<lv_coord_t>(y / FP));

  // hit box in whole pixels: slightly smaller than the drawing, it feels fairer
  const int32_t left = balloonX + 3;
  const int32_t right = balloonX + balloonW - 3;
  const int32_t top = y / FP + 2;
  const int32_t bottom = y / FP + balloonH - 1;

  if (bottom >= LV_VER_RES - groundHeight) {
    Crash();
    return;
  }

  for (auto& p : pillars) {
    p.x -= scrollSpeed;
    if (p.x < -pillarWidth * FP) {
      int32_t rightmost = 0;
      for (const auto& other : pillars) {
        rightmost = other.x > rightmost ? other.x : rightmost;
      }
      PlacePillar(p, rightmost + pillarSpacing * FP);
      continue;
    }
    const int32_t px = p.x / FP;
    lv_obj_set_x(p.top, static_cast<lv_coord_t>(px));
    lv_obj_set_x(p.bottom, static_cast<lv_coord_t>(px));

    if (right > px && left < px + pillarWidth) {
      if (top < p.gapY - p.gapH / 2 || bottom > p.gapY + p.gapH / 2) {
        Crash();
        return;
      }
    }
    if (!p.scored && px + pillarWidth < left) {
      p.scored = true;
      score++;
      UpdateScore();
    }
  }
}
