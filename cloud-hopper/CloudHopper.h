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

      // Cloud Hopper: a jelly blob bounces from cloud to cloud. Hold the left or right
      // half of the screen to steer; see how high you can climb.
      class CloudHopper : public Screen {
      public:
        CloudHopper(Controllers::MotorController& motor, System::SystemTask& systemTask);
        ~CloudHopper() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        bool OnTouchEvent(uint16_t x, uint16_t y) override;

      private:
        enum class State { Ready, Playing, Fallen };
        enum class Kind : uint8_t { Plain, Drifting, Puff };

        struct Cloud {
          lv_obj_t* obj;
          int32_t x; // fixed point, see FP
          int32_t y;
          int32_t vx;
          Kind kind;
          bool gone;
        };

        static constexpr int16_t cloudW = 46;
        static constexpr int16_t cloudH = 10;
        static constexpr int16_t blobW = 22;
        static constexpr int16_t blobH = 20;
        static constexpr int16_t cameraLine = 100; // the blob never goes above this; the world scrolls instead

        void Reset();
        void Step();
        void PlaceCloud(Cloud& c, int32_t y);
        void ShowMessage(const char* text);
        void Press();
        void UpdateScore();
        void Fall();
        uint32_t Random();
        int32_t Gap();

        Controllers::MotorController& motor;
        System::WakeLock wakeLock;

        lv_obj_t* blob;
        lv_obj_t* scoreLabel;
        lv_obj_t* message;
        lv_obj_t* messagePanel;
        lv_obj_t* scorePanel;
        std::array<Cloud, 7> clouds;
        lv_task_t* refreshTask;

        // Positions and speeds are fixed point (1/256 px): no floating point in the game loop.
        static constexpr int32_t FP = 256;

        State state = State::Ready;
        int32_t x = 0;
        int32_t y = 0;
        int32_t vx = 0;
        int32_t vy = 0;
        uint32_t height = 0; // climbed, in 1/256 px
        uint32_t lastTouch = 0;
        uint16_t touchX = 0;
        bool steering = false; // the current touch began during play (the tap that starts a game doesn't steer)
        uint32_t fellAt = 0;
        uint32_t rng = 0;
        uint32_t lastStep = 0;
        uint32_t stepBudget = 0;
        static constexpr uint32_t stepMs = 20;
      };
    }

    template <>
    struct AppTraits<Apps::CloudHopper> {
      static constexpr Apps app = Apps::CloudHopper;
      static constexpr const char* icon = "\xEF\x83\x82"; // FontAwesome "cloud" (0xf0c2)

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::CloudHopper(controllers.motorController, *controllers.systemTask);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
