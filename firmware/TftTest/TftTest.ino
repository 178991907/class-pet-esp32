// ESP32-S3 电容屏黑板 (ES3C28P) TFT drawString 文字与划线测试
#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI* tft = nullptr;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=========================================");
  Serial.println("   ESP32-S3 (ES3C28P) drawString 测试");
  Serial.println("=========================================");

  tft = new TFT_eSPI();
  tft->init();
  tft->invertDisplay(true); // 颜色修正

  // 手动拉高背光引脚
  pinMode(45, OUTPUT);
  digitalWrite(45, HIGH);

  // 默认方向 (竖屏 240x320)
  tft->fillScreen(TFT_BLUE); 

  // 1. 绘制几何图形以确认 SPI 数据传输完全正确
  tft->fillRect(20, 20, 200, 50, TFT_WHITE);
  tft->fillCircle(120, 160, 40, TFT_RED);

  // 绘制几条测试线
  tft->drawLine(0, 0, 240, 320, TFT_YELLOW);
  tft->drawLine(240, 0, 0, 320, TFT_YELLOW);

  // 2. 尝试使用 drawString（TFT_eSPI 专用高效画字函数）
  // 参数: String, X, Y, FontNumber
  Serial.println("正在绘制文字...");
  tft->setTextColor(TFT_YELLOW, TFT_BLUE);
  
  // 字体 1 (内置小字体)
  tft->drawString("Font 1: Hello World!", 10, 220, 1);
  
  // 字体 2 (中等字体)
  tft->drawString("Font 2: ClassPet", 10, 240, 2);
  
  // 字体 4 (大号字体)
  tft->drawString("Font 4: OK", 10, 270, 4);

  Serial.println("=== 绘制完毕！ ===");
}

void loop() {
  delay(1000);
  Serial.println("运行中...");
}
