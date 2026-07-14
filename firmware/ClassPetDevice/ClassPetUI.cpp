/**
 * @file ClassPetUI.cpp
 * @brief 班级宠物园 LVGL 9.5 图形界面子系统实现
 */

#include "ClassPetUI.h"
#include "LcdDisplay.h"
#include "DeviceStateMachine.h"
#include "Config.h"
#include "ApiClient.h"
#include "Network.h"
#include "DeviceSettings.h"
#include "AudioHAL.h"
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <vector>
#include <ArduinoJson.h>
#include "cjk16_bin.h"   // 中文字库 (GB2312) 已编译进固件, 450KB
#include "time76_bin.h"  // 待机时钟时间高清字体 (76px ASCII 数字 + 冒号)
#include "date40_bin.h"  // 待机日期/星期高清字体 (40px CJK + 空格)

// 中文字库直接编译进固件 (tools/fonts/cjk16.bin -> cjk16_bin.c), 运行时通过
// lv_binfont_create_from_buffer() 从内存缓冲区加载。彻底绕开 TF 卡 / HTTP 下载,
// 避免之前出现的下载污染导致 lv_binfont 解析失败、中文显示方块的问题。
// (之前误判 "3MB 分区放不下" 是因为按 3.1MB 的 C 数组估算; 实际 bin 字体仅 450KB,
//  固件 1.76MB + 450KB = 2.2MB 远低于 huge_app 分区 3MB 上限。)
static const lv_font_t* g_cjk_font = nullptr;

// 待机时钟专用字体 (time76 用于时间, date40 用于日期)
static lv_font_t* g_standby_time_font = nullptr;
static lv_font_t* g_standby_date_font = nullptr;

// 息屏状态标志, 定义在 ClassPetDevice.ino
extern bool g_screen_off;

// LVGL 9 的 binfont 加载器/内存文件系统未通过 lvgl.h 导出, 这里直接声明其 C 链接原型
extern "C" {
    void lv_fs_memfs_init(void);
    lv_font_t* lv_binfont_create_from_buffer(void* buffer, uint32_t size);
    lv_font_t* lv_binfont_create(const char* path);
    void lv_fs_make_path_from_buffer(lv_fs_path_ex_t* path, char letter, const void* buf, uint32_t size, const char* ext);
    lv_fs_res_t lv_fs_open(lv_fs_file_t* file_p, const char* path, lv_fs_mode_t mode);
    lv_fs_res_t lv_fs_read(lv_fs_file_t* file_p, void* buf, uint32_t btr, uint32_t* br);
    lv_fs_res_t lv_fs_close(lv_fs_file_t* file_p);
    lv_result_t lv_fs_get_buffer_from_path(lv_fs_path_ex_t* path, void** buffer, uint32_t* size);
}

// ==========================================
// 自定义 LVGL 文件系统驱动: 直接通过 SD_MMC 读取 TF 卡
// 用于 lv_binfont_create("S:/cjk16.bin") 直接加载 bin 字体, 绕开 MEMFS 机制。
// ==========================================
static void* sd_fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    LV_UNUSED(drv);
    if(mode != LV_FS_MODE_RD) return NULL;
    fs::File* f = new fs::File;
    *f = SD_MMC.open(path, FILE_READ);
    if(!f->available()) {
        delete f;
        return NULL;
    }
    return f;
}

static lv_fs_res_t sd_fs_close(lv_fs_drv_t* drv, void* file_p) {
    LV_UNUSED(drv);
    fs::File* f = (fs::File*)file_p;
    if(f) {
        f->close();
        delete f;
    }
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_read(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    LV_UNUSED(drv);
    fs::File* f = (fs::File*)file_p;
    if(!f) return LV_FS_RES_INV_PARAM;
    int read = f->read((uint8_t*)buf, btr);
    if(read < 0) read = 0;
    if(br) *br = (uint32_t)read;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
    LV_UNUSED(drv);
    fs::File* f = (fs::File*)file_p;
    if(!f) return LV_FS_RES_INV_PARAM;
    uint32_t target = pos;
    if(whence == LV_FS_SEEK_CUR) {
        target = f->position() + pos;
    } else if(whence == LV_FS_SEEK_END) {
        target = f->size() - pos;
    }
    if(!f->seek(target)) return LV_FS_RES_UNKNOWN;
    return LV_FS_RES_OK;
}

static lv_fs_res_t sd_fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
    LV_UNUSED(drv);
    fs::File* f = (fs::File*)file_p;
    if(!f || !pos_p) return LV_FS_RES_INV_PARAM;
    *pos_p = f->position();
    return LV_FS_RES_OK;
}

static void lv_sd_fs_init() {
    static lv_fs_drv_t sd_drv;
    lv_fs_drv_init(&sd_drv);
    sd_drv.letter = 'S';
    sd_drv.cache_size = 0;  // 直接读取 SD 卡, 不走缓存
    sd_drv.open_cb = sd_fs_open;
    sd_drv.close_cb = sd_fs_close;
    sd_drv.read_cb = sd_fs_read;
    sd_drv.seek_cb = sd_fs_seek;
    sd_drv.tell_cb = sd_fs_tell;
    lv_fs_drv_register(&sd_drv);
    DEBUG_PRINTLN("📝 [字体] 已注册 SD 卡 LVGL 文件系统驱动 (S:)");
}

// 返回当前中文字体; 若 TF 卡字库尚未加载成功, 回退到内置默认字体(中文可能显示为方块)
static inline const lv_font_t* cjkFont() {
    return g_cjk_font ? g_cjk_font : LV_FONT_DEFAULT;
}

// 所有需要中文显示的标签都登记到这里, 字库下载完成后可统一刷新
// is_cjk 标记该标签用的是 cjkFont() (可能因字库下载而需刷新); 其它标签(如数字用 montserrat)保持原字体
struct CjkLabelEntry { lv_obj_t* obj; const lv_font_t* font; int part; bool is_cjk; };
static std::vector<CjkLabelEntry> s_cjk_labels;
static void setCjkFont(lv_obj_t* obj, const lv_font_t* font, int part) {
    if (!obj) return;
    lv_obj_set_style_text_font(obj, font, part);
    s_cjk_labels.push_back({obj, font, part, (font == LV_FONT_DEFAULT)});
}

// 从内置字库缓冲区 (cjk16_bin, 编译进固件) 加载中文字库。
// 该缓冲区位于 flash (const), 生命周期与程序一致, 可直接交给 lv_binfont 的
// MEMFS 缓冲加载器使用, 不依赖 TF 卡 / HTTP 下载, 不存在下载污染风险。
static void loadCjkFontFromSD() {
    DEBUG_PRINTF("📝 [字体] 尝试从内置字库缓冲区加载中文字库 (%u 字节)\n", cjk16_bin_len);
    g_cjk_font = lv_binfont_create_from_buffer((void*)cjk16_bin, (uint32_t)cjk16_bin_len);
    if (g_cjk_font) {
        DEBUG_PRINTLN("✅ [字体] 已从内置缓冲区加载中文字库");
    } else {
        DEBUG_PRINTLN("⚠️ [字体] 内置字库加载失败, 回退到内置默认字体(中文可能显示为方块)");
    }
}

// 加载待机/菜单用高清字体 (time76 数字 + date40 日期/星期)
static void loadStandbyFonts() {
    if (!g_standby_time_font) {
        DEBUG_PRINTF("📝 [字体] 加载待机时间字体 time76 (%u 字节)\n", time76_bin_len);
        g_standby_time_font = lv_binfont_create_from_buffer((void*)time76_bin, (uint32_t)time76_bin_len);
        if (!g_standby_time_font) {
            DEBUG_PRINTLN("⚠️ [字体] time76 加载失败, 回退到 Montserrat 48");
        }
    }
    if (!g_standby_date_font) {
        DEBUG_PRINTF("📝 [字体] 加载待机日期字体 date40 (%u 字节)\n", date40_bin_len);
        g_standby_date_font = lv_binfont_create_from_buffer((void*)date40_bin, (uint32_t)date40_bin_len);
        if (!g_standby_date_font) {
            DEBUG_PRINTLN("⚠️ [字体] date40 加载失败, 回退到默认 CJK 16");
        }
    }
}

// 字库下载完成后由 DeviceStateMachine 调用: 重新从 SD 加载并刷新所有中文标签
void ClassPetUI::reloadCjkFont() {
    loadCjkFontFromSD();  // 若 SD 已有 cjk16.bin (刚下载完), 会重建 g_cjk_font
    for (auto& e : s_cjk_labels) {
        // 仅当该标签原本使用 cjkFont() (内置子集/完整字库) 时才刷新为新字库;
        // 数字等使用 montserrat 的标签保持原字体不变
        lv_obj_set_style_text_font(e.obj, e.is_cjk ? cjkFont() : e.font, e.part);
    }
    DEBUG_PRINTF("🔄 [字体] 已刷新 %u 个标签\n", (unsigned)s_cjk_labels.size());
}

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
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_SETTINGS);
}

