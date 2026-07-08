/**
 * @file ClassPetUI.cpp
 * @brief 班级宠物园 LVGL 9.5 图形界面子系统实现
 */

#include "ClassPetUI.h"
#include "LcdDisplay.h"
#include "DeviceStateMachine.h"

// 声明外部自定义中文字库 (包含3500常用字 + ASCII + 常用Emoji)
LV_FONT_DECLARE(my_font_cjk_16);

// ==========================================
// 核心调色板 (和 Tailwind 统一视觉系统 - 适配亮色主题以匹配网页端)
// ==========================================
#define LV_COLOR_BG          lv_color_hex(0xFFF1F2) // Rose 50 (极浅的粉白背景，类似网页)
#define LV_COLOR_CARD_BG     lv_color_hex(0xFFFFFF) // Pure White (纯白卡片)
#define LV_COLOR_PRIMARY     lv_color_hex(0xFF9F43) // 班级宠物橙色
#define LV_COLOR_SUCCESS     lv_color_hex(0x26DE81) // 绿色
#define LV_COLOR_WARNING     lv_color_hex(0xF59E0B) // 黄色
#define LV_COLOR_DANGER      lv_color_hex(0xEF4444) // 红色
#define LV_COLOR_TEXT        lv_color_hex(0x1E293B) // Slate 800 (深灰黑字)
#define LV_COLOR_TEXT_MUTED  lv_color_hex(0x64748B) // Slate 500 (辅助灰色字)
#define LV_COLOR_BORDER      lv_color_hex(0xE2E8F0) // Slate 200 (浅灰边框)

// 按钮点击事件回调函数
static void btn_tomato_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_START);
}

static void btn_voice_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_START);
}

static void btn_tomato_pause_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_PAUSE_RESUME);
}

static void btn_tomato_exit_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_STOP);
}

static void lbl_tomato_time_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_ADJUST);
}

