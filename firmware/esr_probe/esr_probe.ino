// 探针：仅用于验证 arduino-cli 能否通过 EXTRA_COMPONENT_DIRS 发现并链接 esp-sr 组件。
// 真实集成见 firmware/ClassPetDevice 的 WakeWordEngine。
#include "esp_afe_sr_iface.h"
extern "C" const esp_afe_sr_iface_t *esp_afe_handle_from_config(afe_config_t *config);

void setup() {
  // 仅引用符号地址以强制链接器拉入 esp-sr 组件，不做任何运行时调用
  (void)&esp_afe_handle_from_config;
}

void loop() {}
