import re

file_path = "ClassPetDevice.ino"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

setup_new = """void setup() {
  // 1. 等待 USB CDC 枚举，给串口监视器留出连接时间
  delay(3000);

  Serial.begin(115200);
  Serial.println("\\n\\n========================================");
  Serial.println("🚀 [系统] 班级宠物园 (ClassPet Garden) 启动");
  Serial.println("========================================");

  // 1.5 测试背光 (闪烁三次)
  pinMode(45, OUTPUT);
  Serial.println("💡 [硬件] 开始测试背光 (闪烁 3 次)...");
  for(int i = 0; i < 3; i++) {
    digitalWrite(45, HIGH);
    delay(200);
    digitalWrite(45, LOW);
    delay(200);
  }
  digitalWrite(45, HIGH);
  Serial.println("💡 [硬件] 背光测试结束，应保持亮起。");

  // A. 初始化本地存储与配置
  Storage::init();"""

content = re.sub(r'void setup\(\) \{\s*Serial\.begin\(115200\);\s*Serial\.println\("\\n\\n========================================"\);\s*Serial\.println\("🚀 \[系统\] 班级宠物园 \(ClassPet Garden\) 启动"\);\s*Serial\.println\("========================================"\);\s*// A\. 初始化本地存储与配置\s*Storage::init\(\);', setup_new, content)

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
