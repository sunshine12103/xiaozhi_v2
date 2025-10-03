#pragma once

#include "display/lcd_display.h"
#include "otto_emoji_gif.h"
#include <string>

/**
 * @brief Custom ESP32S3 colorful emoji display class
 * Sử dụng GIF emojis thay vì static images để có màu sắc đa dạng
 */
class CustomEmojiDisplay : public SpiLcdDisplay {
public:
    /**
     * @brief Constructor với các tham số SpiLcdDisplay
     */
    CustomEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, 
                      int width, int height, int offset_x, int offset_y, 
                      bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~CustomEmojiDisplay() = default;

    // Override emotion setting để dùng colorful GIFs
    virtual void SetEmotion(const char* emotion) override;

    // Override chat message để tương thích
    virtual void SetChatMessage(const char* role, const char* content) override;

private:
    void SetupGifContainer();

    lv_obj_t* emotion_gif_;  ///< GIF emotion component
    
    // Text accumulation for proper UTF-8 handling
    std::string text_buffer_;
    bool is_accumulating_text_;

    // Emotion mapping structure
    struct EmotionMap {
        const char* name;
        const lv_img_dsc_t* gif;
    };

    // Emotion mapping table
    static const EmotionMap emotion_maps_[];
};