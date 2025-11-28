#include "otto_emoji_display.h"
#include "lvgl_theme.h"

#include <esp_log.h>
#include <font_awesome.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "display/lcd_display.h"

#define TAG "OttoEmojiDisplay"

// Include only 3 essential GIF data files to reduce CPU load
#include "neutral.h"
#include "happy.h"
#include "angry.h"

// Define only 3 essential GIF descriptors to minimize CPU usage
const lv_img_dsc_t staticstate = {
    .data_size = Neutral_1__gif_len,
    .data = Neutral_1__gif,
};

const lv_img_dsc_t happy = {
    .data_size = Happy_gif_len,
    .data = Happy_gif,
};

const lv_img_dsc_t angry = {
    .data_size = Angry_gif_len, 
    .data = Angry_gif,
};

// Map all other emotions to these 3 basic ones to save CPU
const lv_img_dsc_t sad = staticstate;       // Use neutral for sad
const lv_img_dsc_t surprised = happy;       // Use happy for surprised
const lv_img_dsc_t scared = staticstate;    // Use neutral for scared
const lv_img_dsc_t furious = angry;         // Very angry -> angry
const lv_img_dsc_t awe = happy;             // Awe -> happy
const lv_img_dsc_t focused = staticstate;   // Thinking -> neutral
const lv_img_dsc_t squint = staticstate;    // Squinting -> neutral

// 表情映射表 - Khoa học mapping emotions to appropriate GIFs
const OttoEmojiDisplay::EmotionMap OttoEmojiDisplay::emotion_maps_[] = {
    // Neutral/Calm emotions
    {"neutral", &staticstate},
    {"relaxed", &staticstate},
    {"sleepy", &staticstate},
    {"idle", &staticstate},
    
    // Happy/Positive emotions
    {"happy", &happy},
    {"laughing", &happy},
    {"funny", &happy},
    {"loving", &happy},
    {"confident", &happy},
    {"cool", &happy},
    {"delicious", &happy},
    {"kissy", &happy},
    {"silly", &happy},
    {"joyful", &happy},
    {"excited", &happy},
    
    // Winking/Skeptical emotions
    {"winking", &squint},
    {"skeptical", &squint},
    {"mischievous", &squint},
    
    // Sad/Down emotions  
    {"sad", &sad},
    {"crying", &sad},
    {"disappointed", &sad},
    {"melancholy", &sad},
    {"depressed", &sad},
    
    // Angry emotions (mild)
    {"angry", &angry},
    {"annoyed", &angry},
    {"frustrated", &angry},
    {"irritated", &angry},
    
    // Very angry emotions
    {"furious", &furious},
    {"rage", &furious},
    {"livid", &furious},
    {"enraged", &furious},
    
    // Surprised emotions
    {"surprised", &surprised},
    {"shocked", &surprised},
    {"astonished", &surprised},
    {"amazed", &awe},
    {"impressed", &awe},
    {"wonder", &awe},
    
    // Thinking/Focused emotions
    {"thinking", &focused},
    {"confused", &focused},
    {"concentrated", &focused},
    {"determined", &focused},
    {"focused", &focused},
    {"pondering", &focused},
    
    // Fear/Scared emotions
    {"scared", &scared},
    {"afraid", &scared},
    {"worried", &scared},
    {"anxious", &scared},
    {"nervous", &scared},
    {"embarrassed", &staticstate},

    {nullptr, nullptr}  // 结束标记
};

OttoEmojiDisplay::OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                   int width, int height, int offset_x, int offset_y, bool mirror_x,
                                   bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy),
      emotion_gif_(nullptr) {
    SetupGifContainer();
};

void OttoEmojiDisplay::SetupGifContainer() {
    DisplayLockGuard lock(this);

    if (emoji_label_) {
        lv_obj_del(emoji_label_);
    }

    if (chat_message_label_) {
        lv_obj_del(chat_message_label_);
    }
    if (content_) {
        lv_obj_del(content_);
    }

    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(content_, LV_HOR_RES, LV_HOR_RES);
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_center(content_);

    emoji_label_ = lv_label_create(content_);
    lv_label_set_text(emoji_label_, "");
    lv_obj_set_width(emoji_label_, 0);
    lv_obj_set_style_border_width(emoji_label_, 0, 0);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    emotion_gif_ = lv_gif_create(content_);
    int gif_size = LV_HOR_RES;
    lv_obj_set_size(emotion_gif_, gif_size, gif_size);
    lv_obj_set_style_border_width(emotion_gif_, 0, 0);
    lv_obj_set_style_bg_opa(emotion_gif_, LV_OPA_TRANSP, 0);
    lv_obj_center(emotion_gif_);
    lv_gif_set_src(emotion_gif_, &staticstate);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
    lv_obj_set_style_border_width(chat_message_label_, 0, 0);

    lv_obj_set_style_bg_opa(chat_message_label_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(chat_message_label_, lv_color_black(), 0);
    
    // Hide chat message text completely
    lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_ver(chat_message_label_, 5, 0);

    lv_obj_align(chat_message_label_, LV_ALIGN_BOTTOM_MID, 0, 0);

    auto& theme_manager = LvglThemeManager::GetInstance();
    auto theme = theme_manager.GetTheme("dark");
    if (theme != nullptr) {
        LcdDisplay::SetTheme(theme);
    }
}

void OttoEmojiDisplay::SetEmotion(const char* emotion) {
    if (!emotion || !emotion_gif_) {
        return;
    }

    DisplayLockGuard lock(this);

    for (const auto& map : emotion_maps_) {
        if (map.name && strcmp(map.name, emotion) == 0) {
            lv_gif_set_src(emotion_gif_, map.gif);
            ESP_LOGI(TAG, "设置表情: %s", emotion);
            return;
        }
    }

    lv_gif_set_src(emotion_gif_, &staticstate);
    ESP_LOGI(TAG, "未知表情'%s'，使用默认", emotion);
}

void OttoEmojiDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    // Always keep chat message text hidden - only show GIF emotions
    lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "聊天消息被隐藏 [%s]: %s", role, content);
}
