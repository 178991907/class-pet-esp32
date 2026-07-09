cd firmware/ClassPetDevice
sed -i '' 's/ES8311::init(TOUCH_SDA_PIN, TOUCH_SCL_PIN);/pinMode(AUDIO_EN_PIN, OUTPUT);\n  digitalWrite(AUDIO_EN_PIN, HIGH);\n  ES8311::init(TOUCH_SDA_PIN, TOUCH_SCL_PIN);/g' ESP32Audio.cpp
