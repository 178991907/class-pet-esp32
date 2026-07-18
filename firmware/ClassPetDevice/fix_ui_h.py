import re

file_path = "ClassPetUI.h"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# Add missing fields to ClassPetUI.h
fields = """
  // 现代 Web 风格的集成式选项卡屏幕
  lv_obj_t* _scr_tabview = nullptr;
  lv_obj_t* _tabview = nullptr;
  lv_obj_t* _tab_calendar = nullptr;
  lv_obj_t* _tab_list = nullptr;
  lv_obj_t* _tab_alarm = nullptr;
  lv_obj_t* _tab_owner = nullptr;

  lv_obj_t* _btn_voice_float = nullptr;
  lv_obj_t* _btn_tomato_float = nullptr;
"""
content = re.sub(r'lv_obj_t\* _scr_normal;', fields + '\n  lv_obj_t* _scr_normal;', content)

# And add initTabView to the public methods
methods = """
  void initTabView();
  void initNormalScreen();
"""
content = re.sub(r'void initNormalScreen\(\);', methods, content)

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
