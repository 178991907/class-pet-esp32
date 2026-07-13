// 最小桩实现，提供 esp-sr 链接所需的 get_model_base_path / srmodel_spiffs_init 符号。
// 真实模型加载需烧录 "model" SPIFFS 分区（见 DEVELOPMENT_STANDARD.md §6）。
#include "stdio.h"

char *get_model_base_path(void) {
    return "srmodel";
}

void srmodel_spiffs_init(void) {
    printf("srmodel_spiffs_init (stub, 未挂载 model 分区)\n");
}
