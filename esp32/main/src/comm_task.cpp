#include "comm_task.h"

#include "ota_handler.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace gui::esp32
{

namespace
{

void commTaskFn(void *param)
{
    auto *loop = static_cast<gui::common::CommLoop *>(param);
    while (!loop->shouldStop()) {
        loop->runOnce();
        checkOtaDataTimeout();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(nullptr);
}

} // namespace

void startCommTask(gui::common::CommLoop &loop)
{
    xTaskCreatePinnedToCore(commTaskFn, "comm", 4096, &loop, 4, nullptr, 0);
}

} // namespace gui::esp32
