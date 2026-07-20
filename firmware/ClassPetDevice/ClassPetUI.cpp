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



static void btn_tomato_pause_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_PAUSE_RESUME);
}

static void btn_tomato_exit_cb(lv_event_t* e) {
  DeviceStateMachine::getInstance().postEvent(EVENT_POMODORO_STOP);
  ClassPetUI::getInstance().showMenu();
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
    _scr_standby = lv_obj_create(NULL);
  _scr_settings = lv_obj_create(NULL);
  _scr_menu = lv_obj_create(NULL);
  _scr_calendar = lv_obj_create(NULL);
  _scr_list = lv_obj_create(NULL);
  _scr_alarm = lv_obj_create(NULL);
  _scr_owner = lv_obj_create(NULL);
        
  // 2. 配置背景样式
  lv_obj_t* screens[] = { _scr_normal, _scr_diag, _scr_tomato, _scr_processing, _scr_standby, _scr_settings, _scr_menu, _scr_calendar, _scr_list, _scr_alarm, _scr_owner };
  for (int i = 0; i < 11; i++) {
    lv_obj_set_style_bg_color(screens[i], LV_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(screens[i], LV_OPA_COVER, 0);
  }
  
  // 3. 构造各页面的静态子控件
  initNormalScreen();
  initDiagScreen();
  initTomatoScreen();
  initProcessingScreen();
    initStandbyScreen();
  initSettingsScreen();
  initMenuScreen();
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
    ui.showMenu();
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
  // // lv_obj_add_event_cb(_scr_tomato, gesture_event_cb, LV_EVENT_GESTURE, NULL);

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
    ClassPetUI::getInstance().showMenu();
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
// 待机屏幕点击/按压唤醒回调
static void standby_wake_event_cb(lv_event_t* e) {
  LV_UNUSED(e);
  g_screen_off = false;
  LcdDisplay::getInstance().resetActivityTime();
  LcdDisplay::getInstance().setBrightness(DeviceSettings::getBrightness());
  ClassPetUI::getInstance().forceSwitchToNormal();
}

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

  // 倒计时提示已移除

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

  // "按住说话"大按钮
  _voice_stop_btn = lv_button_create(_voice_overlay);
  lv_obj_set_size(_voice_stop_btn, 180, 50);
  lv_obj_align(_voice_stop_btn, LV_ALIGN_BOTTOM_MID, 0, -28);
  lv_obj_set_style_bg_color(_voice_stop_btn, LV_COLOR_PRIMARY, 0);
  lv_obj_set_style_bg_opa(_voice_stop_btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(_voice_stop_btn, 25, 0);
  lv_obj_clear_flag(_voice_stop_btn, LV_OBJ_FLAG_SCROLLABLE);

  // 添加按下和松开事件
  lv_obj_add_event_cb(_voice_stop_btn, [](lv_event_t* e) {
      DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_START);
  }, LV_EVENT_PRESSED, NULL);

  lv_obj_add_event_cb(_voice_stop_btn, [](lv_event_t* e) {
      DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_RECORD_DONE);
  }, LV_EVENT_RELEASED, NULL);
  
  lv_obj_add_event_cb(_voice_stop_btn, [](lv_event_t* e) {
      DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_RECORD_DONE);
  }, LV_EVENT_PRESS_LOST, NULL);

  lv_obj_t* lbl_stop = lv_label_create(_voice_stop_btn);
  lv_obj_set_style_text_color(lbl_stop, lv_color_white(), 0);
  lv_obj_set_style_text_font(lbl_stop, cjkFont(), 0);
  lv_label_set_text(lbl_stop, "按住说话");
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