static void btn_voice_cb(lv_event_t* e) {
  LV_UNUSED(e);
  // 语音会话进行中(录音/识别/播放)时直接忽略, 不排队, 防止退出后残留事件触发重复倒计时
  if (DeviceStateMachine::getInstance().isVoiceSessionActive()) {
    return;
  }
  static uint32_t last_voice_click = 0;
  uint32_t now = millis();
  if (now - last_voice_click < 600) {
    return; // 600ms 内去抖, 避免重复触发
  }
  last_voice_click = now;
  DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_START);
}

// 语音覆盖层上的"结束录音"按钮回调
static void voice_overlay_stop_cb(lv_event_t* e) {
  LV_UNUSED(e);
  static uint32_t last_stop_click = 0;
  uint32_t now = millis();
  if (now - last_stop_click < 600) {
    return;
  }
  last_stop_click = now;
  DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_RECORD_DONE);
}

static void btn_tomato_pause_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_PAUSE_RESUME);
}

static void btn_tomato_exit_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_STOP);
}

// 移除旧的 lbl_tomato_time_cb

void ClassPetUI::init() {
  // 0. 注册 LVGL 文件系统驱动, 并从 TF 卡加载中文字库
  lv_fs_memfs_init();
  lv_sd_fs_init();       // 直接通过 SD 卡加载字库 (比 MEMFS 更可靠)
  loadCjkFontFromSD();
  loadStandbyFonts();    // 加载待机/菜单高清字体

  // 1. 初始化四个独立页面
  _scr_normal = lv_obj_create(NULL);
  _scr_diag = lv_obj_create(NULL);
  _scr_tomato = lv_obj_create(NULL);
  _scr_processing = lv_obj_create(NULL);
  _scr_menu = lv_obj_create(NULL);
  _scr_standby = lv_obj_create(NULL);
  _scr_settings = lv_obj_create(NULL);
  _scr_calendar = lv_obj_create(NULL);
  _scr_list = lv_obj_create(NULL);
  _scr_alarm = lv_obj_create(NULL);
  _scr_owner = lv_obj_create(NULL);
  
  // 2. 配置背景样式
  lv_obj_t* screens[] = { _scr_normal, _scr_diag, _scr_tomato, _scr_processing, _scr_menu, _scr_standby,
                          _scr_settings, _scr_calendar, _scr_list, _scr_alarm, _scr_owner };
  for (int i = 0; i < 11; i++) {
    lv_obj_set_style_bg_color(screens[i], LV_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(screens[i], LV_OPA_COVER, 0);
  }
  
  // 3. 构造各页面的静态子控件
  initNormalScreen();
  initDiagScreen();
  initTomatoScreen();
  initProcessingScreen();
  initMenuScreen();
  initStandbyScreen();
  initSettingsScreen();
  initCalendarScreen();
  initListScreen();
  initAlarmScreen();
  initOwnerScreen();
  
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
  setCjkFont(_toast_label, cjkFont(), 0); // 设置中文字体
  lv_obj_align(_toast_label, LV_ALIGN_CENTER, 0, 0);
  
  lv_obj_add_flag(_toast_container, LV_OBJ_FLAG_HIDDEN); // 默认隐藏
  
  // 默认启动载入自检诊断页
  loadScreen(_scr_diag);
}

void ClassPetUI::gesture_event_cb(lv_event_t* e) {
  LV_UNUSED(e);
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
  ClassPetUI& ui = ClassPetUI::getInstance();
  lv_obj_t* active_scr = lv_screen_active();
  
  if (dir == LV_DIR_TOP && active_scr == ui._scr_normal) {
    ui.loadScreen(ui._scr_menu);
  } else if (active_scr == ui._scr_menu) {
    // 菜单页任意方向滑动手势返回主页
    ui.loadScreen(ui._scr_normal);
  } else if (active_scr == ui._scr_standby) {
    // 待机下任意滑动手势唤醒设备
    g_screen_off = false;
    LcdDisplay::getInstance().resetActivityTime();
    LcdDisplay::getInstance().setBrightness(DeviceSettings::getBrightness());
    ui.forceSwitchToNormal();
  }
}

// ==========================================
// 1. 初始化主正常宠物页 (ScreenNormal)
// ==========================================
void ClassPetUI::initNormalScreen() {
  // 绑定全局手势
  lv_obj_add_event_cb(_scr_normal, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_clear_flag(_scr_normal, LV_OBJ_FLAG_SCROLLABLE);

  // A. 顶部状态栏 - WiFi
  _lbl_normal_wifi = lv_label_create(_scr_normal);
  lv_obj_set_pos(_lbl_normal_wifi, 8, 8);
  lv_obj_set_size(_lbl_normal_wifi, 220, 18);
  lv_label_set_long_mode(_lbl_normal_wifi, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(_lbl_normal_wifi, LV_COLOR_SUCCESS, 0);
  setCjkFont(_lbl_normal_wifi, cjkFont(), 0);
  lv_label_set_text(_lbl_normal_wifi, "未连接");
  
  // A. 顶部状态栏 - 电池外框 (右对齐)
  _bar_battery = lv_bar_create(_scr_normal);
  lv_obj_set_size(_bar_battery, 46, 18);
  lv_obj_align(_bar_battery, LV_ALIGN_TOP_RIGHT, -15, 6);
  
  lv_obj_set_style_bg_color(_bar_battery, lv_color_hex(0xEEEEEE), 0);
  lv_obj_set_style_bg_opa(_bar_battery, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_bar_battery, 1, 0);
  lv_obj_set_style_border_color(_bar_battery, LV_COLOR_TEXT, 0);
  lv_obj_set_style_radius(_bar_battery, 4, 0);
  lv_obj_set_style_pad_all(_bar_battery, 2, 0);
  
  lv_obj_set_style_bg_color(_bar_battery, LV_COLOR_SUCCESS, LV_PART_INDICATOR);
  lv_obj_set_style_radius(_bar_battery, 2, LV_PART_INDICATOR);
  lv_bar_set_range(_bar_battery, 0, 100);
  lv_bar_set_value(_bar_battery, 100, LV_ANIM_OFF);

  lv_obj_t* bat_cap = lv_obj_create(_scr_normal);
  lv_obj_set_size(bat_cap, 3, 8);
  lv_obj_align_to(bat_cap, _bar_battery, LV_ALIGN_OUT_RIGHT_MID, 1, 0);
  lv_obj_set_style_bg_color(bat_cap, LV_COLOR_TEXT, 0);
  lv_obj_set_style_border_width(bat_cap, 0, 0);
  lv_obj_set_style_radius(bat_cap, 1, 0);

  _lbl_normal_battery = lv_label_create(_bar_battery);
  lv_obj_center(_lbl_normal_battery);
  lv_obj_set_style_text_color(_lbl_normal_battery, lv_color_hex(0x222222), 0);
  setCjkFont(_lbl_normal_battery, cjkFont(), 0);
  lv_label_set_text(_lbl_normal_battery, "100%");
  
  // B. 正常屏幕时钟 (同行显示)
  _lbl_time = lv_label_create(_scr_normal);
  lv_obj_align(_lbl_time, LV_ALIGN_TOP_MID, -70, 35);
  lv_obj_set_style_text_color(_lbl_time, LV_COLOR_TEXT, 0);
  setCjkFont(_lbl_time, &lv_font_montserrat_24, 0);
  lv_label_set_text(_lbl_time, "00:00");
  
  _lbl_date = lv_label_create(_scr_normal);
  lv_obj_align(_lbl_date, LV_ALIGN_TOP_MID, 40, 41);
  lv_obj_set_style_text_color(_lbl_date, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(_lbl_date, cjkFont(), 0);
  lv_label_set_text(_lbl_date, "1月1日 星期一");

  // C. 宠物大卡片 (居中略微靠下)
  _card_normal = lv_obj_create(_scr_normal);
  lv_obj_t* card = _card_normal;
  lv_obj_set_size(card, 310, 140);
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -25);
  lv_obj_set_style_bg_color(card, LV_COLOR_CARD_BG, 0);
  lv_obj_set_style_border_color(card, LV_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_pad_all(card, 0, 0); // 取消内部 padding，防止内部文字被挤压变形
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE); // 允许卡片将滑动事件传递给屏幕，修复滑动不灵敏问题
  
  // 学生姓名标签
  _lbl_normal_name = lv_label_create(card);
  lv_obj_align(_lbl_normal_name, LV_ALIGN_TOP_LEFT, 130, 22);
  lv_obj_set_style_text_color(_lbl_normal_name, LV_COLOR_TEXT, 0);
  setCjkFont(_lbl_normal_name, cjkFont(), 0);
  lv_label_set_text(_lbl_normal_name, "加载中...");
  
  // 等级 & 积分标签
  _lbl_normal_lv = lv_label_create(card);
  lv_obj_align(_lbl_normal_lv, LV_ALIGN_TOP_LEFT, 130, 48);
  lv_obj_set_style_text_color(_lbl_normal_lv, LV_COLOR_WARNING, 0);
  setCjkFont(_lbl_normal_lv, cjkFont(), 0);
  lv_label_set_text(_lbl_normal_lv, "Lv.1 | 积分: 0");
  
  // 经验文字
  _lbl_normal_exp = lv_label_create(card);
  lv_obj_align(_lbl_normal_exp, LV_ALIGN_TOP_LEFT, 130, 75);
  lv_obj_set_style_text_color(_lbl_normal_exp, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(_lbl_normal_exp, cjkFont(), 0);
  lv_label_set_text(_lbl_normal_exp, "经验: 0 / 40");
  
  // 经验条
  _bar_normal_exp = lv_bar_create(card);
  lv_obj_set_size(_bar_normal_exp, 150, 10);
  lv_obj_align(_bar_normal_exp, LV_ALIGN_TOP_LEFT, 130, 102);
  lv_obj_set_style_bg_color(_bar_normal_exp, LV_COLOR_BORDER, 0);
  lv_obj_set_style_bg_color(_bar_normal_exp, LV_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_bar_set_range(_bar_normal_exp, 0, 100);
  lv_bar_set_value(_bar_normal_exp, 0, LV_ANIM_ON);

  // D. 提示文字: 划动进入菜单
  lv_obj_t* hint = lv_label_create(_scr_normal);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -3);
  lv_obj_set_style_text_color(hint, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(hint, cjkFont(), 0);
  lv_label_set_text(hint, "^ 上滑展开菜单");
}

// ==========================================
// 2. 初始化智能诊断网络页面 (ScreenDiag)
// ==========================================
void ClassPetUI::initDiagScreen() {
  // A. 顶部标题
  lv_obj_t* title = lv_label_create(_scr_diag);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_style_text_color(title, LV_COLOR_PRIMARY, 0);
  setCjkFont(title, cjkFont(), 0);
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
  setCjkFont(_lbl_diag_wifi, cjkFont(), 0);
  lv_label_set_text(_lbl_diag_wifi, "WiFi 状态: 连接中...");
  
  _lbl_diag_ip = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_ip, 10, 25);
  setCjkFont(_lbl_diag_ip, cjkFont(), 0);
  lv_label_set_text(_lbl_diag_ip, "本地 IP: 0.0.0.0");
  
  _lbl_diag_domain = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_domain, 10, 45);
  setCjkFont(_lbl_diag_domain, cjkFont(), 0);
  lv_label_set_text(_lbl_diag_domain, "服务器: pete.qqzy.de5.net");
  
  _lbl_diag_resolved = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_resolved, 10, 65);
  setCjkFont(_lbl_diag_resolved, cjkFont(), 0);
  lv_label_set_text(_lbl_diag_resolved, "DNS 解析: 获取中...");
  
  _lbl_diag_http = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_http, 10, 85);
  setCjkFont(_lbl_diag_http, cjkFont(), 0);
  lv_label_set_text(_lbl_diag_http, "HTTP 状态: 0");
  
  _lbl_diag_tls = lv_label_create(frame);
  lv_obj_set_pos(_lbl_diag_tls, 10, 105);
  setCjkFont(_lbl_diag_tls, cjkFont(), 0);
  lv_label_set_text(_lbl_diag_tls, "TLS 握手: 等待中");
  
  // C. 底部智能排障指南文字/设备 MAC 地址区域
  _lbl_diag_sugg = lv_label_create(_scr_diag);
  lv_obj_set_width(_lbl_diag_sugg, 290);
  lv_obj_align(_lbl_diag_sugg, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_set_style_text_color(_lbl_diag_sugg, LV_COLOR_WARNING, 0);
  lv_obj_set_style_text_align(_lbl_diag_sugg, LV_TEXT_ALIGN_CENTER, 0);
  setCjkFont(_lbl_diag_sugg, cjkFont(), 0);
  lv_label_set_text(_lbl_diag_sugg, "系统自愈中，请稍候...");
  
  _lbl_diag_mac = lv_label_create(_scr_diag);
  lv_obj_align(_lbl_diag_mac, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_text_color(_lbl_diag_mac, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(_lbl_diag_mac, cjkFont(), 0);
  lv_label_set_text(_lbl_diag_mac, "MAC: FF:FF:FF:FF:FF:FF");
}

// ==========================================
// 3. 初始化番茄计时器工作页面 (ScreenTomato)
// ==========================================
void ClassPetUI::initTomatoScreen() {
  // 移除_scr_tomato上的手势注册，防止触摸偏移被识别为手势而吃掉点击事件
  // lv_obj_add_event_cb(_scr_tomato, gesture_event_cb, LV_EVENT_GESTURE, NULL);

  // === 1. 设置界面容器 ===
  _cont_tomato_settings = lv_obj_create(_scr_tomato);
  lv_obj_set_size(_cont_tomato_settings, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(_cont_tomato_settings, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(_cont_tomato_settings, 0, 0);
  lv_obj_set_style_pad_all(_cont_tomato_settings, 0, 0);
  
  lv_obj_t* title = lv_label_create(_cont_tomato_settings);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_color(title, LV_COLOR_DANGER, 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "选择专注时长");

  // 添加滚轮，用户可以选择时间
  _roller_tomato_time = lv_roller_create(_cont_tomato_settings);
  lv_roller_set_options(_roller_tomato_time, 
    "5\n10\n15\n20\n25\n30\n45\n60\n90\n120", 
    LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(_roller_tomato_time, 3);
  lv_obj_align(_roller_tomato_time, LV_ALIGN_CENTER, 0, -15);
  lv_roller_set_selected(_roller_tomato_time, 4, LV_ANIM_OFF); // 默认25分钟

  // “开始专注” 按钮
  lv_obj_t* btn_start = lv_button_create(_cont_tomato_settings);
  lv_obj_set_size(btn_start, 110, 36);
  lv_obj_align(btn_start, LV_ALIGN_BOTTOM_RIGHT, -25, -10);
  lv_obj_set_style_bg_color(btn_start, LV_COLOR_PRIMARY, 0);
  lv_obj_add_event_cb(btn_start, [](lv_event_t* e) {
    ClassPetUI& ui = ClassPetUI::getInstance();
    lv_obj_add_flag(ui._cont_tomato_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui._cont_tomato_timer, LV_OBJ_FLAG_HIDDEN);
    DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_START);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* lbl_start = lv_label_create(btn_start);
  lv_label_set_text(lbl_start, "开始专注");
  setCjkFont(lbl_start, cjkFont(), 0);
  lv_obj_align(lbl_start, LV_ALIGN_CENTER, 0, 0);

  // “取消” 按钮
  lv_obj_t* btn_cancel = lv_button_create(_cont_tomato_settings);
  lv_obj_set_size(btn_cancel, 110, 36);
  lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 25, -10);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x475569), 0);
  lv_obj_add_event_cb(btn_cancel, [](lv_event_t* e) {
    DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_STOP);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_cancel, "取消");
  setCjkFont(lbl_cancel, cjkFont(), 0);
  lv_obj_align(lbl_cancel, LV_ALIGN_CENTER, 0, 0);

  // === 2. 倒计时界面容器 ===
  _cont_tomato_timer = lv_obj_create(_scr_tomato);
  lv_obj_set_size(_cont_tomato_timer, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(_cont_tomato_timer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(_cont_tomato_timer, 0, 0);
  lv_obj_set_style_pad_all(_cont_tomato_timer, 0, 0);
  lv_obj_add_flag(_cont_tomato_timer, LV_OBJ_FLAG_HIDDEN); // 默认隐藏

  // 顶部标题
  lv_obj_t* title_timer = lv_label_create(_cont_tomato_timer);
  lv_obj_align(title_timer, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_color(title_timer, LV_COLOR_DANGER, 0);
  setCjkFont(title_timer, cjkFont(), 0);
  lv_label_set_text(title_timer, "番茄专注时间");

  // 中心环形大进度条
  _arc_tomato_progress = lv_arc_create(_cont_tomato_timer);
  lv_obj_set_size(_arc_tomato_progress, 130, 130);
  lv_obj_align(_arc_tomato_progress, LV_ALIGN_CENTER, 0, -20);
  lv_arc_set_rotation(_arc_tomato_progress, 270); 
  lv_arc_set_bg_angles(_arc_tomato_progress, 0, 360);
  lv_obj_remove_style(_arc_tomato_progress, NULL, LV_PART_KNOB); 
  lv_obj_clear_flag(_arc_tomato_progress, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(_arc_tomato_progress, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_color(_arc_tomato_progress, LV_COLOR_BORDER, LV_PART_MAIN);
  lv_obj_set_style_arc_width(_arc_tomato_progress, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(_arc_tomato_progress, LV_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(_arc_tomato_progress, true, LV_PART_INDICATOR);
  lv_arc_set_range(_arc_tomato_progress, 0, 100);
  lv_arc_set_value(_arc_tomato_progress, 100);
  
  // 进度倒计时字样 (完全居中)
  _lbl_tomato_time = lv_label_create(_cont_tomato_timer);
  lv_obj_align(_lbl_tomato_time, LV_ALIGN_CENTER, 0, -20); // 放在圆圈中心
  lv_obj_set_style_text_color(_lbl_tomato_time, LV_COLOR_TEXT, 0);
  setCjkFont(_lbl_tomato_time, &lv_font_montserrat_24, 0);
  lv_label_set_text(_lbl_tomato_time, "25:00");
  
  // 专注状态小字 (移到圆环下方)
  _lbl_tomato_status = lv_label_create(_cont_tomato_timer);
  lv_obj_align(_lbl_tomato_status, LV_ALIGN_CENTER, 0, 60);
  lv_obj_set_style_text_color(_lbl_tomato_status, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(_lbl_tomato_status, cjkFont(), 0);
  lv_label_set_text(_lbl_tomato_status, "努力专注学习中...");
  
  // 底部操作按键 (暂停/继续，退出专注)
  lv_obj_t* btn_pause = lv_button_create(_cont_tomato_timer);
  lv_obj_set_size(btn_pause, 110, 36);
  lv_obj_align(btn_pause, LV_ALIGN_BOTTOM_LEFT, 25, -10);
  lv_obj_set_style_bg_color(btn_pause, lv_color_hex(0x475569), 0);
  lv_obj_set_style_bg_opa(btn_pause, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_pause, 8, 0);
  lv_obj_add_event_cb(btn_pause, btn_tomato_pause_cb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* lbl_p = lv_label_create(btn_pause);
  lv_label_set_text(lbl_p, "暂停/继续");
  setCjkFont(lbl_p, cjkFont(), 0);
  lv_obj_align(lbl_p, LV_ALIGN_CENTER, 0, 0);
  
  lv_obj_t* btn_exit = lv_button_create(_cont_tomato_timer);
  lv_obj_set_size(btn_exit, 110, 36);
  lv_obj_align(btn_exit, LV_ALIGN_BOTTOM_RIGHT, -25, -10);
  lv_obj_set_style_bg_color(btn_exit, LV_COLOR_DANGER, 0);
  lv_obj_set_style_bg_opa(btn_exit, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_exit, 8, 0);
  lv_obj_add_event_cb(btn_exit, btn_tomato_exit_cb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* lbl_e = lv_label_create(btn_exit);
  lv_label_set_text(lbl_e, "退出专注");
  setCjkFont(lbl_e, cjkFont(), 0);
  lv_obj_align(lbl_e, LV_ALIGN_CENTER, 0, 0);
}

uint32_t ClassPetUI::getSelectedTomatoTime() {
  if (_roller_tomato_time) {
    char buf[16];
    lv_roller_get_selected_str(_roller_tomato_time, buf, sizeof(buf));
    return String(buf).toInt();
  }
  return 25;
}

void ClassPetUI::showTomatoSettings() {
  lv_obj_clear_flag(_cont_tomato_settings, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(_cont_tomato_timer, LV_OBJ_FLAG_HIDDEN);
  loadScreen(_scr_tomato);
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
  setCjkFont(_lbl_proc_text, cjkFont(), 0);
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
  
  // 允许点击屏幕停止录音
  lv_obj_add_flag(_scr_processing, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(_scr_processing, [](lv_event_t* e) {
    DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_RECORD_DONE);
  }, LV_EVENT_CLICKED, NULL);
}

// ==========================================
// 新增：初始化菜单界面 (ScreenMenu) — 可滚动功能列表
// ==========================================
void ClassPetUI::initMenuScreen() {
  lv_obj_add_event_cb(_scr_menu, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_clear_flag(_scr_menu, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(_scr_menu);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_text_color(title, LV_COLOR_TEXT, 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "功能菜单");

  // 左上角返回按钮
  lv_obj_t* btn_back = lv_btn_create(_scr_menu);
  lv_obj_set_size(btn_back, 44, 28);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 6, 4);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_back, 8, 0);
  lv_obj_set_style_border_width(btn_back, 0, 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){
    LV_UNUSED(e);
    ClassPetUI::getInstance().loadScreen(ClassPetUI::getInstance()._scr_normal);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_back, LV_COLOR_TEXT, 0);
  lv_obj_center(lbl_back);

  // 手机 APP 风格图标网格: 3 列, 适配横屏 320x240, 一页放下所有功能
  lv_obj_t* grid = lv_obj_create(_scr_menu);
  lv_obj_set_size(grid, 318, 206);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 4, 0);
  lv_obj_set_style_pad_row(grid, 6, 0);
  lv_obj_set_style_pad_column(grid, 8, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  // 帮助函数：创建一个 APP 风格图标磁贴 (FontAwesome 符号 + 中文标签)
  auto mkTile = [&](const char* label, const char* icon, uint32_t color, void (*cb)(lv_event_t*)) {
    lv_obj_t* tile = lv_obj_create(grid);
    lv_obj_set_size(tile, 98, 62);
    lv_obj_set_style_bg_color(tile, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_shadow_width(tile, 6, 0);
    lv_obj_set_style_shadow_color(tile, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_opa(tile, 70, 0);
    lv_obj_set_style_shadow_ofs_y(tile, 2, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);

    // 图标 (20px FontAwesome 符号, 白色, 居中偏上)
    lv_obj_t* icon_lbl = lv_label_create(tile);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0, -8);

    // 短标签 (16px CJK, 2 字, 居中偏下)
    lv_obj_t* l = lv_label_create(tile);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    setCjkFont(l, cjkFont(), 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 10);
    return tile;
  };

  mkTile("番茄", LV_SYMBOL_PAUSE, 0xFF9F43, btn_tomato_cb);
  mkTile("语音", LV_SYMBOL_AUDIO, 0x26DE81, btn_voice_cb);
  mkTile("日历", LV_SYMBOL_EDIT, 0x4F8DFD, [](lv_event_t* e){ ClassPetUI::getInstance().showCalendar(); });
  mkTile("清单", LV_SYMBOL_LIST, 0x9B59F6, [](lv_event_t* e){ ClassPetUI::getInstance().showList(); });
  mkTile("闹铃", LV_SYMBOL_BELL, 0xE67E22, [](lv_event_t* e){ ClassPetUI::getInstance().showAlarm(); });
  mkTile("记忆", LV_SYMBOL_SAVE, 0xEC407A, [](lv_event_t* e){ ClassPetUI::getInstance().showOwnerMemory(); });
  mkTile("设置", LV_SYMBOL_SETTINGS, 0x64748B, [](lv_event_t* e){ ClassPetUI::getInstance().showSettings(); });
}

// 待机屏幕点击/按压唤醒回调
static void standby_wake_event_cb(lv_event_t* e) {
  LV_UNUSED(e);
  g_screen_off = false;
  LcdDisplay::getInstance().resetActivityTime();
  LcdDisplay::getInstance().setBrightness(DeviceSettings::getBrightness());
  ClassPetUI::getInstance().forceSwitchToNormal();
}

// ==========================================
// 新增：初始化待机时钟界面 (ScreenStandby)
// ==========================================
void ClassPetUI::initStandbyScreen() {
  lv_obj_add_event_cb(_scr_standby, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  // 点击/按压也能唤醒, 防止用户单纯点击无法退出待机
  lv_obj_add_event_cb(_scr_standby, standby_wake_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(_scr_standby, standby_wake_event_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(_scr_standby, standby_wake_event_cb, LV_EVENT_LONG_PRESSED, NULL);
  lv_obj_clear_flag(_scr_standby, LV_OBJ_FLAG_SCROLLABLE);

  // 纯黑背景省电 + 防止闪眼
  lv_obj_set_style_bg_color(_scr_standby, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_scr_standby, LV_OPA_COVER, 0); // 确保不透明, 遮住下方内容

  // 时间: 76px 原生高清 ASCII 字体 (不缩放, 高清不模糊)
  _lbl_standby_time = lv_label_create(_scr_standby);
  lv_obj_set_style_text_color(_lbl_standby_time, lv_color_white(), 0);
  lv_obj_set_style_text_font(_lbl_standby_time, g_standby_time_font ? g_standby_time_font : &lv_font_montserrat_48, 0);
  lv_label_set_text(_lbl_standby_time, "00:00");
  lv_obj_align(_lbl_standby_time, LV_ALIGN_CENTER, 0, -22);
  // 时间标签也接收点击事件, 防止点击时间区域不唤醒
  lv_obj_add_event_cb(_lbl_standby_time, standby_wake_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(_lbl_standby_time, standby_wake_event_cb, LV_EVENT_PRESSED, NULL);

  // 日期: 40px 原生高清 CJK 字体
  _lbl_standby_date = lv_label_create(_scr_standby);
  lv_obj_set_style_text_color(_lbl_standby_date, lv_color_hex(0xcccccc), 0);
  lv_obj_set_style_text_font(_lbl_standby_date, g_standby_date_font ? g_standby_date_font : cjkFont(), 0);
  lv_label_set_text(_lbl_standby_date, "1月1日 星期一");
  lv_obj_align(_lbl_standby_date, LV_ALIGN_CENTER, 0, 48);
  lv_obj_add_event_cb(_lbl_standby_date, standby_wake_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(_lbl_standby_date, standby_wake_event_cb, LV_EVENT_PRESSED, NULL);
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
  
  // 防止在菜单页、待机页、番茄钟页、功能页时，被后台数据刷新强制拉回主页（闪退现象）
  lv_obj_t* active_scr = lv_screen_active();
  if (active_scr != _scr_menu && active_scr != _scr_standby && active_scr != _scr_tomato &&
      active_scr != _scr_settings && active_scr != _scr_calendar && active_scr != _scr_list &&
      active_scr != _scr_alarm && active_scr != _scr_owner) {
    loadScreen(_scr_normal);
  }
}

void ClassPetUI::updateClock(const String& timeStr, const String& dateStr) {
  if (_lbl_time) lv_label_set_text(_lbl_time, timeStr.c_str());
  if (_lbl_date) lv_label_set_text(_lbl_date, dateStr.c_str());

  if (_lbl_standby_time) lv_label_set_text(_lbl_standby_time, timeStr.c_str());
  if (_lbl_standby_date) lv_label_set_text(_lbl_standby_date, dateStr.c_str());
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
  
  lv_arc_set_value(_arc_tomato_progress, percent);
    // （宠物 GIF 已从番茄钟界面移除，避免遮挡时间）


  
  if (isPaused) {
    lv_label_set_text(_lbl_tomato_status, "专注已暂停。休息一下吧");
    lv_obj_set_style_text_color(_lbl_tomato_status, LV_COLOR_WARNING, 0);
  } else {
    lv_label_set_text(_lbl_tomato_status, "拼搏与专注中，请勿打扰...");
    lv_obj_set_style_text_color(_lbl_tomato_status, LV_COLOR_TEXT_MUTED, 0);
  }
  
  // REMOVED: Pet GIF is no longer reparented to Tomato Timer
  
  loadScreen(_scr_tomato);
}

void ClassPetUI::showProcessingScreen(const String& statusText) {
  lv_label_set_text(_lbl_proc_text, statusText.c_str());
  lv_obj_add_flag(_bar_rec_vol, LV_OBJ_FLAG_HIDDEN); // 隐藏波形条
  
  loadScreen(_scr_processing);
}

void ClassPetUI::showRecordingScreen(int volumeDb) {
  lv_label_set_text(_lbl_proc_text, "正在录制您的语音...\n请说话 (点击屏幕停止)");
  lv_obj_remove_flag(_bar_rec_vol, LV_OBJ_FLAG_HIDDEN); // 显示波形条
  lv_bar_set_value(_bar_rec_vol, volumeDb, LV_ANIM_OFF);

  loadScreen(_scr_processing);
}

// ==========================================
// P0: 语音期间"宠物常驻 + 实时字幕"覆盖层
// 约定: 调用方必须已持有 LVGL_LOCK (与 showProcessingScreen/showRecordingScreen 一致)。
// 覆盖层作为当前屏(普通屏)的子对象, 背景透明, 底层宠物 GIF 保持可见;
// 顶层显示 状态标题 + 声波条 + "你说/宠物说" 字幕气泡。
// ==========================================
void ClassPetUI::enterVoiceOverlay(const String& title) {
  // 若已存在则先销毁, 保证幂等
  if (_voice_overlay) {
    lv_obj_del(_voice_overlay);
    _voice_overlay = nullptr;
  }
  lv_obj_t* parent = lv_scr_act();
  if (!parent) return;

  _voice_overlay = lv_obj_create(parent);
  lv_obj_set_size(_voice_overlay, 240, 320);
  lv_obj_set_pos(_voice_overlay, 0, 0);
  lv_obj_set_style_bg_color(_voice_overlay, LV_COLOR_CARD_BG, 0); // 白底，遮住下层菜单
  lv_obj_set_style_bg_opa(_voice_overlay, LV_OPA_COVER, 0);       // 不透明，避免按钮透底
  lv_obj_set_style_border_width(_voice_overlay, 0, 0);
  lv_obj_set_style_pad_all(_voice_overlay, 0, 0);
  lv_obj_clear_flag(_voice_overlay, LV_OBJ_FLAG_SCROLLABLE);
  // 覆盖层 clickable 用于阻挡触摸穿透到底层菜单, 停止操作请用显式按钮
  lv_obj_add_flag(_voice_overlay, LV_OBJ_FLAG_CLICKABLE);

  // 顶部状态标题
  _voice_title = lv_label_create(_voice_overlay);
  lv_obj_set_width(_voice_title, 220);
  lv_obj_align(_voice_title, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_set_style_text_color(_voice_title, LV_COLOR_TEXT, 0);
  lv_obj_set_style_text_align(_voice_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(_voice_title, cjkFont(), 0);
  lv_label_set_long_mode(_voice_title, LV_LABEL_LONG_WRAP);
  lv_label_set_text(_voice_title, title.c_str());

  // 倒计时提示(录音阶段显示, 识别阶段用于"识别中...")
  _voice_countdown = lv_label_create(_voice_overlay);
  lv_obj_set_width(_voice_countdown, 220);
  lv_obj_align(_voice_countdown, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_text_color(_voice_countdown, LV_COLOR_TEXT_MUTED, 0);
  lv_obj_set_style_text_align(_voice_countdown, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(_voice_countdown, cjkFont(), 0);
  lv_label_set_text(_voice_countdown, "准备...");

  // 声波/音量条
  _voice_level = lv_bar_create(_voice_overlay);
  lv_obj_set_size(_voice_level, 200, 14);
  lv_obj_align(_voice_level, LV_ALIGN_TOP_MID, 0, 78);
  lv_obj_set_style_bg_color(_voice_level, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_bg_color(_voice_level, LV_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_obj_set_style_radius(_voice_level, 7, 0);
  lv_bar_set_range(_voice_level, 0, 100);
  lv_bar_set_value(_voice_level, 0, LV_ANIM_OFF);

  // 识别阶段显示的 spinner(默认隐藏)
  _voice_processing_spinner = lv_spinner_create(_voice_overlay);
  lv_obj_set_size(_voice_processing_spinner, 50, 50);
  lv_obj_align(_voice_processing_spinner, LV_ALIGN_TOP_MID, 0, 105);
  lv_obj_set_style_arc_color(_voice_processing_spinner, LV_COLOR_PRIMARY, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(_voice_processing_spinner, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_add_flag(_voice_processing_spinner, LV_OBJ_FLAG_HIDDEN);

  // 显式的"结束录音"大按钮(替代不明显的"点击屏幕停止")
  _voice_stop_btn = lv_button_create(_voice_overlay);
  lv_obj_set_size(_voice_stop_btn, 180, 50);
  lv_obj_align(_voice_stop_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
  lv_obj_set_style_bg_color(_voice_stop_btn, LV_COLOR_DANGER, 0);
  lv_obj_set_style_bg_opa(_voice_stop_btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(_voice_stop_btn, 25, 0);
  lv_obj_add_event_cb(_voice_stop_btn, voice_overlay_stop_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_clear_flag(_voice_stop_btn, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl_stop = lv_label_create(_voice_stop_btn);
  lv_obj_set_style_text_color(lbl_stop, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_stop, cjkFont(), 0);
  lv_label_set_text(lbl_stop, "结束录音");
  lv_obj_center(lbl_stop);

  // "你说" 字幕气泡
  _voice_you = lv_label_create(_voice_overlay);
  lv_obj_set_width(_voice_you, 224);
  lv_obj_align(_voice_you, LV_ALIGN_BOTTOM_MID, 0, -90);
  lv_obj_set_style_text_color(_voice_you, LV_COLOR_TEXT, 0);
  lv_obj_set_style_text_align(_voice_you, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(_voice_you, cjkFont(), 0);
  lv_label_set_long_mode(_voice_you, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_bg_color(_voice_you, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(_voice_you, LV_OPA_80, 0);
  lv_obj_set_style_radius(_voice_you, 8, 0);
  lv_obj_set_style_pad_all(_voice_you, 6, 0);
  lv_label_set_text(_voice_you, "");

  // "宠物说" 字幕气泡
  _voice_pet = lv_label_create(_voice_overlay);
  lv_obj_set_width(_voice_pet, 224);
  lv_obj_align(_voice_pet, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_text_color(_voice_pet, lv_color_hex(0x1E293B), 0);
  lv_obj_set_style_text_align(_voice_pet, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(_voice_pet, cjkFont(), 0);
  lv_label_set_long_mode(_voice_pet, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_bg_color(_voice_pet, LV_COLOR_PRIMARY, 0);
  lv_obj_set_style_bg_opa(_voice_pet, LV_OPA_20, 0);
  lv_obj_set_style_radius(_voice_pet, 8, 0);
  lv_obj_set_style_pad_all(_voice_pet, 6, 0);
  lv_label_set_text(_voice_pet, "");
}

void ClassPetUI::setVoiceOverlayTitle(const String& title) {
  if (_voice_title) lv_label_set_text(_voice_title, title.c_str());
}

void ClassPetUI::setVoiceOverlayLevel(int db) {
  if (_voice_level) lv_bar_set_value(_voice_level, db, LV_ANIM_OFF);
}

void ClassPetUI::setVoiceOverlayCountdown(int seconds) {
  if (_voice_countdown) {
    String s = "剩余 " + String(seconds) + " 秒";
    lv_label_set_text(_voice_countdown, s.c_str());
  }
}

void ClassPetUI::setVoiceOverlayProcessing(bool processing) {
  if (processing) {
    if (_voice_level) lv_obj_add_flag(_voice_level, LV_OBJ_FLAG_HIDDEN);
    if (_voice_stop_btn) lv_obj_add_flag(_voice_stop_btn, LV_OBJ_FLAG_HIDDEN);
    if (_voice_countdown) {
      lv_obj_set_style_text_color(_voice_countdown, LV_COLOR_PRIMARY, 0);
      lv_label_set_text(_voice_countdown, "识别中...");
    }
    if (_voice_processing_spinner) lv_obj_remove_flag(_voice_processing_spinner, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (_voice_level) lv_obj_remove_flag(_voice_level, LV_OBJ_FLAG_HIDDEN);
    if (_voice_stop_btn) lv_obj_remove_flag(_voice_stop_btn, LV_OBJ_FLAG_HIDDEN);
    if (_voice_countdown) {
      lv_obj_set_style_text_color(_voice_countdown, LV_COLOR_TEXT_MUTED, 0);
      lv_label_set_text(_voice_countdown, "准备...");
    }
    if (_voice_processing_spinner) lv_obj_add_flag(_voice_processing_spinner, LV_OBJ_FLAG_HIDDEN);
  }
}

void ClassPetUI::setVoiceOverlayCaption(bool isUser, const String& text) {
  if (isUser) {
    if (_voice_you) {
      String s = "你说: " + text;
      lv_label_set_text(_voice_you, s.c_str());
    }
  } else {
    if (_voice_pet) {
      String s = "宠物: " + text;
      lv_label_set_text(_voice_pet, s.c_str());
    }
  }
}

void ClassPetUI::exitVoiceOverlay() {
  if (_voice_overlay) {
    lv_obj_del(_voice_overlay);
    _voice_overlay = nullptr;
    _voice_title = nullptr;
    _voice_countdown = nullptr;
    _voice_level = nullptr;
    _voice_stop_btn = nullptr;
    _voice_processing_spinner = nullptr;
    _voice_you = nullptr;
    _voice_pet = nullptr;
  }
}

// ==========================================
// 6. 浮空 Toast 控制与定时自动销毁
// ==========================================
void ClassPetUI::showToast(const String& message, int duration_ms) {
  if (!_toast_container) return;
  
  lv_label_set_text(_toast_label, message.c_str());
  lv_obj_remove_flag(_toast_container, LV_OBJ_FLAG_HIDDEN);
  
  // P2 修复: 复用同一个定时器 (而非每次新建), 避免多个 Toast 叠加时
  // 第二个定时器提前把容器隐藏, 导致前一个 Toast 被过早清除。
  if (_toast_timer) {
    lv_timer_delete(_toast_timer);
    _toast_timer = nullptr;
  }
  _toast_timer = lv_timer_create(toastTimerCb, duration_ms, NULL);
}

void ClassPetUI::toastTimerCb(lv_timer_t* timer) {
  ClassPetUI& ui = ClassPetUI::getInstance();
  if (ui._toast_container) {
    lv_obj_add_flag(ui._toast_container, LV_OBJ_FLAG_HIDDEN);
  }
  ui._toast_timer = nullptr;  // 标记定时器已结束, 下次 showToast 可重建
  // 销毁计时器本身
  lv_timer_delete(timer);
}

// 统一界面跳转驱动
void ClassPetUI::loadScreen(lv_obj_t* scr) {
  if (lv_screen_active() != scr) {
    lv_screen_load(scr);
  }
}

void ClassPetUI::forceSwitchToNormal() {
  loadScreen(_scr_normal);
}

void ClassPetUI::setPetGif(const void* data, size_t size) {
  if (data == nullptr || size == 0) return;
  Serial.println("🎨 [UI] setPetGif: 准备获取 LVGL 互斥锁...");
  LcdDisplay::getInstance().lock();
  Serial.println("🎨 [UI] setPetGif: 已获取 LVGL 互斥锁，正在删除旧 GIF...");
  
  if (_gif_pet != nullptr) {
    lv_obj_del(_gif_pet);
    _gif_pet = nullptr;
  }
  
  Serial.println("🎨 [UI] setPetGif: 准备初始化 lv_image_dsc_t...");
  memset(&_gif_dsc, 0, sizeof(lv_image_dsc_t));
  _gif_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  _gif_dsc.header.cf = LV_COLOR_FORMAT_RAW; // RAW 数据格式
  _gif_dsc.data_size = size;
  _gif_dsc.data = (const uint8_t*)data;
  
  Serial.println("🎨 [UI] setPetGif: 准备调用 lv_gif_create 创建 GIF 控件...");
  _gif_pet = lv_gif_create(_card_normal);
  Serial.println("🎨 [UI] setPetGif: lv_gif_create 创建完成，准备设置数据源...");
  lv_gif_set_src(_gif_pet, &_gif_dsc); // 传入结构体指针
  Serial.println("🎨 [UI] setPetGif: 数据源设置完成，对齐控件中...");
  // 动图在卡片内的左侧垂直居中对齐，略微靠左
  lv_obj_align(_gif_pet, LV_ALIGN_LEFT_MID, 5, 0);
  
  Serial.println("🎨 [UI] setPetGif: 准备释放 LVGL 互斥锁...");
  LcdDisplay::getInstance().unlock();
  Serial.println("🎨 [UI] setPetGif: 已释放 LVGL 互斥锁，执行完成！");
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

// ==========================================
// 新增：功能屏通用列表辅助
// ==========================================
extern volatile uint8_t g_featureReq;  // 定义在 ClassPetDevice.ino
extern AudioHAL* audio;                 // 定义在 ClassPetDevice.ino

void ClassPetUI::clearList(lv_obj_t* scroll) {
  if (!scroll) return;
  lv_obj_clean(scroll);
}

lv_obj_t* ClassPetUI::addListRow(lv_obj_t* scroll, const String& title, const String& sub, bool dim) {
  if (!scroll) return nullptr;
  lv_obj_t* row = lv_obj_create(scroll);
  lv_obj_set_width(row, 290);
  lv_obj_set_style_bg_color(row, LV_COLOR_CARD_BG, 0);
  lv_obj_set_style_border_color(row, LV_COLOR_BORDER, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_pad_all(row, 6, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t* t = lv_label_create(row);
  lv_obj_set_width(t, 278);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  setCjkFont(t, cjkFont(), 0);
  lv_label_set_text(t, title.c_str());
  lv_obj_set_style_text_color(t, dim ? LV_COLOR_TEXT_MUTED : LV_COLOR_TEXT, 0);

  if (sub.length()) {
    lv_obj_t* s = lv_label_create(row);
    lv_obj_set_width(s, 278);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    setCjkFont(s, cjkFont(), 0);
    lv_label_set_text(s, sub.c_str());
    lv_obj_set_style_text_color(s, LV_COLOR_TEXT_MUTED, 0);
  }
  return row;
}

// ==========================================
// 设置屏 (Settings)
// ==========================================
void ClassPetUI::initSettingsScreen() {
  lv_obj_t* title = lv_label_create(_scr_settings);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_text_color(title, LV_COLOR_TEXT, 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "设备设置");

  // 左上角返回按钮
  lv_obj_t* btn_back = lv_button_create(_scr_settings);
  lv_obj_set_size(btn_back, 44, 28);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 6, 4);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x475569), 0);
  lv_obj_set_style_bg_opa(btn_back, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn_back, 8, 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ ClassPetUI::getInstance().loadScreen(ClassPetUI::getInstance()._scr_menu); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_b = lv_label_create(btn_back);
  lv_label_set_text(lbl_b, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(lbl_b, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_b, lv_color_white(), 0);
  lv_obj_center(lbl_b);

  // 设备信息只读卡片（与调整项分离，避免重叠）
  lv_obj_t* info_card = lv_obj_create(_scr_settings);
  lv_obj_set_size(info_card, 308, 56);
  lv_obj_align(info_card, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_bg_color(info_card, lv_color_hex(0xF1F5F9), 0);
  lv_obj_set_style_bg_opa(info_card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(info_card, 10, 0);
  lv_obj_set_style_border_width(info_card, 0, 0);
  lv_obj_set_style_pad_all(info_card, 6, 0);
  lv_obj_set_style_pad_row(info_card, 4, 0);
  lv_obj_set_flex_flow(info_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);

  _lbl_machine_code = lv_label_create(info_card);
  lv_obj_set_width(_lbl_machine_code, 290);
  lv_obj_set_style_text_color(_lbl_machine_code, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(_lbl_machine_code, cjkFont(), 0);
  lv_label_set_text(_lbl_machine_code, "机器码: -");

  _lbl_dev_status = lv_label_create(info_card);
  lv_obj_set_width(_lbl_dev_status, 290);
  lv_obj_set_style_text_color(_lbl_dev_status, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(_lbl_dev_status, cjkFont(), 0);
  lv_label_set_text(_lbl_dev_status, "状态: -");

  _lbl_fw_info = lv_label_create(info_card);
  lv_obj_set_width(_lbl_fw_info, 290);
  lv_obj_set_style_text_color(_lbl_fw_info, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(_lbl_fw_info, cjkFont(), 0);
  lv_label_set_text(_lbl_fw_info, "固件: -");

  _lbl_sys_info = lv_label_create(info_card);
  lv_obj_set_width(_lbl_sys_info, 290);
  lv_obj_set_style_text_color(_lbl_sys_info, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(_lbl_sys_info, cjkFont(), 0);
  lv_label_set_text(_lbl_sys_info, "系统: -");

  // 可滚动设置区（音量/亮度/待机/息屏）
  lv_obj_t* cont = lv_obj_create(_scr_settings);
  lv_obj_set_size(cont, 308, 144);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 4, 0);
  lv_obj_set_style_pad_row(cont, 6, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  auto mkSlider = [&](const char* label, int min, int max, int val, lv_obj_t*& slider, lv_obj_t*& valLbl) {
    lv_obj_t* box = lv_obj_create(cont);
    lv_obj_set_width(box, 296);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* top = lv_obj_create(box);
    lv_obj_set_width(top, 280);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* l = lv_label_create(top);
    lv_obj_set_style_text_color(l, LV_COLOR_TEXT, 0);
    setCjkFont(l, cjkFont(), 0);
    lv_label_set_text(l, label);

    valLbl = lv_label_create(top);
    lv_obj_set_style_text_color(valLbl, LV_COLOR_PRIMARY, 0);
    setCjkFont(valLbl, cjkFont(), 0);
    lv_label_set_text_fmt(valLbl, "%d", val);

    slider = lv_slider_create(box);
    lv_obj_set_size(slider, 280, 16);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, LV_ANIM_OFF);
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR);
    return box;
  };

  mkSlider("音量", 0, 100, DeviceSettings::getVolume(), _slider_volume, _lbl_volume_val);
  mkSlider("亮度", 0, 100, DeviceSettings::getBrightness(), _slider_brightness, _lbl_brightness_val);
  mkSlider("待机(秒)", 0, 300, DeviceSettings::getStandbySeconds(), _slider_standby, _lbl_standby_val);
  mkSlider("息屏(秒)", 0, 600, DeviceSettings::getScreenOffSeconds(), _slider_screenoff, _lbl_screenoff_val);

  auto sliderCb = [](lv_event_t* e) {
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    int v = lv_slider_get_value(slider);
    ClassPetUI& ui = ClassPetUI::getInstance();
    if (slider == ui._slider_volume) {
      DeviceSettings::setVolume(v);
      lv_label_set_text_fmt(ui._lbl_volume_val, "%d", v);
      if (audio) audio->setVolume(v);
    } else if (slider == ui._slider_brightness) {
      DeviceSettings::setBrightness(v);
      lv_label_set_text_fmt(ui._lbl_brightness_val, "%d", v);
      LcdDisplay::getInstance().setBrightness(v);
    } else if (slider == ui._slider_standby) {
      DeviceSettings::setStandbySeconds(v);
      lv_label_set_text_fmt(ui._lbl_standby_val, "%d", v);
    } else if (slider == ui._slider_screenoff) {
      DeviceSettings::setScreenOffSeconds(v);
      lv_label_set_text_fmt(ui._lbl_screenoff_val, "%d", v);
    }
  };
  lv_obj_add_event_cb(_slider_volume, sliderCb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(_slider_brightness, sliderCb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(_slider_standby, sliderCb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(_slider_screenoff, sliderCb, LV_EVENT_VALUE_CHANGED, NULL);
}

void ClassPetUI::showSettings() {
  refreshSettingsInfo();
  loadScreen(_scr_settings);
}

void ClassPetUI::refreshSettingsInfo() {
  if (_lbl_machine_code) {
    lv_label_set_text_fmt(_lbl_machine_code, "机器码: %s", Network::getMacAddress().c_str());
  }
  if (_lbl_dev_status) {
    String s = Network::isConnected() ? ("在线 | " + Network::getLocalIP()) : "离线";
    lv_label_set_text_fmt(_lbl_dev_status, "状态: %s", s.c_str());
  }
  if (_lbl_fw_info) {
    lv_label_set_text_fmt(_lbl_fw_info, "固件: v%s", DeviceSettings::getFirmwareVersion().c_str());
  }
  if (_lbl_sys_info) {
    char buf[64];
    snprintf(buf, sizeof(buf), "系统: %s | 空闲 %uKB", ESP.getChipModel(), ESP.getFreeHeap()/1024);
    lv_label_set_text(_lbl_sys_info, buf);
  }
}

// ==========================================
// 日历屏
// ==========================================
void ClassPetUI::initCalendarScreen() {
  lv_obj_t* title = lv_label_create(_scr_calendar);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_text_color(title, LV_COLOR_TEXT, 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "我的日历");

  _list_calendar = lv_obj_create(_scr_calendar);
  lv_obj_set_size(_list_calendar, 300, 200);
  lv_obj_align(_list_calendar, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_bg_opa(_list_calendar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(_list_calendar, 0, 0);
  lv_obj_set_style_pad_all(_list_calendar, 4, 0);
  lv_obj_set_style_pad_row(_list_calendar, 6, 0);
  lv_obj_set_flex_flow(_list_calendar, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(_list_calendar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hint = lv_label_create(_scr_calendar);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_text_color(hint, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(hint, cjkFont(), 0);
  lv_label_set_text(hint, "可在网页后台或语音申报添加");

  lv_obj_t* btn_back = lv_button_create(_scr_calendar);
  lv_obj_set_size(btn_back, 70, 28);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 8, 6);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x475569), 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ ClassPetUI::getInstance().loadScreen(ClassPetUI::getInstance()._scr_menu); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_b = lv_label_create(btn_back); lv_label_set_text(lbl_b, "返回"); setCjkFont(lbl_b, cjkFont(), 0); lv_obj_center(lbl_b);

  lv_obj_t* btn_ref = lv_button_create(_scr_calendar);
  lv_obj_set_size(btn_ref, 70, 28);
  lv_obj_align(btn_ref, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_set_style_bg_color(btn_ref, LV_COLOR_PRIMARY, 0);
  lv_obj_add_event_cb(btn_ref, [](lv_event_t* e){ ClassPetUI::getInstance().markFeatureSync(1); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_r = lv_label_create(btn_ref); lv_label_set_text(lbl_r, "刷新"); setCjkFont(lbl_r, cjkFont(), 0); lv_obj_center(lbl_r);
}

void ClassPetUI::showCalendar() {
  loadScreen(_scr_calendar);
  markFeatureSync(1);
}

// ==========================================
// 清单屏
// ==========================================
void ClassPetUI::initListScreen() {
  lv_obj_t* title = lv_label_create(_scr_list);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_text_color(title, LV_COLOR_TEXT, 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "我的清单");

  _list_checklist = lv_obj_create(_scr_list);
  lv_obj_set_size(_list_checklist, 300, 200);
  lv_obj_align(_list_checklist, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_bg_opa(_list_checklist, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(_list_checklist, 0, 0);
  lv_obj_set_style_pad_all(_list_checklist, 4, 0);
  lv_obj_set_style_pad_row(_list_checklist, 6, 0);
  lv_obj_set_flex_flow(_list_checklist, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(_list_checklist, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hint = lv_label_create(_scr_list);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_text_color(hint, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(hint, cjkFont(), 0);
  lv_label_set_text(hint, "点击项目可切换完成 · 语音可添加");

  lv_obj_t* btn_back = lv_button_create(_scr_list);
  lv_obj_set_size(btn_back, 70, 28);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 8, 6);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x475569), 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ ClassPetUI::getInstance().loadScreen(ClassPetUI::getInstance()._scr_menu); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_b = lv_label_create(btn_back); lv_label_set_text(lbl_b, "返回"); setCjkFont(lbl_b, cjkFont(), 0); lv_obj_center(lbl_b);

  lv_obj_t* btn_ref = lv_button_create(_scr_list);
  lv_obj_set_size(btn_ref, 70, 28);
  lv_obj_align(btn_ref, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_set_style_bg_color(btn_ref, LV_COLOR_PRIMARY, 0);
  lv_obj_add_event_cb(btn_ref, [](lv_event_t* e){ ClassPetUI::getInstance().markFeatureSync(2); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_r = lv_label_create(btn_ref); lv_label_set_text(lbl_r, "刷新"); setCjkFont(lbl_r, cjkFont(), 0); lv_obj_center(lbl_r);
}

void ClassPetUI::showList() {
  loadScreen(_scr_list);
  markFeatureSync(2);
}

void ClassPetUI::checklistRowCb(lv_event_t* e) {
  char* idc = (char*)lv_event_get_user_data(e);
  if (!idc) return;
  ClassPetUI& ui = ClassPetUI::getInstance();
  int idx = -1;
  for (size_t i = 0; i < ui._check_ids.size(); i++) {
    if (ui._check_ids[i] == String(idc)) { idx = (int)i; break; }
  }
  if (idx < 0) return;
  bool nd = !ui._check_done[idx];
  ui._check_done[idx] = nd;
  ApiClient::putChecklistItem(String(idc), nd);
  ui.markFeatureSync(2);
}

// ==========================================
// 闹铃屏
// ==========================================
void ClassPetUI::initAlarmScreen() {
  lv_obj_t* title = lv_label_create(_scr_alarm);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_text_color(title, LV_COLOR_TEXT, 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "我的闹铃");

  _list_alarm = lv_obj_create(_scr_alarm);
  lv_obj_set_size(_list_alarm, 300, 200);
  lv_obj_align(_list_alarm, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_bg_opa(_list_alarm, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(_list_alarm, 0, 0);
  lv_obj_set_style_pad_all(_list_alarm, 4, 0);
  lv_obj_set_style_pad_row(_list_alarm, 6, 0);
  lv_obj_set_flex_flow(_list_alarm, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(_list_alarm, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hint = lv_label_create(_scr_alarm);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_text_color(hint, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(hint, cjkFont(), 0);
  lv_label_set_text(hint, "可语音说：明天七点叫我起床");

  lv_obj_t* btn_back = lv_button_create(_scr_alarm);
  lv_obj_set_size(btn_back, 70, 28);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 8, 6);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x475569), 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ ClassPetUI::getInstance().loadScreen(ClassPetUI::getInstance()._scr_menu); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_b = lv_label_create(btn_back); lv_label_set_text(lbl_b, "返回"); setCjkFont(lbl_b, cjkFont(), 0); lv_obj_center(lbl_b);

  lv_obj_t* btn_ref = lv_button_create(_scr_alarm);
  lv_obj_set_size(btn_ref, 70, 28);
  lv_obj_align(btn_ref, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_set_style_bg_color(btn_ref, LV_COLOR_PRIMARY, 0);
  lv_obj_add_event_cb(btn_ref, [](lv_event_t* e){ ClassPetUI::getInstance().markFeatureSync(3); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_r = lv_label_create(btn_ref); lv_label_set_text(lbl_r, "刷新"); setCjkFont(lbl_r, cjkFont(), 0); lv_obj_center(lbl_r);
}

void ClassPetUI::showAlarm() {
  loadScreen(_scr_alarm);
  markFeatureSync(3);
}

// ==========================================
// 宠物主人记忆屏
// ==========================================
void ClassPetUI::initOwnerScreen() {
  lv_obj_t* title = lv_label_create(_scr_owner);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_text_color(title, LV_COLOR_TEXT, 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "宠物记忆");

  _list_owner = lv_obj_create(_scr_owner);
  lv_obj_set_size(_list_owner, 300, 200);
  lv_obj_align(_list_owner, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_bg_opa(_list_owner, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(_list_owner, 0, 0);
  lv_obj_set_style_pad_all(_list_owner, 4, 0);
  lv_obj_set_style_pad_row(_list_owner, 6, 0);
  lv_obj_set_flex_flow(_list_owner, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(_list_owner, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hint = lv_label_create(_scr_owner);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_text_color(hint, LV_COLOR_TEXT_MUTED, 0);
  setCjkFont(hint, cjkFont(), 0);
  lv_label_set_text(hint, "宠物会记住你 · 老师可在后台维护");

  lv_obj_t* btn_back = lv_button_create(_scr_owner);
  lv_obj_set_size(btn_back, 70, 28);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 8, 6);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x475569), 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ ClassPetUI::getInstance().loadScreen(ClassPetUI::getInstance()._scr_menu); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_b = lv_label_create(btn_back); lv_label_set_text(lbl_b, "返回"); setCjkFont(lbl_b, cjkFont(), 0); lv_obj_center(lbl_b);

  lv_obj_t* btn_ref = lv_button_create(_scr_owner);
  lv_obj_set_size(btn_ref, 70, 28);
  lv_obj_align(btn_ref, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_set_style_bg_color(btn_ref, LV_COLOR_PRIMARY, 0);
  lv_obj_add_event_cb(btn_ref, [](lv_event_t* e){ ClassPetUI::getInstance().markFeatureSync(4); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_r = lv_label_create(btn_ref); lv_label_set_text(lbl_r, "刷新"); setCjkFont(lbl_r, cjkFont(), 0); lv_obj_center(lbl_r);
}

void ClassPetUI::showOwnerMemory() {
  loadScreen(_scr_owner);
  markFeatureSync(4);
}

void ClassPetUI::showStandbyClock() {
  loadScreen(_scr_standby);
}

void ClassPetUI::markFeatureSync(uint8_t type) {
  g_featureReq = type;
}

// ==========================================
// 后台取回数据后渲染对应列表
// ==========================================
void ClassPetUI::renderFeatureData(uint8_t type, const String& json) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    DEBUG_PRINTF("⚠️ [UI] 功能数据 JSON 解析失败: %s\n", err.c_str());
    return;
  }

  if (type == 1 && _list_calendar) {
    clearList(_list_calendar);
    JsonArray events = doc["events"].as<JsonArray>();
    if (!events || events.size() == 0) {
      addListRow(_list_calendar, "(暂无日程)", "在网页后台或语音添加", true);
    } else {
      for (JsonObject e : events) {
        String date = e["event_date"].as<String>();
        String time = e["time_str"].as<String>();
        String title = e["title"].as<String>();
        String desc = e["description"].as<String>();
        String head = date + (time.length() ? (" " + time) : "");
        addListRow(_list_calendar, head, title + (desc.length() ? ("\n" + desc) : ""));
      }
    }
  } else if (type == 2 && _list_checklist) {
    clearList(_list_checklist);
    _check_ids.clear();
    _check_done.clear();
    JsonArray items = doc["items"].as<JsonArray>();
    if (!items || items.size() == 0) {
      addListRow(_list_checklist, "(暂无清单)", "在网页后台添加", true);
    } else {
      for (JsonObject it : items) {
        String id = it["id"].as<String>();
        String content = it["content"].as<String>();
        bool done = it["is_done"].as<int>() ? true : false;
        _check_ids.push_back(id);
        _check_done.push_back(done);
        String prefix = done ? "[✓] " : "[ ] ";
        lv_obj_t* row = addListRow(_list_checklist, prefix + content, "", done);
        if (row) {
          char* idc = strdup(id.c_str());
          lv_obj_set_user_data(row, idc);
          lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_add_event_cb(row, checklistRowCb, LV_EVENT_CLICKED, NULL);
        }
      }
    }
  } else if (type == 3 && _list_alarm) {
    clearList(_list_alarm);
    JsonArray scheds = doc["schedules"].as<JsonArray>();
    if (!scheds || scheds.size() == 0) {
      addListRow(_list_alarm, "(暂无闹铃)", "在网页后台或语音添加", true);
    } else {
      const char* wday[] = {"日","一","二","三","四","五","六"};
      for (JsonObject s : scheds) {
        int d = s["day_of_week"].as<int>();
        String t = s["time_str"].as<String>();
        String desc = s["task_desc"].as<String>();
        String head = "周" + String(wday[(d>=0&&d<=6)?d:0]) + " " + t;
        addListRow(_list_alarm, head, desc);
      }
    }
  } else if (type == 4 && _list_owner) {
    clearList(_list_owner);
    JsonObject profile = doc["profile"].as<JsonObject>();
    if (profile) {
      if (profile["grade"].as<String>().length())
        addListRow(_list_owner, "年级: " + profile["grade"].as<String>(), "");
      JsonArray subs = profile["subjects"].as<JsonArray>();
      if (subs && subs.size()) {
        String s;
        for (JsonVariant v : subs) { s += (s.length()?", ":"") + v.as<String>(); }
        addListRow(_list_owner, "科目: " + s, "");
      }
      if (profile["goals"].as<String>().length())
        addListRow(_list_owner, "目标: " + profile["goals"].as<String>(), "");
      if (profile["notes"].as<String>().length())
        addListRow(_list_owner, "备注: " + profile["notes"].as<String>(), "");
    } else {
      addListRow(_list_owner, "(暂无画像)", "老师可在后台完善学生画像", true);
    }
    JsonArray emo = doc["emotion_recent"].as<JsonArray>();
    if (emo && emo.size()) {
      for (JsonObject o : emo) {
        String txt = "情绪: " + o["emotion"].as<String>();
        if (o["note"].as<String>().length()) txt += " (" + o["note"].as<String>() + ")";
        addListRow(_list_owner, txt, "", true);
      }
    }
    JsonArray learn = doc["learning_recent"].as<JsonArray>();
    if (learn && learn.size()) {
      for (JsonObject o : learn) {
        String txt = "学习: " + o["subject"].as<String>() + " " + o["mastery"].as<String>();
        if (o["note"].as<String>().length()) txt += " (" + o["note"].as<String>() + ")";
        addListRow(_list_owner, txt, "", true);
      }
    }
  }
}
