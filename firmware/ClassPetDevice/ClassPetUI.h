/**
 * @file ClassPetUI.h
 * @brief 班级宠物园 LVGL 9.5 图形界面子系统
 */

#ifndef CLASSPET_UI_H
#define CLASSPET_UI_H

#include <lvgl.h>
#include <Arduino.h>
#include <vector>

class ClassPetUI {
public:
  static ClassPetUI& getInstance() {
    static ClassPetUI instance;
    return instance;
  }

  void init();
  void reloadCjkFont();  // 字库下载完成后刷新所有中文标签

  // UI 触发接口
  void showNormalScreen(const String& name, int points, int level, int progress, int required, bool isMaxLevel, bool isOnline);
  void updateClock(const String& timeStr, const String& dateStr);
  void updateStatusBar(bool isOnline, const String& wifiName, int batteryPct, bool isCharging);
  void showDiagnosticScreen(const String& wifiStatus, const String& localIp, const String& domain, const String& resolvedIp, int httpCode, int tlsErr, const String& suggestion, const String& mac);
  void showTomatoScreen(int remainingSec, int percent, bool isPaused);
  void showProcessingScreen(const String& statusText);
  void showRecordingScreen(int volumeDb);
  void showTomatoSettings();
  uint32_t getSelectedTomatoTime();

  // 新增功能屏
  void showSettings();
  void showStandbyClock();

  void showMenu();
  void showCalendar();
  void showList();
  void showAlarm();
  void showOwner();
  
  void initMenuScreen();
  void initCalendarScreen();
  void initListScreen();
  void initAlarmScreen();
  void initOwnerScreen();

  // 请求后台同步某功能数据 (type: 1=日历 2=清单 3=闹铃 4=主人记忆)
  void markFeatureSync(uint8_t type);
  // 后台(主循环)取回数据后调用，刷新对应列表
  void renderFeatureData(uint8_t type, const String& json);
  // 刷新设置页的只读信息 (机器码/状态/固件/系统)
  void refreshSettingsInfo();
  
  void showToast(const String& message, int duration_ms = 3000);
  void forceSwitchToNormal();

  // P0: 语音期间"宠物常驻 + 实时字幕"覆盖层 (叠在当前屏之上, 不切换整页)
  void enterVoiceOverlay(const String& title);
  void setVoiceOverlayTitle(const String& title);
  void setVoiceOverlayLevel(int db);
  void setVoiceOverlayCaption(bool isUser, const String& text);
  void setVoiceOverlayCountdown(int seconds);      // 更新录音倒计时
  void setVoiceOverlayProcessing(bool processing); // 切换到"识别中" spinner
  void exitVoiceOverlay();

  // 设置并在主页播放宠物动图
  void setPetGif(const void* data, size_t size);
  
  private:
  ClassPetUI() : 
    _scr_normal(nullptr), _scr_diag(nullptr), _scr_tomato(nullptr), _scr_processing(nullptr),
    _scr_standby(nullptr),
    _scr_settings(nullptr), _scr_menu(nullptr),
    _scr_calendar(nullptr), _scr_list(nullptr), _scr_alarm(nullptr), _scr_owner(nullptr),
    _card_normal(nullptr),
    _lbl_normal_wifi(nullptr), _lbl_normal_battery(nullptr), _bar_battery(nullptr), _lbl_normal_name(nullptr), _lbl_normal_lv(nullptr), 
    _bar_normal_exp(nullptr), _lbl_normal_exp(nullptr),
    _lbl_time(nullptr), _lbl_date(nullptr), _lbl_standby_time(nullptr), _lbl_standby_date(nullptr),
    _lbl_diag_wifi(nullptr), _lbl_diag_ip(nullptr), _lbl_diag_domain(nullptr), _lbl_diag_resolved(nullptr),
    _lbl_diag_http(nullptr), _lbl_diag_tls(nullptr), _lbl_diag_sugg(nullptr), _lbl_diag_mac(nullptr),
    _lbl_tomato_time(nullptr), _arc_tomato_progress(nullptr), _lbl_tomato_status(nullptr),
    _lbl_proc_text(nullptr), _bar_rec_vol(nullptr), _toast_label(nullptr), _toast_container(nullptr),
    _gif_pet(nullptr),
    _slider_volume(nullptr), _slider_brightness(nullptr), _slider_standby(nullptr), _slider_screenoff(nullptr),
    _lbl_volume_val(nullptr), _lbl_brightness_val(nullptr), _lbl_standby_val(nullptr), _lbl_screenoff_val(nullptr),
    _lbl_machine_code(nullptr), _lbl_dev_status(nullptr), _lbl_fw_info(nullptr), _lbl_sys_info(nullptr),
    _list_calendar(nullptr), _list_checklist(nullptr), _list_alarm(nullptr), _list_owner(nullptr) {}
  
  // 屏幕对象
  
  // 现代 Web 风格的集成式选项卡屏幕
  lv_obj_t* _scr_tabview = nullptr;
  lv_obj_t* _tabview = nullptr;
  lv_obj_t* _tab_calendar = nullptr;
  lv_obj_t* _tab_list = nullptr;
  lv_obj_t* _tab_alarm = nullptr;
  lv_obj_t* _tab_owner = nullptr;

  lv_obj_t* _btn_voice_float = nullptr;
  lv_obj_t* _btn_tomato_float = nullptr;