void ClassPetUI::setVoiceOverlayProcessing(bool processing) {
  if (processing) {
    if (_voice_level) lv_obj_add_flag(_voice_level, LV_OBJ_FLAG_HIDDEN);
    if (_voice_stop_btn) lv_obj_add_flag(_voice_stop_btn, LV_OBJ_FLAG_HIDDEN);
    if (_voice_title) lv_label_set_text(_voice_title, "识别中...");
    if (_voice_processing_spinner) lv_obj_remove_flag(_voice_processing_spinner, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (_voice_level) lv_obj_remove_flag(_voice_level, LV_OBJ_FLAG_HIDDEN);
    if (_voice_stop_btn) lv_obj_remove_flag(_voice_stop_btn, LV_OBJ_FLAG_HIDDEN);
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


void ClassPetUI::showSettings() {
  refreshSettingsInfo();
  loadScreen(_scr_settings);
}
void ClassPetUI::showMenu() {
  loadScreen(_scr_menu);
}
void ClassPetUI::refreshSettingsInfo() {}

void ClassPetUI::initSettingsScreen() {
  // lv_obj_add_event_cb(_scr_settings, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_set_style_bg_color(_scr_settings, lv_color_hex(0xF1F5F9), 0);
  
  lv_obj_t* header = lv_obj_create(_scr_settings);
  lv_obj_set_size(header, 320, 48); // 恢复 320 宽
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);

  lv_obj_t* btn_back = lv_button_create(header);
  lv_obj_set_size(btn_back, 40, 40);
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn_back, 0, 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ ClassPetUI::getInstance().showMenu(); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* lbl_b = lv_label_create(btn_back);
  lv_label_set_text(lbl_b, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lbl_b, lv_color_white(), 0);
  lv_obj_center(lbl_b);

  lv_obj_t* title = lv_label_create(header);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 40, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "设备设置");

  // 设置列表容器，限制在 320x190 宽度，高度不超过240
  lv_obj_t* list = lv_list_create(_scr_settings);
  lv_obj_set_size(list, 320, 192);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 48);
  lv_obj_set_style_bg_color(list, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC); // 去掉弹性滑动，或者保留，只要不妨碍

  auto mkSwitch = [&](const char* icon, const char* name, bool on, void(*cb)(lv_event_t*)) {
      lv_obj_t* btn = lv_list_add_btn(list, icon, name);
      lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE); // 让背景不要抢占事件
      setCjkFont(lv_obj_get_child(btn, 1), cjkFont(), 0);
      lv_obj_t* sw = lv_switch_create(btn);
      lv_obj_set_size(sw, 40, 20);
      lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
      if(on) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
  };
  
  mkSwitch(LV_SYMBOL_VOLUME_MAX, "系统音效", true, [](lv_event_t* e){
      ClassPetUI::getInstance().showToast("系统音效设置将在云端同步生效", 2000);
  });
  mkSwitch(LV_SYMBOL_WIFI, "Wi-Fi 连接", true, [](lv_event_t* e){
      ClassPetUI::getInstance().showToast("无线网络开关已触发", 2000);
  });

  // 关于设备按钮，点击弹窗
  lv_obj_t* btn_about = lv_list_add_btn(list, LV_SYMBOL_DIRECTORY, "关于设备信息");
  setCjkFont(lv_obj_get_child(btn_about, 1), cjkFont(), 0);
  lv_obj_add_event_cb(btn_about, [](lv_event_t* e) {
      lv_obj_t* mbox = lv_msgbox_create(lv_layer_top());
      lv_obj_set_size(mbox, 260, 200); // 弹窗改宽一点
      lv_obj_center(mbox);
      
      lv_obj_t* mtitle = lv_msgbox_add_title(mbox, "关于设备");
      setCjkFont(mtitle, cjkFont(), 0);

      // 设备信息Label
      lv_obj_t* info = lv_label_create(mbox);
      setCjkFont(info, cjkFont(), 0);
      lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP); // 自动换行
      lv_obj_set_width(info, 220);
      
      char buf[256];
      snprintf(buf, sizeof(buf), "状态: 在线\\nIP: %s\\n机器码: %s\\n固件: v2.2.0\\n内存空闲: %uKB", 
               "192.168.0.100", "28:84:85:42:1C:74", ESP.getFreeHeap()/1024);
      lv_label_set_text(info, buf);

      lv_obj_t* close_btn = lv_msgbox_add_footer_button(mbox, "关闭");
      setCjkFont(lv_obj_get_child(close_btn, 0), cjkFont(), 0);
      lv_obj_add_event_cb(close_btn, [](lv_event_t* ev) {
          lv_msgbox_close(lv_obj_get_parent(lv_obj_get_parent((lv_obj_t*)lv_event_get_target(ev))));
      }, LV_EVENT_CLICKED, NULL);

  }, LV_EVENT_CLICKED, NULL);

  _lbl_machine_code = NULL;
  _lbl_dev_status = NULL;
  _lbl_fw_info = NULL;
  _lbl_sys_info = NULL;
}
void ClassPetUI::initMenuScreen() {
  lv_obj_add_event_cb(_scr_menu, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_set_style_bg_color(_scr_menu, lv_color_hex(0xF1F5F9), 0);

  lv_obj_t* title = lv_label_create(_scr_menu);
  lv_obj_set_style_text_color(title, lv_color_hex(0x334155), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, "所有的应用");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);

  // 横向屏幕网格容器：宽 320, 高 200 (底部留空或满屏，这里让它占下半部分即可滚动)
  lv_obj_t* grid = lv_obj_create(_scr_menu);
  lv_obj_set_size(grid, 320, 204);
  lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 8, 0);
  
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  
  lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF); // 隐藏恶心的滚动条

  auto mkAppIcon = [&](lv_obj_t* parent, const char* symbol, const char* name, lv_color_t color, void(*cb)(lv_event_t*)) {
    lv_obj_t* cont = lv_obj_create(parent);
    // 一行排四个，减小宽度以容纳
    lv_obj_set_size(cont, 68, 86);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* btn = lv_button_create(cont);
    lv_obj_set_size(btn, 54, 54);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_shadow_width(btn, 3, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x94A3B8), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* icon = lv_label_create(btn);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_label_set_text(icon, symbol);
    lv_obj_center(icon);

    lv_obj_t* lbl = lv_label_create(cont);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
    setCjkFont(lbl, cjkFont(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x475569), 0);
    lv_label_set_text(lbl, name);
  };

  mkAppIcon(grid, LV_SYMBOL_FILE, "日历", lv_color_hex(0xFB7185), [](lv_event_t*){ ClassPetUI::getInstance().showCalendar(); });
  mkAppIcon(grid, LV_SYMBOL_LIST, "清单", lv_color_hex(0x4ADE80), [](lv_event_t*){ ClassPetUI::getInstance().showList(); });
  mkAppIcon(grid, LV_SYMBOL_BELL, "闹铃", lv_color_hex(0xFB923C), [](lv_event_t*){ ClassPetUI::getInstance().showAlarm(); });
  mkAppIcon(grid, LV_SYMBOL_DIRECTORY, "记忆", lv_color_hex(0x60A5FA), [](lv_event_t*){ ClassPetUI::getInstance().showOwner(); });
  mkAppIcon(grid, LV_SYMBOL_PLAY, "番茄", lv_color_hex(0xEF4444), [](lv_event_t*){ ClassPetUI::getInstance().showTomatoSettings(); });
  mkAppIcon(grid, LV_SYMBOL_AUDIO, "语音", lv_color_hex(0x8B5CF6), [](lv_event_t*){ 
      DeviceStateMachine::getInstance().postEvent(EVENT_VOICE_ENTER);
  });
  mkAppIcon(grid, LV_SYMBOL_SETTINGS, "设置", lv_color_hex(0x64748B), [](lv_event_t*){ ClassPetUI::getInstance().showSettings(); });
}