void ClassPetUI::init() {
  // 1. 初始化四个独立页面
  _scr_normal = lv_obj_create(NULL);
  _scr_diag = lv_obj_create(NULL);
  _scr_tomato = lv_obj_create(NULL);
  _scr_processing = lv_obj_create(NULL);
  
  // 2. 配置背景样式
  lv_obj_t* screens[] = { _scr_normal, _scr_diag, _scr_tomato, _scr_processing };
  for (int i = 0; i < 4; i++) {
    lv_obj_set_style_bg_color(screens[i], LV_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(screens[i], LV_OPA_COVER, 0);
  }
  
  // 3. 构造各页面的静态子控件
  initNormalScreen();
  initDiagScreen();
  initTomatoScreen();
  initProcessingScreen();
  
  // 4. 创建系统全局浮空 Toast 气泡 (挂载在系统层，换页不消失)
  _toast_container = lv_obj_create(lv_layer_sys());
  lv_obj_set_size(_toast_container, 280, 50);
  lv_obj_align(_toast_container, LV_ALIGN_BOTTOM_MID, 0, -25);
  lv_obj_set_style_bg_color(_toast_container, lv_color_hex(0x334155), 0); // slate 700
  lv_obj_set_style_bg_opa(_toast_container, LV_OPA_90, 0);
  lv_obj_set_style_border_color(_toast_container, LV_COLOR_PRIMARY, 0);
  lv_obj_set_style_border_width(_toast_container, 1, 0);
  lv_obj_set_style_radius(_toast_container, 8, 0);
  lv_obj_set_style_shadow_width(_toast_container, 10, 0);
  lv_obj_set_style_shadow_color(_toast_container, lv_color_black(), 0);
  
  _toast_label = lv_label_create(_toast_container);
  lv_obj_set_width(_toast_label, 260);
  lv_label_set_text(_toast_label, "");
  lv_obj_set_style_text_color(_toast_label, LV_COLOR_TEXT, 0);
  lv_obj_set_style_text_align(_toast_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(_toast_label, &my_font_cjk_16, 0); // 设置中文字体
  lv_obj_align(_toast_label, LV_ALIGN_CENTER, 0, 0);
  
  lv_obj_add_flag(_toast_container, LV_OBJ_FLAG_HIDDEN); // 默认隐藏
  
  // 默认启动载入自检诊断页
  loadScreen(_scr_diag);
}

// ==========================================
// 1. 初始化主正常宠物页 (ScreenNormal)
// ==========================================
void ClassPetUI::initNormalScreen() {
  // A. 顶部状态栏 - WiFi
  _lbl_normal_wifi = lv_label_create(_scr_normal);
  lv_obj_set_pos(_lbl_normal_wifi, 8, 8);
  lv_obj_set_size(_lbl_normal_wifi, 220, 18); // 限制宽度和高度，强制不换行
  lv_label_set_long_mode(_lbl_normal_wifi, LV_LABEL_LONG_DOT); // 超出部分显示省略号
  lv_obj_set_style_text_color(_lbl_normal_wifi, LV_COLOR_SUCCESS, 0);
  lv_obj_set_style_text_font(_lbl_normal_wifi, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_normal_wifi, "未连接");
  
  // A. 顶部状态栏 - 电池外框 (右对齐)
  _bar_battery = lv_bar_create(_scr_normal);
  lv_obj_set_size(_bar_battery, 46, 18);
  lv_obj_align(_bar_battery, LV_ALIGN_TOP_RIGHT, -15, 6);
  
  lv_obj_set_style_bg_color(_bar_battery, lv_color_hex(0xEEEEEE), 0); // 浅灰背景
  lv_obj_set_style_bg_opa(_bar_battery, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_bar_battery, 1, 0);
  lv_obj_set_style_border_color(_bar_battery, LV_COLOR_TEXT, 0); // 黑色边框
  lv_obj_set_style_radius(_bar_battery, 4, 0);
  lv_obj_set_style_pad_all(_bar_battery, 2, 0); // 内边距，让里面的进度条留出空隙
  
  // 电池进度条指示器
  lv_obj_set_style_bg_color(_bar_battery, LV_COLOR_SUCCESS, LV_PART_INDICATOR);
  lv_obj_set_style_radius(_bar_battery, 2, LV_PART_INDICATOR);
  lv_bar_set_range(_bar_battery, 0, 100);
  lv_bar_set_value(_bar_battery, 100, LV_ANIM_OFF);

  // 电池正极 (右侧小突起)
  lv_obj_t* bat_cap = lv_obj_create(_scr_normal);
  lv_obj_set_size(bat_cap, 3, 8);
  lv_obj_align_to(bat_cap, _bar_battery, LV_ALIGN_OUT_RIGHT_MID, 1, 0); // 贴在右侧外侧
  lv_obj_set_style_bg_color(bat_cap, LV_COLOR_TEXT, 0);
  lv_obj_set_style_border_width(bat_cap, 0, 0);
  lv_obj_set_style_radius(bat_cap, 1, 0);

  // 电池百分比文本 (居中显示在电池上)
  _lbl_normal_battery = lv_label_create(_bar_battery);
  lv_obj_center(_lbl_normal_battery);
  lv_obj_set_style_text_color(_lbl_normal_battery, lv_color_hex(0x222222), 0); // 深色文字
  lv_obj_set_style_text_font(_lbl_normal_battery, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_normal_battery, "100%");
  
  // B. 中心学生档案大卡片 (加宽以适应左右布局)
  _card_normal = lv_obj_create(_scr_normal);
  lv_obj_t* card = _card_normal;
  lv_obj_set_size(card, 290, 140);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, -12);
  lv_obj_set_style_bg_color(card, LV_COLOR_CARD_BG, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, LV_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_shadow_width(card, 20, 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
  lv_obj_set_style_shadow_ofs_y(card, 4, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE); // 禁用滚动，去除右侧多余的滚动条
  
  // 学生姓名标签 (居右对齐)
  _lbl_normal_name = lv_label_create(card);
  lv_obj_align(_lbl_normal_name, LV_ALIGN_TOP_LEFT, 130, 12);
  lv_obj_set_style_text_color(_lbl_normal_name, LV_COLOR_TEXT, 0);
  lv_obj_set_style_text_font(_lbl_normal_name, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_normal_name, "加载中...");
  
  // 等级 & 积分标签
  _lbl_normal_lv = lv_label_create(card);
  lv_obj_align(_lbl_normal_lv, LV_ALIGN_TOP_LEFT, 130, 42);
  lv_obj_set_style_text_color(_lbl_normal_lv, LV_COLOR_WARNING, 0);
  lv_obj_set_style_text_font(_lbl_normal_lv, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_normal_lv, "Lv.1 | 积分: 0");
  
  // 经验文字
  _lbl_normal_exp = lv_label_create(card);
  lv_obj_align(_lbl_normal_exp, LV_ALIGN_BOTTOM_LEFT, 130, -32);
  lv_obj_set_style_text_color(_lbl_normal_exp, LV_COLOR_TEXT_MUTED, 0);
  lv_obj_set_style_text_font(_lbl_normal_exp, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_normal_exp, "0 / 40");
  
  // 经验条
  _bar_normal_exp = lv_bar_create(card);
  lv_obj_set_size(_bar_normal_exp, 130, 10);
  lv_obj_align(_bar_normal_exp, LV_ALIGN_BOTTOM_LEFT, 130, -15);
  lv_obj_set_style_bg_color(_bar_normal_exp, LV_COLOR_BORDER, 0); // 浅灰轨道颜色
  lv_obj_set_style_bg_opa(_bar_normal_exp, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(_bar_normal_exp, LV_COLOR_PRIMARY, LV_PART_INDICATOR); // 橙色指示器
  lv_obj_set_style_bg_opa(_bar_normal_exp, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(_bar_normal_exp, 5, 0);
  lv_obj_set_style_radius(_bar_normal_exp, 5, LV_PART_INDICATOR);
  lv_bar_set_range(_bar_normal_exp, 0, 100);
  lv_bar_set_value(_bar_normal_exp, 0, LV_ANIM_ON);
  
  // C. 底部触摸操作按键 (番茄钟 & 语音通话)
  lv_obj_t* btn_tomato = lv_button_create(_scr_normal);
  lv_obj_set_size(btn_tomato, 130, 40);
  lv_obj_align(btn_tomato, LV_ALIGN_BOTTOM_LEFT, 15, -12);
  lv_obj_set_style_bg_color(btn_tomato, LV_COLOR_PRIMARY, 0); // 使用亮眼的主题橙色
  lv_obj_set_style_bg_opa(btn_tomato, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_tomato, 20, 0); // 圆角按钮更符合现代网页设计
  lv_obj_add_event_cb(btn_tomato, btn_tomato_cb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* lbl_t = lv_label_create(btn_tomato);
  lv_label_set_text(lbl_t, "启动番茄");
  lv_obj_set_style_text_color(lbl_t, lv_color_hex(0xFFFFFF), 0); // 明确白色文字
  lv_obj_set_style_text_font(lbl_t, &my_font_cjk_16, 0);
  lv_obj_align(lbl_t, LV_ALIGN_CENTER, 0, 0);
  
  lv_obj_t* btn_voice = lv_button_create(_scr_normal);
  lv_obj_set_size(btn_voice, 130, 40);
  lv_obj_align(btn_voice, LV_ALIGN_BOTTOM_RIGHT, -15, -12);
  lv_obj_set_style_bg_color(btn_voice, LV_COLOR_SUCCESS, 0); // 翠绿
  lv_obj_set_style_bg_opa(btn_voice, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_voice, 20, 0);
  lv_obj_add_event_cb(btn_voice, btn_voice_cb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* lbl_v = lv_label_create(btn_voice);
  lv_label_set_text(lbl_v, "语音通话");
  lv_obj_set_style_text_color(lbl_v, lv_color_hex(0xFFFFFF), 0); // 明确白色文字
  lv_obj_set_style_text_font(lbl_v, &my_font_cjk_16, 0);
  lv_obj_align(lbl_v, LV_ALIGN_CENTER, 0, 0);
}

// ==========================================
// 2. 初始化智能诊断网络页面 (ScreenDiag)
// ==========================================
void ClassPetUI::initDiagScreen() {
  // A. 顶部标题
  lv_obj_t* title = lv_label_create(_scr_diag);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_style_text_color(title, LV_COLOR_PRIMARY, 0);
  lv_obj_set_style_text_font(title, &my_font_cjk_16, 0);
  lv_label_set_text(title, "设备自检自愈中心");
  
  // B. 中心诊断参数框 (Grid-like list)
  lv_obj_t* frame = lv_obj_create(_scr_diag);
  lv_obj_set_size(frame, 290, 140);
  lv_obj_align(frame, LV_ALIGN_CENTER, 0, -2);
  lv_obj_set_style_bg_color(frame, LV_COLOR_CARD_BG, 0);
  lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(frame, LV_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(frame, 1, 0);
  lv_obj_set_style_radius(frame, 12, 0);
  
  // 核心指标
  _lbl_diag_wifi = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_wifi, 10, 5);
  lv_obj_set_style_text_font(_lbl_diag_wifi, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_diag_wifi, "WiFi 状态: 连接中...");
  
  _lbl_diag_ip = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_ip, 10, 25);
  lv_obj_set_style_text_font(_lbl_diag_ip, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_diag_ip, "本地 IP: 0.0.0.0");
  
  _lbl_diag_domain = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_domain, 10, 45);
  lv_obj_set_style_text_font(_lbl_diag_domain, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_diag_domain, "服务器: pete.qqzy.de5.net");
  
  _lbl_diag_resolved = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_resolved, 10, 65);
  lv_obj_set_style_text_font(_lbl_diag_resolved, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_diag_resolved, "DNS 解析: 获取中...");
  
  _lbl_diag_http = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_http, 10, 85);
  lv_obj_set_style_text_font(_lbl_diag_http, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_diag_http, "HTTP 状态: 0");
  
  _lbl_diag_tls = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_tls, 10, 105);
  lv_obj_set_style_text_font(_lbl_diag_tls, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_diag_tls, "TLS 握手: 等待中");
  
  // C. 底部智能排障指南文字/设备 MAC 地址区域
  _lbl_diag_sugg = lv_label_create(_scr_diag);
  lv_obj_set_width(_lbl_diag_sugg, 290);
  lv_obj_align(_lbl_diag_sugg, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_set_style_text_color(_lbl_diag_sugg, LV_COLOR_WARNING, 0);
  lv_obj_set_style_text_align(_lbl_diag_sugg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(_lbl_diag_sugg, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_diag_sugg, "系统自愈中，请稍候...");
  
  _lbl_diag_mac = lv_label_create(_scr_diag);
  lv_obj_align(_lbl_diag_mac, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_text_color(_lbl_diag_mac, LV_COLOR_TEXT_MUTED, 0);
  lv_obj_set_style_text_font(_lbl_diag_mac, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_diag_mac, "MAC: FF:FF:FF:FF:FF:FF");
}

// ==========================================
// 3. 初始化番茄计时器工作页面 (ScreenTomato)
// ==========================================
void ClassPetUI::initTomatoScreen() {
  // A. 顶部标题
  lv_obj_t* title = lv_label_create(_scr_tomato);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_color(title, LV_COLOR_DANGER, 0);
  lv_obj_set_style_text_font(title, &my_font_cjk_16, 0);
  lv_label_set_text(title, "番茄专注时间");
  
  // B. 中心环形大进度条
  _bar_tomato_progress = lv_bar_create(_scr_tomato);
  lv_obj_set_size(_bar_tomato_progress, 240, 15);
  lv_obj_align(_bar_tomato_progress, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(_bar_tomato_progress, LV_COLOR_BORDER, 0);
  lv_obj_set_style_bg_opa(_bar_tomato_progress, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(_bar_tomato_progress, LV_COLOR_DANGER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(_bar_tomato_progress, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(_bar_tomato_progress, 0, 100);
  lv_bar_set_value(_bar_tomato_progress, 100, LV_ANIM_OFF);
  
  // C. 进度倒计时字样
  _lbl_tomato_time = lv_label_create(_scr_tomato);
  lv_obj_align(_lbl_tomato_time, LV_ALIGN_CENTER, 0, -35);
  lv_obj_set_style_text_color(_lbl_tomato_time, LV_COLOR_TEXT, 0);
  lv_obj_set_style_text_font(_lbl_tomato_time, &lv_font_montserrat_24, 0);
  lv_label_set_text(_lbl_tomato_time, "25:00");
  lv_obj_add_flag(_lbl_tomato_time, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(_lbl_tomato_time, lbl_tomato_time_cb, LV_EVENT_CLICKED, NULL);
  
  // D. 专注状态小字
  _lbl_tomato_status = lv_label_create(_scr_tomato);
  lv_obj_align(_lbl_tomato_status, LV_ALIGN_CENTER, 0, 15);
  lv_obj_set_style_text_color(_lbl_tomato_status, LV_COLOR_TEXT_MUTED, 0);
  lv_obj_set_style_text_font(_lbl_tomato_status, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_tomato_status, "努力专注学习中...");
  
  // E. 底部操作按键 (暂停/继续，退出专注)
  lv_obj_t* btn_pause = lv_button_create(_scr_tomato);
  lv_obj_set_size(btn_pause, 110, 36);
  lv_obj_align(btn_pause, LV_ALIGN_BOTTOM_LEFT, 25, -10);
  lv_obj_set_style_bg_color(btn_pause, lv_color_hex(0x475569), 0); // 灰蓝色
  lv_obj_set_style_bg_opa(btn_pause, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_pause, 8, 0);
  lv_obj_add_event_cb(btn_pause, btn_tomato_pause_cb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* lbl_p = lv_label_create(btn_pause);
  lv_label_set_text(lbl_p, "暂停/继续");
  lv_obj_set_style_text_font(lbl_p, &my_font_cjk_16, 0);
  lv_obj_align(lbl_p, LV_ALIGN_CENTER, 0, 0);
  
  lv_obj_t* btn_exit = lv_button_create(_scr_tomato);
  lv_obj_set_size(btn_exit, 110, 36);
  lv_obj_align(btn_exit, LV_ALIGN_BOTTOM_RIGHT, -25, -10);
  lv_obj_set_style_bg_color(btn_exit, LV_COLOR_DANGER, 0); // 红色
  lv_obj_set_style_bg_opa(btn_exit, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_exit, 8, 0);
  lv_obj_add_event_cb(btn_exit, btn_tomato_exit_cb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* lbl_e = lv_label_create(btn_exit);
  lv_label_set_text(lbl_e, "退出专注");
  lv_obj_set_style_text_font(lbl_e, &my_font_cjk_16, 0);
  lv_obj_align(lbl_e, LV_ALIGN_CENTER, 0, 0);
}

// ==========================================
// 4. 初始化加载与语音音频运行中过渡页面 (ScreenProcessing)
// ==========================================
void ClassPetUI::initProcessingScreen() {
  // A. 过渡事件指示文本
  _lbl_proc_text = lv_label_create(_scr_processing);
  lv_obj_align(_lbl_proc_text, LV_ALIGN_CENTER, 0, -25);
  lv_obj_set_style_text_color(_lbl_proc_text, LV_COLOR_TEXT, 0);
  lv_obj_set_style_text_align(_lbl_proc_text, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(_lbl_proc_text, &my_font_cjk_16, 0);
  lv_label_set_text(_lbl_proc_text, "正在上传数据...");
  
  // B. 漂亮的加载环
  lv_obj_t* spinner = lv_spinner_create(_scr_processing);
  lv_obj_set_size(spinner, 50, 50);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 25);
  lv_obj_set_style_arc_color(spinner, LV_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0x1E293B), LV_PART_MAIN);
  
  // C. 模拟波形指示条 (供 Recording 时用)
  _bar_rec_vol = lv_bar_create(_scr_processing);
  lv_obj_set_size(_bar_rec_vol, 200, 15);
  lv_obj_align(_bar_rec_vol, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_bg_color(_bar_rec_vol, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_color(_bar_rec_vol, LV_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_bar_set_range(_bar_rec_vol, 0, 100);
  lv_bar_set_value(_bar_rec_vol, 0, LV_ANIM_OFF);
  
  lv_obj_add_flag(_bar_rec_vol, LV_OBJ_FLAG_HIDDEN); // 默认隐藏
}

// ==========================================
// 5. 页面状态值加载映射驱动层
// ==========================================
void ClassPetUI::showNormalScreen(const String& name, int points, int level, int progress, int required, bool isMaxLevel, bool isOnline) {
  // 设置标题
  // 在线状态不再直接修改_lbl_normal_title，交给 updateStatusBar 控制
  
  // 名字
  String petTitle = name + "的宠物";
  lv_label_set_text(_lbl_normal_name, petTitle.c_str());
  
  // 等级积分
  char buf[48];
  snprintf(buf, sizeof(buf), "Lv.%d  |  积分: %d", level, points);
  lv_label_set_text(_lbl_normal_lv, buf);
  
  // 经验
  if (isMaxLevel) {
    lv_label_set_text(_lbl_normal_exp, "已成功毕业！");
    lv_bar_set_value(_bar_normal_exp, 100, LV_ANIM_ON);
  } else if (required <= 0) {
    lv_label_set_text(_lbl_normal_exp, "等待服务器同步...");
    lv_bar_set_value(_bar_normal_exp, 0, LV_ANIM_OFF);
  } else {
    char expBuf[32];
    snprintf(expBuf, sizeof(expBuf), "经验: %d / %d", progress, required);
    lv_label_set_text(_lbl_normal_exp, expBuf);
    
    int pct = (progress * 100) / required;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(_bar_normal_exp, pct, LV_ANIM_ON);
  }
  
  loadScreen(_scr_normal);
}

void ClassPetUI::showDiagnosticScreen(const String& wifiStatus, const String& localIp, const String& domain, const String& resolvedIp, int httpCode, int tlsErr, const String& suggestion, const String& mac) {
  // WiFi
  String wText = "WiFi状态: " + wifiStatus;
  lv_label_set_text(_lbl_diag_wifi, wText.c_str());
  
  // IP
  String ipText = "本地 IP: " + localIp;
  lv_label_set_text(_lbl_diag_ip, ipText.c_str());
  
  // Domain
  String domText = "服务器: " + domain;
  lv_label_set_text(_lbl_diag_domain, domText.c_str());
  
  // Resolved
  String resText = "DNS解析: " + resolvedIp;
  if (resolvedIp.indexOf("76.76") != -1 || resolvedIp.indexOf("199.59") != -1) {
    resText += " (域名投毒!)";
    lv_obj_set_style_text_color(_lbl_diag_resolved, LV_COLOR_DANGER, 0);
  } else {
    lv_obj_set_style_text_color(_lbl_diag_resolved, LV_COLOR_TEXT, 0);
  }
  lv_label_set_text(_lbl_diag_resolved, resText.c_str());
  
  // HTTP Code
  char httpBuf[32];
  snprintf(httpBuf, sizeof(httpBuf), "HTTP 状态码: %d", httpCode);
  lv_label_set_text(_lbl_diag_http, httpBuf);
  if (httpCode == 403 || httpCode == 500 || httpCode == 502) {
    lv_obj_set_style_text_color(_lbl_diag_http, LV_COLOR_DANGER, 0);
  } else {
    lv_obj_set_style_text_color(_lbl_diag_http, LV_COLOR_TEXT, 0);
  }
  
  // TLS Code
  char tlsBuf[48];
  if (tlsErr != 0) {
    snprintf(tlsBuf, sizeof(tlsBuf), "TLS 握手报错: -0x%04X", -tlsErr);
    lv_obj_set_style_text_color(_lbl_diag_tls, LV_COLOR_DANGER, 0);
  } else {
    snprintf(tlsBuf, sizeof(tlsBuf), "TLS 状态: 正常");
    lv_obj_set_style_text_color(_lbl_diag_tls, LV_COLOR_TEXT, 0);
  }
  lv_label_set_text(_lbl_diag_tls, tlsBuf);
  
  // Suggs
  lv_label_set_text(_lbl_diag_sugg, suggestion.c_str());
  
  // MAC
  String macText = "设备 MAC 地址: " + mac;
  lv_label_set_text(_lbl_diag_mac, macText.c_str());
  
  loadScreen(_scr_diag);
}

void ClassPetUI::showTomatoScreen(int remainingSec, int percent, bool isPaused) {
  int min = remainingSec / 60;
  int sec = remainingSec % 60;
  
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", min, sec);
  lv_label_set_text(_lbl_tomato_time, timeBuf);
  
  lv_bar_set_value(_bar_tomato_progress, percent, LV_ANIM_OFF);
  
  if (isPaused) {
    lv_label_set_text(_lbl_tomato_status, "专注已暂停。休息一下吧");
    lv_obj_set_style_text_color(_lbl_tomato_status, LV_COLOR_WARNING, 0);
  } else {
    lv_label_set_text(_lbl_tomato_status, "拼搏与专注中，请勿打扰...");
    lv_obj_set_style_text_color(_lbl_tomato_status, LV_COLOR_TEXT_MUTED, 0);
  }
  
  loadScreen(_scr_tomato);
}

void ClassPetUI::showProcessingScreen(const String& statusText) {
  lv_label_set_text(_lbl_proc_text, statusText.c_str());
  lv_obj_add_flag(_bar_rec_vol, LV_OBJ_FLAG_HIDDEN); // 隐藏波形条
  
  loadScreen(_scr_processing);
}

void ClassPetUI::showRecordingScreen(int volumeDb) {
  lv_label_set_text(_lbl_proc_text, "🎙️ 正在录制您的语音...\n请说话 (点击屏幕停止)");
  lv_obj_remove_flag(_bar_rec_vol, LV_OBJ_FLAG_HIDDEN); // 显示波形条
  lv_bar_set_value(_bar_rec_vol, volumeDb, LV_ANIM_OFF);
  
  loadScreen(_scr_processing);
}

// ==========================================
// 6. 浮空 Toast 控制与定时自动销毁
// ==========================================
void ClassPetUI::showToast(const String& message, int duration_ms) {
  if (!_toast_container) return;
  
  lv_label_set_text(_toast_label, message.c_str());
  lv_obj_remove_flag(_toast_container, LV_OBJ_FLAG_HIDDEN);
  
  // 创建一个一次性定时器，时间到后自动隐藏 Toast 容器
  lv_timer_create(toastTimerCb, duration_ms, NULL);
}

void ClassPetUI::toastTimerCb(lv_timer_t* timer) {
  ClassPetUI& ui = ClassPetUI::getInstance();
  if (ui._toast_container) {
    lv_obj_add_flag(ui._toast_container, LV_OBJ_FLAG_HIDDEN);
  }
  // 销毁计时器本身
  lv_timer_delete(timer);
}

// 统一界面跳转驱动
void ClassPetUI::loadScreen(lv_obj_t* scr) {
  if (lv_screen_active() != scr) {
    lv_screen_load(scr);
  }
}

void ClassPetUI::setPetGif(const void* data, size_t size) {
  if (data == nullptr || size == 0) return;
  LcdDisplay::getInstance().lock();
  
  if (_gif_pet != nullptr) {
    lv_obj_del(_gif_pet);
    _gif_pet = nullptr;
  }
  
  // LVGL 9 要求内存图片资源必须包装为 lv_image_dsc_t 结构体
  memset(&_gif_dsc, 0, sizeof(lv_image_dsc_t));
  _gif_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  _gif_dsc.header.cf = LV_COLOR_FORMAT_RAW; // RAW 数据格式
  _gif_dsc.data_size = size;
  _gif_dsc.data = (const uint8_t*)data;
  
  // 为了保证层级和定位绝对正确，将动图挂载到卡片内部
  _gif_pet = lv_gif_create(_card_normal ? _card_normal : _scr_normal);
  lv_gif_set_src(_gif_pet, &_gif_dsc); // 传入结构体指针
  // 动图在卡片内的左侧垂直居中对齐，略微靠左避免遮挡文字
  lv_obj_align(_gif_pet, LV_ALIGN_LEFT_MID, -5, 0);
  
  LcdDisplay::getInstance().unlock();
}

void ClassPetUI::updateStatusBar(bool isOnline, const String& wifiName, int batteryPct, bool isCharging) {
  if (_lbl_normal_wifi) {
    if (isOnline) {
      lv_obj_set_style_text_color(_lbl_normal_wifi, LV_COLOR_SUCCESS, 0);
      lv_label_set_text_fmt(_lbl_normal_wifi, "在线 | %s", wifiName.c_str());
    } else {
      lv_obj_set_style_text_color(_lbl_normal_wifi, LV_COLOR_WARNING, 0);
      lv_label_set_text(_lbl_normal_wifi, "离线");
    }
  }

  if (_bar_battery && _lbl_normal_battery) {
    int pct = batteryPct;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    
    lv_bar_set_value(_bar_battery, pct, LV_ANIM_ON);
    
    if (isCharging) {
      lv_label_set_text_fmt(_lbl_normal_battery, "%d%%", pct);
      lv_obj_set_style_bg_color(_bar_battery, LV_COLOR_WARNING, LV_PART_INDICATOR); // 充电时显示橘黄色/黄色
    } else {
      lv_label_set_text_fmt(_lbl_normal_battery, "%d%%", pct);
      if (pct <= 20) {
        lv_obj_set_style_bg_color(_bar_battery, LV_COLOR_DANGER, LV_PART_INDICATOR);
      } else {
        lv_obj_set_style_bg_color(_bar_battery, LV_COLOR_SUCCESS, LV_PART_INDICATOR);
      }
    }
  }
}
