/**
 * @file ClassPetUI.h
 * @brief 班级宠物园 LVGL 9.5 图形界面子系统
 */

#ifndef CLASSPET_UI_H
#define CLASSPET_UI_H

#include <lvgl.h>
#include <Arduino.h>

class ClassPetUI {
public:
  static ClassPetUI& getInstance() {
    static ClassPetUI instance;
    return instance;
  }

  void init();
  
  // UI 触发接口
  void showNormalScreen(const String& name, int points, int level, int progress, int required, bool isMaxLevel, bool isOnline);
  void updateStatusBar(bool isOnline, const String& wifiName, int batteryPct, bool isCharging);
  void showDiagnosticScreen(const String& wifiStatus, const String& localIp, const String& domain, const String& resolvedIp, int httpCode, int tlsErr, const String& suggestion, const String& mac);
  void showTomatoScreen(int remainingSec, int percent, bool isPaused);
  void showProcessingScreen(const String& statusText);
  void showRecordingScreen(int volumeDb);
  
  void showToast(const String& message, int duration_ms = 3000);

  // 设置并在主页播放宠物动图
  void setPetGif(const void* data, size_t size);
  
  private:
  ClassPetUI() : 
    _scr_normal(nullptr), _scr_diag(nullptr), _scr_tomato(nullptr), _scr_processing(nullptr),
    _card_normal(nullptr),
    _lbl_normal_wifi(nullptr), _lbl_normal_battery(nullptr), _bar_battery(nullptr), _lbl_normal_name(nullptr), _lbl_normal_lv(nullptr), 
    _bar_normal_exp(nullptr), _lbl_normal_exp(nullptr),
    _lbl_diag_wifi(nullptr), _lbl_diag_ip(nullptr), _lbl_diag_domain(nullptr), _lbl_diag_resolved(nullptr),
    _lbl_diag_http(nullptr), _lbl_diag_tls(nullptr), _lbl_diag_sugg(nullptr), _lbl_diag_mac(nullptr),
    _lbl_tomato_time(nullptr), _bar_tomato_progress(nullptr), _lbl_tomato_status(nullptr),
    _lbl_proc_text(nullptr), _bar_rec_vol(nullptr), _toast_label(nullptr), _toast_container(nullptr),
    _gif_pet(nullptr) {}
  
  // 屏幕对象
  lv_obj_t* _scr_normal;
  lv_obj_t* _scr_diag;
  lv_obj_t* _scr_tomato;
  lv_obj_t* _scr_processing;
  
  // 主界面控件
  lv_obj_t* _card_normal;
  lv_obj_t* _lbl_normal_wifi;
  lv_obj_t* _lbl_normal_battery;
  lv_obj_t* _bar_battery;
  lv_obj_t* _lbl_normal_name;
  lv_obj_t* _lbl_normal_lv;
  lv_obj_t* _bar_normal_exp;
  lv_obj_t* _lbl_normal_exp;
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
  lv_obj_t* _lbl_tomato_time;
  lv_obj_t* _bar_tomato_progress;
  lv_obj_t* _lbl_tomato_status;
  
  // 进度/录音界面控件
  lv_obj_t* _lbl_proc_text;
  lv_obj_t* _bar_rec_vol;
  
  // 全局 Toast
  lv_obj_t* _toast_label;
  lv_obj_t* _toast_container;
  
  void initNormalScreen();
  void initDiagScreen();
  void initTomatoScreen();
  void initProcessingScreen();
  
  // 内部辅助
  static void toastTimerCb(lv_timer_t* timer);
  
  // 统一界面跳转
  void loadScreen(lv_obj_t* scr);
};

#endif // CLASSPET_UI_H
