#include "custom_emoji_display.h"
#include "lvgl_theme.h"

#include <esp_log.h>
#include <font_awesome.h>
#include <lvgl.h>

#include <algorithm>
#include <cstring>
#include <string>

#define TAG "CustomEmojiDisplay"

// Emotion mapping table - map 21 emotions to 6 colorful GIFs
const CustomEmojiDisplay::EmotionMap CustomEmojiDisplay::emotion_maps_[] = {
    // Neutral/calm emotions -> staticstate
    {"neutral", &staticstate},
    {"relaxed", &staticstate},
    {"sleepy", &staticstate},

    // Positive/happy emotions -> happy
    {"happy", &happy},
    {"laughing", &happy},
    {"funny", &happy},
    {"loving", &happy},
    {"confident", &happy},
    {"winking", &happy},
    {"cool", &happy},
    {"delicious", &happy},
    {"kissy", &happy},
    {"silly", &happy},

    // Sad emotions -> sad
    {"sad", &sad},
    {"crying", &sad},

    // Angry emotions -> anger
    {"angry", &anger},

    // Surprised/shocked emotions -> scare
    {"surprised", &scare},
    {"shocked", &scare},
    {"embarrassed", &scare},

    // Thinking/confused emotions -> buxue
    {"thinking", &buxue},
    {"confused", &buxue},

    // End marker
    {nullptr, nullptr}
};

CustomEmojiDisplay::CustomEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                     int width, int height, int offset_x, int offset_y, 
                                     bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy),
      emotion_gif_(nullptr), text_buffer_(), is_accumulating_text_(false) {
    
    // Setup GIF container for colorful emotions
    SetupGifContainer();
    
    ESP_LOGI(TAG, "Custom colorful emoji display initialized");
}

void CustomEmojiDisplay::SetupGifContainer() {
    DisplayLockGuard lock(this);

    // Hide default emoji label (we'll use GIF instead)
    if (emoji_label_) {
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    // Create IMG emotion container (use IMG instead of GIF for better compatibility)
    emotion_gif_ = lv_img_create(content_);
    int gif_size = LV_HOR_RES / 2; // Làm to hơn: chia 2 thay vì chia 3
    lv_obj_set_size(emotion_gif_, gif_size, gif_size);
    lv_obj_set_style_border_width(emotion_gif_, 0, 0);
    lv_obj_set_style_bg_opa(emotion_gif_, LV_OPA_TRANSP, 0);
    lv_obj_center(emotion_gif_);
    
    // Set default emotion
    lv_img_set_src(emotion_gif_, &staticstate);

    ESP_LOGI(TAG, "IMG emotion container setup completed");
}

void CustomEmojiDisplay::SetEmotion(const char* emotion) {
    if (!emotion || !emotion_gif_) {
        return;
    }

    DisplayLockGuard lock(this);

    // Find matching emotion in mapping table
    for (const auto& map : emotion_maps_) {
        if (map.name && strcmp(map.name, emotion) == 0) {
            lv_img_set_src(emotion_gif_, map.gif);
            ESP_LOGI(TAG, "Set colorful emotion: %s", emotion);
            return;
        }
    }

    // Default to staticstate if emotion not found
    lv_img_set_src(emotion_gif_, &staticstate);
    ESP_LOGI(TAG, "Unknown emotion '%s', using default", emotion);
}

void CustomEmojiDisplay::SetChatMessage(const char* role, const char* content) {
    // Add debug logging to track text corruption
    ESP_LOGI(TAG, "SetChatMessage: role='%s', content='%s'", role ? role : "NULL", content ? content : "NULL");
    
    if (!content || !role) {
        return;
    }
    
    DisplayLockGuard lock(this);
    
    // Check if this is assistant streaming text (should be accumulated)
    if (strcmp(role, "assistant") == 0) {
        // Check if content looks like partial UTF-8 (ends with incomplete character)
        std::string new_content(content);
        
        // Accumulate text in buffer
        text_buffer_ += new_content;
        is_accumulating_text_ = true;
        
        // Update display with accumulated text immediately
        if (!text_buffer_.empty()) {
            // Get the last message container to see if we should append or create new
            uint32_t child_count = lv_obj_get_child_cnt(content_);
            if (child_count > 0) {
                lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);
                if (last_container != nullptr && lv_obj_get_child_cnt(last_container) > 0) {
                    lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);
                    if (last_bubble != nullptr) {
                        // Check if last message is also from assistant
                        void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);
                        if (bubble_type_ptr != nullptr && strcmp((const char*)bubble_type_ptr, "assistant") == 0) {
                            // Get the label inside the bubble and update with accumulated text
                            if (lv_obj_get_child_cnt(last_bubble) > 0) {
                                lv_obj_t* label = lv_obj_get_child(last_bubble, 0);
                                if (label != nullptr) {
                                    lv_label_set_text(label, text_buffer_.c_str());
                                    ESP_LOGI(TAG, "Updated accumulated text. Length: %d", text_buffer_.length());
                                    
                                    // Scroll to the last message
                                    lv_obj_scroll_to_view_recursive(last_container, LV_ANIM_OFF);
                                    return; // Don't create new message
                                }
                            }
                        }
                    }
                }
            }
            
            // If no existing assistant message, create new one with accumulated text
            SpiLcdDisplay::SetChatMessage(role, text_buffer_.c_str());
        }
        return;
    } else {
        // For user messages, clear buffer and reset accumulation
        text_buffer_.clear();
        is_accumulating_text_ = false;
        
        // Use parent implementation for user messages
        SpiLcdDisplay::SetChatMessage(role, content);
    }
}