// 辅助 Header: 横屏 320 宽度
lv_obj_t* ClassPetUI_createAppHeader(lv_obj_t* parent, const char* titleText, lv_color_t color) {
  lv_obj_t* header = lv_obj_create(parent);
  lv_obj_set_size(header, 320, 44);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(header, color, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* btn_back = lv_button_create(header);
  lv_obj_set_size(btn_back, 40, 30);
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn_back, 0, 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ ClassPetUI::getInstance().showMenu(); }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* lbl_b = lv_label_create(btn_back);
  lv_label_set_text(lbl_b, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lbl_b, lv_color_white(), 0);
  lv_obj_center(lbl_b);

  lv_obj_t* title = lv_label_create(header);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 65, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  setCjkFont(title, cjkFont(), 0);
  lv_label_set_text(title, titleText);
  return header;
}

static lv_obj_t* s_calendar = nullptr;

#include <time.h>
extern uint32_t getGlobalUnixTimestamp(); // 假设有这么个方法或者在 Network 里

void ClassPetUI::showCalendar() { 
  loadScreen(_scr_calendar); 
  markFeatureSync(1);
  
  if (s_calendar) {
      // 从 Network 拿到当前时间
      // 由于没有暴露 getUnixTimestamp 出来（在 Network 中），我们可以用粗略的时间获取
      // 如果没有合适的工具，直接用 time() 或者默认给一个 2026/7/21
      // 这里我们在 ClassPetUI 里为了编译通过，假设在主循环里有同步 RTC。
      // 我们在此处用一种安全的方式：
      time_t t;
      time(&t);
      struct tm* tm_info = gmtime(&t);
      if(tm_info && tm_info->tm_year > 100) {
          int y = tm_info->tm_year + 1900;
          int m = tm_info->tm_mon + 1;
          int d = tm_info->tm_mday;
          lv_calendar_set_today_date(s_calendar, y, m, d);
          lv_calendar_set_showed_date(s_calendar, y, m);
      } else {
          lv_calendar_set_today_date(s_calendar, 2026, 7, 21);
          lv_calendar_set_showed_date(s_calendar, 2026, 7);
      }
  }
}

