import re

file_path = "WakeWordEngine.h"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# Disable wake word engine by removing includes and replacing with dummy functions
content = re.sub(r'#include "esp_afe_sr_iface\.h"\n#include "esp_afe_sr_models\.h"\n#include "esp_mn_iface\.h"\n#include "esp_mn_models\.h"', '// esp-sr includes removed', content)

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
    
file_path2 = "WakeWordEngine.cpp"
with open(file_path2, "r", encoding="utf-8") as f:
    content2 = f.read()
    
content2 = re.sub(r'#include "esp_afe_sr_iface\.h".*?#include "esp_mn_models\.h"', '// esp-sr includes removed', content2, flags=re.DOTALL)
content2 = re.sub(r'const esp_afe_sr_iface_t\s*\*\s*afe_handle\s*=\s*&ESP_AFE_SR_HANDLE;.*?\n', '// afe_handle removed\n', content2, flags=re.DOTALL)

with open(file_path2, "w", encoding="utf-8") as f:
    f.write(content2)
