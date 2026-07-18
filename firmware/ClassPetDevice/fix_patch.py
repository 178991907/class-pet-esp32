import re

file_path = "ClassPetUI.cpp"
with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

standby_clock = """
void ClassPetUI::showStandbyClock() {
  if (lv_screen_active() != _scr_standby) {
    loadScreen(_scr_standby);
  }
}
"""

checklist_cb = """
static void checklistRowCb(lv_event_t* e) {
  lv_obj_t* row = (lv_obj_t*)lv_event_get_target(e);
  lv_obj_t* check = lv_obj_get_child(row, 0); // checkbox
  if (check && lv_obj_check_type(check, &lv_checkbox_class)) {
    bool is_checked = lv_obj_has_state(check, LV_STATE_CHECKED);
    lv_obj_t* label = lv_obj_get_child(row, 1);
    if (label) {
      if (is_checked) lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
      else lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_NONE, 0);
    }
  }
}
"""

if "void ClassPetUI::showStandbyClock()" not in content:
    content = content.replace("void ClassPetUI::showSettings() {", standby_clock + "\nvoid ClassPetUI::showSettings() {")

if "static void checklistRowCb" not in content:
    content = content.replace("void ClassPetUI::addListRow", checklist_cb + "\nlv_obj_t* ClassPetUI::addListRow")

with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