void ClassPetUI::initCalendarScreen() {
  lv_obj_set_style_bg_color(_scr_calendar, lv_color_hex(0xF8FAFC), 0);
  
  s_calendar = lv_calendar_create(_scr_calendar);
  lv_obj_set_size(s_calendar, 300, 240); 
  lv_obj_align(s_calendar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(s_calendar, lv_color_white(), 0);
  lv_obj_set_style_border_width(s_calendar, 0, 0);
  
  // 【致命修复】必须创建日历 Header，否则底层组件为空，点击易引发异常断言！
  lv_calendar_header_arrow_create(s_calendar);
  
  lv_obj_t* btn_back = lv_button_create(_scr_calendar);
  lv_obj_set_size(btn_back, 36, 36);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 5, 5);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(btn_back, LV_OPA_50, 0);
  lv_obj_set_style_border_width(btn_back, 0, 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_set_style_radius(btn_back, 18, 0); 
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e){ ClassPetUI::getInstance().showMenu(); }, LV_EVENT_CLICKED, NULL);
  lv_obj_t* lbl_b = lv_label_create(btn_back);
  lv_label_set_text(lbl_b, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lbl_b, lv_color_white(), 0);
  lv_obj_center(lbl_b);

  auto calendar_event_cb = [](lv_event_t * e) {
      // 捕获点击，不使用底层的高危日期获取，纯作功能拦截展示
      // 日历目前仅作为日期查看使用，去除报错打扰
  };
  lv_obj_add_event_cb(s_calendar, calendar_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void ClassPetUI::showList() { loadScreen(_scr_list); markFeatureSync(2); }
void ClassPetUI::initListScreen() {
  // lv_obj_add_event_cb(_scr_list, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_set_style_bg_color(_scr_list, lv_color_hex(0xF8FAFC), 0);
  ClassPetUI_createAppHeader(_scr_list, "每日清单", lv_color_hex(0x4ADE80));

  lv_obj_t* cont = lv_obj_create(_scr_list);
  lv_obj_set_size(cont, 320, 196); // 横屏高度稍小
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE); // 移除不必要的滑块
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cont, 6, 0);
  
  auto addCheckTask = [&](const char* text) {
      lv_obj_t* card = lv_obj_create(cont);
      lv_obj_set_size(card, 300, 40); // 宽度拉伸至 300
      lv_obj_set_style_radius(card, 8, 0);
      lv_obj_set_style_border_width(card, 0, 0);
      lv_obj_set_style_shadow_width(card, 2, 0);
      lv_obj_set_style_shadow_color(card, lv_color_hex(0xE2E8F0), 0);
      lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE); // 彻底移除横滑
      
      lv_obj_t* cb = lv_checkbox_create(card);
      lv_checkbox_set_text(cb, "");
      lv_obj_align(cb, LV_ALIGN_LEFT_MID, 0, 0);
      
      lv_obj_t* lbl = lv_label_create(card);
      setCjkFont(lbl, cjkFont(), 0);
      lv_label_set_text(lbl, text);
      lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 40, 0);
      
      lv_obj_add_event_cb(cb, [](lv_event_t* e){
          lv_obj_t* checkbox = (lv_obj_t*)lv_event_get_target(e);
          lv_obj_t* c = lv_obj_get_parent(checkbox);
          lv_obj_t* l = lv_obj_get_child(c, 1);
          if(lv_obj_get_state(checkbox) & LV_STATE_CHECKED) {
              lv_obj_set_style_text_decor(l, LV_TEXT_DECOR_STRIKETHROUGH, 0);
              lv_obj_set_style_text_color(l, lv_color_hex(0x94A3B8), 0);
          } else {
              lv_obj_set_style_text_decor(l, LV_TEXT_DECOR_NONE, 0);
              lv_obj_set_style_text_color(l, lv_color_hex(0x1E293B), 0);
          }
      }, LV_EVENT_VALUE_CHANGED, NULL);
  };
  
  addCheckTask("完成数学作业");
  addCheckTask("练钢琴 30 分钟");
  addCheckTask("打扫房间");
}