  lv_obj_t* _scr_normal;
  lv_obj_t* _scr_diag;
  lv_obj_t* _scr_tomato;
  lv_obj_t* _scr_processing;
  lv_obj_t* _scr_standby;
  lv_obj_t* _scr_settings;
  lv_obj_t* _scr_menu;
  lv_obj_t* _scr_calendar;
  lv_obj_t* _scr_list;
  lv_obj_t* _scr_alarm;
  lv_obj_t* _scr_owner;
  
  // 主界面控件
  lv_obj_t* _card_normal;
  lv_obj_t* _lbl_normal_wifi;
  lv_obj_t* _lbl_normal_battery;
  lv_obj_t* _bar_battery;
  lv_obj_t* _lbl_normal_name;
  lv_obj_t* _lbl_normal_lv;
  lv_obj_t* _bar_normal_exp;
  lv_obj_t* _lbl_normal_exp;
  lv_obj_t* _lbl_time;
  lv_obj_t* _lbl_date;
  lv_obj_t* _lbl_standby_time;
  lv_obj_t* _lbl_standby_date;
  
  lv_obj_t* _gif_pet;
  lv_image_dsc_t _gif_dsc; // LVGL 9 需要包装成 img_dsc
  
  // 诊断界面控件
  lv_obj_t* _lbl_diag_wifi;
  lv_obj_t* _lbl_diag_ip;
  lv_obj_t* _lbl_diag_domain;
  lv_obj_t* _lbl_diag_resolved;
  lv_obj_t* _lbl_diag_http;
  lv_obj_t* _lbl_diag_tls;
  lv_obj_t* _lbl_diag_sugg;
  lv_obj_t* _lbl_diag_mac;
  
  // 番茄钟控件
  lv_obj_t* _cont_tomato_settings;
  lv_obj_t* _cont_tomato_timer;
  lv_obj_t* _roller_tomato_time;
  lv_obj_t* _lbl_tomato_time;
  lv_obj_t* _arc_tomato_progress;
  lv_obj_t* _lbl_tomato_status;

  lv_image_dsc_t _gif_tomato_dsc;

  // 设置屏控件
  lv_obj_t* _slider_volume;
  lv_obj_t* _slider_brightness;
  lv_obj_t* _slider_standby;
  lv_obj_t* _slider_screenoff;
  lv_obj_t* _lbl_volume_val;
  lv_obj_t* _lbl_brightness_val;
  lv_obj_t* _lbl_standby_val;
  lv_obj_t* _lbl_screenoff_val;
  lv_obj_t* _lbl_machine_code;
  lv_obj_t* _lbl_dev_status;
  lv_obj_t* _lbl_fw_info;
  lv_obj_t* _lbl_sys_info;

  // 功能列表容器
  lv_obj_t* _list_calendar;
  lv_obj_t* _list_checklist;
  lv_obj_t* _list_alarm;
  lv_obj_t* _list_owner;
  
  // Tab 按钮引用，方便切换样式
  lv_obj_t* _tab_btn_calendar;
  lv_obj_t* _tab_btn_checklist;
  lv_obj_t* _tab_btn_alarm;
  lv_obj_t* _tab_btn_owner;
  lv_obj_t* _tab_lbl_calendar;
  lv_obj_t* _tab_lbl_checklist;
  lv_obj_t* _tab_lbl_alarm;
  lv_obj_t* _tab_lbl_owner;
  int _current_tab_index = 0; // 0=日历, 1=清单, 2=闹铃, 3=画像
  // 清单项 id / 完成态缓存(供点击切换)
  std::vector<String> _check_ids;
  std::vector<bool> _check_done;

  
  // 进度/录音界面控件
  lv_obj_t* _lbl_proc_text;
  lv_obj_t* _bar_rec_vol;
  
  // 全局 Toast
  lv_obj_t* _toast_label;
  lv_obj_t* _toast_container;
  lv_timer_t* _toast_timer = nullptr;   // P2: 复用单一定时器, 避免多次 showToast 叠加提前消失

  // P0: 语音覆盖层控件 (child of 当前屏, 透明背景, 宠物在底层保持可见)
  lv_obj_t* _voice_overlay = nullptr;
  lv_obj_t* _voice_title = nullptr;
  lv_obj_t* _voice_countdown = nullptr;
  lv_obj_t* _voice_level = nullptr;
  lv_obj_t* _voice_stop_btn = nullptr;
  lv_obj_t* _voice_processing_spinner = nullptr;
  lv_obj_t* _voice_you = nullptr;
  lv_obj_t* _voice_pet = nullptr;
  
  
  void initTabView();
  void initNormalScreen();

  void initDiagScreen();
  void initTomatoScreen();
  void initProcessingScreen();
  void initStandbyScreen();
  void initSettingsScreen();
  
  void initFeaturesScreen();
  void switchFeatureTab(int idx);

  // 针对不同设计的卡片渲染函数
  lv_obj_t* addListRow(lv_obj_t* scroll, const String& title, const String& sub, bool dim = false);
  lv_obj_t* addTodoRow(lv_obj_t* scroll, const String& title, bool isDone);
  lv_obj_t* addAlarmRow(lv_obj_t* scroll, const String& timeStr, const String& title);
  
  void clearList(lv_obj_t* scroll);
  
  // 内部辅助
  static void toastTimerCb(lv_timer_t* timer);
  static void gesture_event_cb(lv_event_t* e);
  static void checklistRowCb(lv_event_t* e);
  
  // 统一界面跳转
  void loadScreen(lv_obj_t* scr);

};

#endif // CLASSPET_UI_H
