sed -i '' 's/#include "ESP32Audio.h"/#include "ESP32Audio.h"\n#include "ES8311.h"\n/g' ESP32Audio.cpp
sed -i '' 's/audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);/ES8311::init(TOUCH_SDA_PIN, TOUCH_SCL_PIN);\n  audioStream.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);/g' ESP32Audio.cpp
