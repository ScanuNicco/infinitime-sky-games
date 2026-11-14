#pragma once

#include <array>
#include <cstdint>
#include <lvgl/lvgl.h>

#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/Screen.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"

namespace Pinetime {
  namespace Controllers {
    class MotorController;
  }

  namespace Applications {
    namespace Screens {

      // Hot Air: tap to fire the balloon's burner and drift between the rock pillars.
      class HotAir : public Screen {
      public:
        HotAir(Controllers::MotorController& motor, System::SystemTask& systemTask);
        ~HotAir() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        bool OnTouchEvent(uint16_t x, uint16_t y) override;

      private:
        enum class State { Ready, Flying, Crashed };

        struct Pillar {
          lv_obj_t* top;
          lv_obj_t* bottom;
          int32_t x; // fixed point, see FP
          int16_t gapY; // centre of the gap
          int16_t gapH; // height of the gap, fixed when the pillar is placed
          bool scored;
        };

        static constexpr int16_t pillarWidth = 34;
        static constexpr int16_t pillarSpacing = 112;
        static constexpr int16_t groundHeight = 12;
        static constexpr int16_t balloonX = 54;
        static constexpr int16_t balloonW = 28;
        static constexpr int16_t balloonH = 40;

        void Reset();
        void Step();
        void Fire();
        void Crash();
        void PlacePillar(Pillar& p, int32_t x);
        void ShowMessage(const char* text, lv_coord_t yOffset = 0);
        void Press();
        void UpdateScore();
        int16_t GapHeight() const;
        uint32_t Random();

        Controllers::MotorController& motor;
        System::WakeLock wakeLock;

        lv_obj_t* balloon;
        lv_obj_t* flame;
        lv_obj_t* scoreLabel;
        lv_obj_t* message;
        lv_obj_t* messagePanel;
        std::array<Pillar, 3> pillars;
        lv_task_t* refreshTask;

        // Positions and speeds are fixed point (1/256 px): no floating point in the game loop.
        static constexpr int32_t FP = 256;

        State state = State::Ready;
        int32_t y = 90 * FP;
        int32_t vy = 0;
        uint16_t score = 0;
        uint8_t flameFrames = 0;
        uint32_t frame = 0;
        uint32_t lastTouch = 0;
        uint32_t crashedAt = 0;
        uint32_t rng = 0;
        uint32_t lastStep = 0;
        uint32_t stepBudget = 0;
        static constexpr uint32_t stepMs = 20;
      };
    }

    template <>
    struct AppTraits<Apps::HotAir> {
      static constexpr Apps app = Apps::HotAir;
      static constexpr const char* icon = "\xEF\x81\xAD"; // FontAwesome "fire" (0xf06d)

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HotAir(controllers.motorController, *controllers.systemTask);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