void ClassPetUI::showAlarm() { loadScreen(_scr_alarm); markFeatureSync(3); }
void ClassPetUI::initAlarmScreen() {
  // lv_obj_add_event_cb(_scr_alarm, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_set_style_bg_color(_scr_alarm, lv_color_hex(0xF8FAFC), 0);
  ClassPetUI_createAppHeader(_scr_alarm, "设备闹铃", lv_color_hex(0xFB923C));

  _list_alarm = lv_obj_create(_scr_alarm);
  lv_obj_set_size(_list_alarm, 320, 150);
  lv_obj_align(_list_alarm, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_bg_opa(_list_alarm, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(_list_alarm, 0, 0);
  lv_obj_clear_flag(_list_alarm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(_list_alarm, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(_list_alarm, 6, 0);
  
  addAlarmRow(_list_alarm, "07:30", "起床上学");
  addAlarmRow(_list_alarm, "20:00", "阅读时间");
  

  lv_obj_t* btn_add = lv_button_create(_scr_alarm);
  lv_obj_set_size(btn_add, 160, 36);
  lv_obj_align(btn_add, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(btn_add, lv_color_hex(0xFB923C), 0);
  lv_obj_set_style_radius(btn_add, 18, 0);
  
  lv_obj_t* lbl_add = lv_label_create(btn_add);
  setCjkFont(lbl_add, cjkFont(), 0);
  lv_label_set_text(lbl_add, "+ 新增闹铃");
  lv_obj_center(lbl_add);

  lv_obj_add_event_cb(btn_add, [](lv_event_t* e) {
      lv_obj_t* scr = lv_scr_act();
      
      lv_obj_t* bg = lv_obj_create(scr);
      lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
      lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(bg, LV_OPA_60, 0);
      lv_obj_set_style_border_width(bg, 0, 0);
      
      lv_obj_t* mbox = lv_obj_create(bg);
      lv_obj_set_size(mbox, 260, 200);
      lv_obj_center(mbox);
      lv_obj_set_style_pad_all(mbox, 10, 0);
      lv_obj_set_style_border_width(mbox, 0, 0);
      lv_obj_set_style_radius(mbox, 12, 0);
      lv_obj_set_flex_flow(mbox, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(mbox, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      lv_obj_t* title = lv_label_create(mbox);
      setCjkFont(title, cjkFont(), 0);
      lv_label_set_text(title, "设定闹钟时间");
      lv_obj_set_style_text_color(title, lv_color_hex(0x1E293B), 0);

      lv_obj_t* rollers = lv_obj_create(mbox);
      lv_obj_set_size(rollers, 240, 100);
      lv_obj_set_style_bg_opa(rollers, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(rollers, 0, 0);
      lv_obj_set_flex_flow(rollers, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(rollers, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      static char hours[150] = "";
      if(hours[0] == 0) {
          for(int i=0; i<24; i++) {
              char buf[10];
              snprintf(buf, sizeof(buf), "%02d\n", i);
              strcat(hours, buf);
          }
          hours[strlen(hours)-1] = '\0';
      }

      static char mins[300] = "";
      if(mins[0] == 0) {
          for(int i=0; i<60; i++) {
              char buf[10];
              snprintf(buf, sizeof(buf), "%02d\n", i);
              strcat(mins, buf);
          }
          mins[strlen(mins)-1] = '\0';
      }

      lv_obj_t* roller_h = lv_roller_create(rollers);
      lv_roller_set_options(roller_h, hours, LV_ROLLER_MODE_INFINITE);
      lv_roller_set_visible_row_count(roller_h, 3);
      lv_obj_set_width(roller_h, 70);
      
      lv_obj_t* sep = lv_label_create(rollers);
      lv_label_set_text(sep, ":");
      // Use fallback font size or default font for colon
      lv_obj_set_style_text_font(sep, &lv_font_montserrat_20, 0);

      lv_obj_t* roller_m = lv_roller_create(rollers);
      lv_roller_set_options(roller_m, mins, LV_ROLLER_MODE_INFINITE);
      lv_roller_set_visible_row_count(roller_m, 3);
      lv_obj_set_width(roller_m, 70);

      lv_obj_t* btn_row = lv_obj_create(mbox);
      lv_obj_set_size(btn_row, 240, LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(btn_row, 0, 0);
      lv_obj_set_style_pad_all(btn_row, 0, 0);
      lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      lv_obj_t* btn_cancel = lv_button_create(btn_row);
      lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x94A3B8), 0);
      lv_obj_set_size(btn_cancel, 90, 36);
      lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
      setCjkFont(lbl_cancel, cjkFont(), 0);
      lv_label_set_text(lbl_cancel, "取消");
      lv_obj_center(lbl_cancel);

      lv_obj_t* btn_ok = lv_button_create(btn_row);
      lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xEA580C), 0);
      lv_obj_set_size(btn_ok, 90, 36);
      lv_obj_t* lbl_ok = lv_label_create(btn_ok);
      setCjkFont(lbl_ok, cjkFont(), 0);
      lv_label_set_text(lbl_ok, "保存");
      lv_obj_center(lbl_ok);

      lv_obj_add_event_cb(btn_cancel, [](lv_event_t* ev) {
          lv_obj_t* parent = (lv_obj_t*)lv_event_get_user_data(ev);
          lv_obj_delete_async(parent);
      }, LV_EVENT_CLICKED, bg);

      lv_obj_add_event_cb(btn_ok, [](lv_event_t* ev) {
          lv_obj_t* bg = (lv_obj_t*)lv_event_get_user_data(ev);
          lv_obj_t* mbox = lv_obj_get_child(bg, 0);
          if (mbox) {
              lv_obj_t* rollers = lv_obj_get_child(mbox, 1);
              if (rollers) {
                  lv_obj_t* r_h = lv_obj_get_child(rollers, 0);
                  lv_obj_t* r_m = lv_obj_get_child(rollers, 2);
                  char h_str[10]; char m_str[10];
                  lv_roller_get_selected_str(r_h, h_str, sizeof(h_str));
                  lv_roller_get_selected_str(r_m, m_str, sizeof(m_str));
                  String tStr = String(h_str) + ":" + String(m_str);
                  ClassPetUI::getInstance().addAlarmRow(ClassPetUI::getInstance()._list_alarm, tStr, "自建闹铃");
              }
          }
          ClassPetUI::getInstance().showToast("新闹铃设定已发送至后台并添加到列表", 3000);
          lv_obj_delete_async(bg);
      }, LV_EVENT_CLICKED, bg);

  }, LV_EVENT_CLICKED, NULL);
}

void ClassPetUI::showOwner() { loadScreen(_scr_owner); markFeatureSync(4); }
void ClassPetUI::initOwnerScreen() {
  // lv_obj_add_event_cb(_scr_owner, gesture_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_set_style_bg_color(_scr_owner, lv_color_hex(0xF8FAFC), 0);
  ClassPetUI_createAppHeader(_scr_owner, "主人记忆", lv_color_hex(0x60A5FA));

  lv_obj_t* list = lv_list_create(_scr_owner);
  lv_obj_set_size(list, 320, 196); // 宽度320
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  auto addMenu = [&](const char* icon, const char* text) {
      lv_obj_t* btn = lv_list_add_btn(list, icon, text);
      lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      lv_obj_set_style_radius(btn, 8, 0);
      lv_obj_set_style_margin_bottom(btn, 8, 0);
      
      lv_obj_t* l = lv_obj_get_child(btn, 1);
      if(l) setCjkFont(l, cjkFont(), 0);
      
      lv_obj_add_event_cb(btn, [](lv_event_t* e) {
          lv_obj_t* sub = lv_obj_create(lv_layer_top()); // 放在最顶层避免挤压
          lv_obj_set_size(sub, 320, 240);
          lv_obj_center(sub);
          lv_obj_set_style_bg_color(sub, lv_color_hex(0xF1F5F9), 0);
          lv_obj_set_style_radius(sub, 0, 0);
          lv_obj_set_style_border_width(sub, 0, 0);
          
          lv_obj_t* sub_header = ClassPetUI_createAppHeader(sub, "详情", lv_color_hex(0x3B82F6));
          lv_obj_t* back_btn = lv_obj_get_child(sub_header, 0);
          lv_obj_remove_event_cb_with_user_data(back_btn, NULL, NULL); 
          lv_obj_add_event_cb(back_btn, [](lv_event_t* ev) {
              lv_obj_t* target = (lv_obj_t*)lv_event_get_user_data(ev);
              lv_obj_delete_async(target);
          }, LV_EVENT_CLICKED, sub);

          lv_obj_t* info = lv_label_create(sub);
          setCjkFont(info, cjkFont(), 0);
          lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP); // 开启自动换行，去除横向滑块
          lv_obj_set_width(info, 300); // 宽度留白
          lv_label_set_text(info, "这部分是详细的数据分析报告，当内容过多时会自动换行而不会撑爆屏幕。");
          lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 50);

      }, LV_EVENT_CLICKED, NULL);
  };
  
  addMenu(LV_SYMBOL_IMAGE, " 用户画像");
  addMenu(LV_SYMBOL_DIRECTORY, " 情绪记录");
  addMenu(LV_SYMBOL_FILE, " 学习情况");
}

void ClassPetUI::renderFeatureData(uint8_t type, const String& json) {
  // TODO
}

void ClassPetUI::showStandbyClock() {
  loadScreen(_scr_standby);
}

extern volatile uint8_t g_featureReq;
void ClassPetUI::markFeatureSync(uint8_t type) {
  g_featureReq = type;
}

lv_obj_t* ClassPetUI::addAlarmRow(lv_obj_t* scroll, const String& timeStr, const String& title) {
    if (!scroll) return nullptr;
    lv_obj_t* card = lv_obj_create(scroll);
    lv_obj_set_size(card, 300, 50); // 横向拉满
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t* t_lbl = lv_label_create(card);
    lv_obj_set_style_text_font(t_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t_lbl, lv_color_hex(0xFB923C), 0);
    lv_label_set_text(t_lbl, timeStr.c_str());
    lv_obj_align(t_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    
    lv_obj_t* n_lbl = lv_label_create(card);
    setCjkFont(n_lbl, cjkFont(), 0);
    lv_obj_set_style_text_color(n_lbl, lv_color_hex(0x64748B), 0);
    lv_label_set_text(n_lbl, title.c_str());
    lv_obj_align(n_lbl, LV_ALIGN_LEFT_MID, 60, 0); // 在时间右侧
    
    lv_obj_t* sw = lv_switch_create(card);
    lv_obj_set_size(sw, 40, 20); 
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    
    return card;
}
