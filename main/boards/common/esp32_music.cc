#include "esp32_music.h"
#include "board.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>  // Σ╕║isdigitσç╜µò░
#include <thread>   // Σ╕║τ║┐τ¿ïIDµ»öΦ╛â
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Esp32Music"

// ========== τ«ÇσìòτÜäESP32Φ«ñΦ»üσç╜µò░ ==========

/**
 * @brief ΦÄ╖σÅûΦ«╛σñçMACσ£░σ¥Ç
 * @return MACσ£░σ¥Çσ¡ùτ¼ªΣ╕▓
 */
static std::string get_device_mac() {
    return SystemInfo::GetMacAddress();
}

/**
 * @brief ΦÄ╖σÅûΦ«╛σñçΦè»τëçID
 * @return Φè»τëçIDσ¡ùτ¼ªΣ╕▓
 */
static std::string get_device_chip_id() {
    // Σ╜┐τö¿MACσ£░σ¥ÇΣ╜£Σ╕║Φè»τëçID∩╝îσÄ╗ΘÖñσåÆσÅ╖σêåΘÜöτ¼ª
    std::string mac = SystemInfo::GetMacAddress();
    // σÄ╗ΘÖñµëÇµ£ëσåÆσÅ╖
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
    return mac;
}

/**
 * @brief τöƒµêÉσè¿µÇüσ»åΘÆÑ
 * @param timestamp µù╢Θù┤µê│
 * @return σè¿µÇüσ»åΘÆÑσ¡ùτ¼ªΣ╕▓
 */
static std::string generate_dynamic_key(int64_t timestamp) {
    // σ»åΘÆÑ∩╝êΦ»╖Σ┐«µö╣Σ╕║Σ╕Äµ£ìσèíτ½»Σ╕ÇΦç┤∩╝ë
    const std::string secret_key = "your-esp32-secret-key-2024";
    
    // ΦÄ╖σÅûΦ«╛σñçΣ┐íµü»
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();
    
    // τ╗äσÉêµò░µì«∩╝ÜMAC:Φè»τëçID:µù╢Θù┤µê│:σ»åΘÆÑ
    std::string data = mac + ":" + chip_id + ":" + std::to_string(timestamp) + ":" + secret_key;
    
    // SHA256σôêσ╕î
    unsigned char hash[32];
    mbedtls_sha256((unsigned char*)data.c_str(), data.length(), hash, 0);
    
    // Φ╜¼µìóΣ╕║σìüσà¡Φ┐¢σê╢σ¡ùτ¼ªΣ╕▓∩╝êσëì16σ¡ùΦèé∩╝ë
    std::string key;
    for (int i = 0; i < 16; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", hash[i]);
        key += hex;
    }
    
    return key;
}

/**
 * @brief Σ╕║HTTPΦ»╖µ▒éµ╖╗σèáΦ«ñΦ»üσñ┤
 * @param http HTTPσ«óµê╖τ½»µîçΘÆê
 */
static void add_auth_headers(Http* http) {
    // ΦÄ╖σÅûσ╜ôσëìµù╢Θù┤µê│
    int64_t timestamp = esp_timer_get_time() / 1000000;  // Φ╜¼µìóΣ╕║τºÆ
    
    // τöƒµêÉσè¿µÇüσ»åΘÆÑ
    std::string dynamic_key = generate_dynamic_key(timestamp);
    
    // ΦÄ╖σÅûΦ«╛σñçΣ┐íµü»
    std::string mac = get_device_mac();
    std::string chip_id = get_device_chip_id();
    
    // µ╖╗σèáΦ«ñΦ»üσñ┤
    if (http) {
        http->SetHeader("X-MAC-Address", mac);
        http->SetHeader("X-Chip-ID", chip_id);
        http->SetHeader("X-Timestamp", std::to_string(timestamp));
        http->SetHeader("X-Dynamic-Key", dynamic_key);
        
        ESP_LOGI(TAG, "Added auth headers - MAC: %s, ChipID: %s, Timestamp: %lld", 
                 mac.c_str(), chip_id.c_str(), timestamp);
    }
}

// URLτ╝ûτáüσç╜µò░
static std::string url_encode(const std::string& str) {
    std::string encoded;
    char hex[4];
    
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = str[i];
        
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';  // τ⌐║µá╝τ╝ûτáüΣ╕║'+'µêû'%20'
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

// σ£¿µûçΣ╗╢σ╝Çσñ┤µ╖╗σèáΣ╕ÇΣ╕¬Φ╛àσè⌐σç╜µò░∩╝îτ╗ƒΣ╕ÇσñäτÉåURLµ₧äσ╗║
static std::string buildUrlWithParams(const std::string& base_url, const std::string& path, const std::string& query) {
    std::string result_url = base_url + path + "?";
    size_t pos = 0;
    size_t amp_pos = 0;
    
    while ((amp_pos = query.find("&", pos)) != std::string::npos) {
        std::string param = query.substr(pos, amp_pos - pos);
        size_t eq_pos = param.find("=");
        
        if (eq_pos != std::string::npos) {
            std::string key = param.substr(0, eq_pos);
            std::string value = param.substr(eq_pos + 1);
            result_url += key + "=" + url_encode(value) + "&";
        } else {
            result_url += param + "&";
        }
        
        pos = amp_pos + 1;
    }
    
    // σñäτÉåµ£ÇσÉÄΣ╕ÇΣ╕¬σÅéµò░
    std::string last_param = query.substr(pos);
    size_t eq_pos = last_param.find("=");
    
    if (eq_pos != std::string::npos) {
        std::string key = last_param.substr(0, eq_pos);
        std::string value = last_param.substr(eq_pos + 1);
        result_url += key + "=" + url_encode(value);
    } else {
        result_url += last_param;
    }
    
    return result_url;
}

Esp32Music::Esp32Music() : last_downloaded_data_(), current_music_url_(), current_song_name_(),
                         song_name_displayed_(false), current_lyric_url_(), lyrics_(), 
                         current_lyric_index_(-1), lyric_thread_(), is_lyric_running_(false),
                         display_mode_(DISPLAY_MODE_LYRICS), is_playing_(false), is_downloading_(false), 
                         play_thread_(), download_thread_(), audio_buffer_(), buffer_mutex_(), 
                         buffer_cv_(), buffer_size_(0), mp3_decoder_(nullptr), mp3_frame_info_(), 
                         mp3_decoder_initialized_(false) {
    ESP_LOGI(TAG, "Music player initialized with default spectrum display mode");
    InitializeMp3Decoder();
}

Esp32Music::~Esp32Music() {
    ESP_LOGI(TAG, "Destroying music player - stopping all operations");
    
    // σü£µ¡óµëÇµ£ëµôìΣ╜£
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;
    
    // ΘÇÜτƒÑµëÇµ£ëτ¡ëσ╛àτÜäτ║┐τ¿ï
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // τ¡ëσ╛àΣ╕ïΦ╜╜τ║┐τ¿ïτ╗ôµ¥ƒ∩╝îΦ«╛τ╜«5τºÆΦ╢àµù╢
    if (download_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for download thread to finish (timeout: 5s)");
        auto start_time = std::chrono::steady_clock::now();
        
        // τ¡ëσ╛àτ║┐τ¿ïτ╗ôµ¥ƒ
        bool thread_finished = false;
        while (!thread_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed >= 5) {
                ESP_LOGW(TAG, "Download thread join timeout after 5 seconds");
                break;
            }
            
            // σåìµ¼íΦ«╛τ╜«σü£µ¡óµáçσ┐ù∩╝îτí«Σ┐¥τ║┐τ¿ïΦâ╜σñƒµúÇµ╡ïσê░
            is_downloading_ = false;
            
            // ΘÇÜτƒÑµ¥íΣ╗╢σÅÿΘçÅ
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }
            
            // µúÇµƒÑτ║┐τ¿ïµÿ»σÉªσ╖▓τ╗Åτ╗ôµ¥ƒ
            if (!download_thread_.joinable()) {
                thread_finished = true;
            }
            
            // σ«Üµ£ƒµëôσì░τ¡ëσ╛àΣ┐íµü»
            if (elapsed > 0 && elapsed % 1 == 0) {
                ESP_LOGI(TAG, "Still waiting for download thread to finish... (%ds)", (int)elapsed);
            }
        }
        
        if (download_thread_.joinable()) {
            download_thread_.join();
        }
        ESP_LOGI(TAG, "Download thread finished");
    }
    
    // τ¡ëσ╛àµÆ¡µö╛τ║┐τ¿ïτ╗ôµ¥ƒ∩╝îΦ«╛τ╜«3τºÆΦ╢àµù╢
    if (play_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for playback thread to finish (timeout: 3s)");
        auto start_time = std::chrono::steady_clock::now();
        
        bool thread_finished = false;
        while (!thread_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed >= 3) {
                ESP_LOGW(TAG, "Playback thread join timeout after 3 seconds");
                break;
            }
            
            // σåìµ¼íΦ«╛τ╜«σü£µ¡óµáçσ┐ù
            is_playing_ = false;
            
            // ΘÇÜτƒÑµ¥íΣ╗╢σÅÿΘçÅ
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }
            
            // µúÇµƒÑτ║┐τ¿ïµÿ»σÉªσ╖▓τ╗Åτ╗ôµ¥ƒ
            if (!play_thread_.joinable()) {
                thread_finished = true;
            }
        }
        
        if (play_thread_.joinable()) {
            play_thread_.join();
        }
        ESP_LOGI(TAG, "Playback thread finished");
    }
    
    // τ¡ëσ╛àµ¡îΦ»ìτ║┐τ¿ïτ╗ôµ¥ƒ
    if (lyric_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread finished");
    }
    
    // µ╕àτÉåτ╝ôσå▓σî║σÆîMP3ΦºúτáüσÖ¿
    ClearAudioBuffer();
    CleanupMp3Decoder();
    
    ESP_LOGI(TAG, "Music player destroyed successfully");
}

bool Esp32Music::Download(const std::string& song_name, const std::string& artist_name) {
    ESP_LOGI(TAG, "σ░ÅµÖ║σ╝Çµ║ÉΘƒ│Σ╣Éσ¢║Σ╗╢qqΣ║ñµ╡üτ╛ñ:826072986");
    ESP_LOGI(TAG, "Starting to get music details for: %s", song_name.c_str());
    
    // µ╕àτ⌐║Σ╣ïσëìτÜäΣ╕ïΦ╜╜µò░µì«
    last_downloaded_data_.clear();
    
    // Σ┐¥σ¡ÿµ¡îσÉìτö¿Σ║ÄσÉÄτ╗¡µÿ╛τñ║
    current_song_name_ = song_name;
    
    // τ¼¼Σ╕Çµ¡Ñ∩╝ÜΦ»╖µ▒éstream_pcmµÄÑσÅúΦÄ╖σÅûΘƒ│ΘóæΣ┐íµü»
    std::string base_url = "http://www.xiaozhishop.xyz:5005";
    std::string full_url = base_url + "/stream_pcm?song=" + url_encode(song_name) + "&artist=" + url_encode(artist_name);
    
    ESP_LOGI(TAG, "Request URL: %s", full_url.c_str());
    
    // Σ╜┐τö¿BoardµÅÉΣ╛¢τÜäHTTPσ«óµê╖τ½»
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    // Φ«╛τ╜«σƒ║µ£¼Φ»╖µ▒éσñ┤
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "application/json");
    
    // µ╖╗σèáESP32Φ«ñΦ»üσñ┤
    add_auth_headers(http.get());
    
    // µëôσ╝ÇGETΦ┐₧µÄÑ
    if (!http->Open("GET", full_url)) {
        ESP_LOGE(TAG, "Failed to connect to music API");
        return false;
    }
    
    // µúÇµƒÑσôìσ║öτè╢µÇüτáü
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        return false;
    }
    
    // Φ»╗σÅûσôìσ║öµò░µì«
    last_downloaded_data_ = http->ReadAll();
    http->Close();
    
    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d", status_code, last_downloaded_data_.length());
    ESP_LOGD(TAG, "Complete music details response: %s", last_downloaded_data_.c_str());
    
    // τ«ÇσìòτÜäΦ«ñΦ»üσôìσ║öµúÇµƒÑ∩╝êσÅ»ΘÇë∩╝ë
    if (last_downloaded_data_.find("ESP32σè¿µÇüσ»åΘÆÑΘ¬îΦ»üσñ▒Φ┤Ñ") != std::string::npos) {
        ESP_LOGE(TAG, "Authentication failed for song: %s", song_name.c_str());
        return false;
    }
    
    if (!last_downloaded_data_.empty()) {
        // Φºúµ₧Éσôìσ║öJSONΣ╗ÑµÅÉσÅûΘƒ│ΘóæURL
        cJSON* response_json = cJSON_Parse(last_downloaded_data_.c_str());
        if (response_json) {
            // µÅÉσÅûσà│Θö«Σ┐íµü»
            cJSON* artist = cJSON_GetObjectItem(response_json, "artist");
            cJSON* title = cJSON_GetObjectItem(response_json, "title");
            cJSON* audio_url = cJSON_GetObjectItem(response_json, "audio_url");
            cJSON* lyric_url = cJSON_GetObjectItem(response_json, "lyric_url");
            
            if (cJSON_IsString(artist)) {
                ESP_LOGI(TAG, "Artist: %s", artist->valuestring);
            }
            if (cJSON_IsString(title)) {
                ESP_LOGI(TAG, "Title: %s", title->valuestring);
            }
            
            // µúÇµƒÑaudio_urlµÿ»σÉªµ£ëµòê
            if (cJSON_IsString(audio_url) && audio_url->valuestring && strlen(audio_url->valuestring) > 0) {
                ESP_LOGI(TAG, "Audio URL path: %s", audio_url->valuestring);
                
                // τ¼¼Σ║îµ¡Ñ∩╝Üµï╝µÄÑσ«îµò┤τÜäΘƒ│ΘóæΣ╕ïΦ╜╜URL∩╝îτí«Σ┐¥σ»╣audio_urlΦ┐¢ΦíîURLτ╝ûτáü
                std::string audio_path = audio_url->valuestring;
                
                // Σ╜┐τö¿τ╗ƒΣ╕ÇτÜäURLµ₧äσ╗║σèƒΦâ╜
                if (audio_path.find("?") != std::string::npos) {
                    size_t query_pos = audio_path.find("?");
                    std::string path = audio_path.substr(0, query_pos);
                    std::string query = audio_path.substr(query_pos + 1);
                    
                    current_music_url_ = buildUrlWithParams(base_url, path, query);
                } else {
                    current_music_url_ = base_url + audio_path;
                }
                
                ESP_LOGI(TAG, "σ░ÅµÖ║σ╝Çµ║ÉΘƒ│Σ╣Éσ¢║Σ╗╢qqΣ║ñµ╡üτ╛ñ:826072986");
                ESP_LOGI(TAG, "Starting streaming playback for: %s", song_name.c_str());
                song_name_displayed_ = false;  // Θçìτ╜«µ¡îσÉìµÿ╛τñ║µáçσ┐ù
                StartStreaming(current_music_url_);
                
                // σñäτÉåµ¡îΦ»ìURL - σÅ¬µ£ëσ£¿µ¡îΦ»ìµÿ╛τñ║µ¿íσ╝ÅΣ╕ïµëìσÉ»σè¿µ¡îΦ»ì
                if (cJSON_IsString(lyric_url) && lyric_url->valuestring && strlen(lyric_url->valuestring) > 0) {
                    // µï╝µÄÑσ«îµò┤τÜäµ¡îΦ»ìΣ╕ïΦ╜╜URL∩╝îΣ╜┐τö¿τ¢╕σÉîτÜäURLµ₧äσ╗║ΘÇ╗Φ╛æ
                    std::string lyric_path = lyric_url->valuestring;
                    if (lyric_path.find("?") != std::string::npos) {
                        size_t query_pos = lyric_path.find("?");
                        std::string path = lyric_path.substr(0, query_pos);
                        std::string query = lyric_path.substr(query_pos + 1);
                        
                        current_lyric_url_ = buildUrlWithParams(base_url, path, query);
                    } else {
                        current_lyric_url_ = base_url + lyric_path;
                    }
                    
                    // µá╣µì«µÿ╛τñ║µ¿íσ╝Åσå│σ«Üµÿ»σÉªσÉ»σè¿µ¡îΦ»ì
                    if (display_mode_ == DISPLAY_MODE_LYRICS) {
                        ESP_LOGI(TAG, "Loading lyrics for: %s (lyrics display mode)", song_name.c_str());
                        
                        // σÉ»σè¿µ¡îΦ»ìΣ╕ïΦ╜╜σÆîµÿ╛τñ║
                        if (is_lyric_running_) {
                            is_lyric_running_ = false;
                            if (lyric_thread_.joinable()) {
                                lyric_thread_.join();
                            }
                        }
                        
                        is_lyric_running_ = true;
                        current_lyric_index_ = -1;
                        lyrics_.clear();
                        
                        lyric_thread_ = std::thread(&Esp32Music::LyricDisplayThread, this);
                    } else {
                        ESP_LOGI(TAG, "Lyric URL found but spectrum display mode is active, skipping lyrics");
                    }
                } else {
                    ESP_LOGW(TAG, "No lyric URL found for this song");
                }
                
                cJSON_Delete(response_json);
                return true;
            } else {
                // audio_urlΣ╕║τ⌐║µêûµùáµòê
                ESP_LOGE(TAG, "Audio URL not found or empty for song: %s", song_name.c_str());
                ESP_LOGE(TAG, "Failed to find music: µ▓íµ£ëµë╛σê░µ¡îµ¢▓ '%s'", song_name.c_str());
                cJSON_Delete(response_json);
                return false;
            }
        } else {
            ESP_LOGE(TAG, "Failed to parse JSON response");
        }
    } else {
        ESP_LOGE(TAG, "Empty response from music API");
    }
    
    return false;
}



std::string Esp32Music::GetDownloadResult() {
    return last_downloaded_data_;
}

// σ╝Çσºïµ╡üσ╝ÅµÆ¡µö╛
bool Esp32Music::StartStreaming(const std::string& music_url) {
    if (music_url.empty()) {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    
    ESP_LOGD(TAG, "Starting streaming for URL: %s", music_url.c_str());
    
    // σü£µ¡óΣ╣ïσëìτÜäµÆ¡µö╛σÆîΣ╕ïΦ╜╜
    is_downloading_ = false;
    is_playing_ = false;
    
    // τ¡ëσ╛àΣ╣ïσëìτÜäτ║┐τ¿ïσ«îσà¿τ╗ôµ¥ƒ
    if (download_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();  // ΘÇÜτƒÑτ║┐τ¿ïΘÇÇσç║
        }
        download_thread_.join();
    }
    if (play_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();  // ΘÇÜτƒÑτ║┐τ¿ïΘÇÇσç║
        }
        play_thread_.join();
    }
    
    // µ╕àτ⌐║τ╝ôσå▓σî║
    ClearAudioBuffer();
    
    // Θàìτ╜«τ║┐τ¿ïµáêσñºσ░ÅΣ╗ÑΘü┐σàìµáêµ║óσç║
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 8192;  // 8KBµáêσñºσ░Å
    cfg.prio = 5;           // Σ╕¡τ¡ëΣ╝ÿσàêτ║º
    cfg.thread_name = "audio_stream";
    esp_pthread_set_cfg(&cfg);
    
    // σ╝ÇσºïΣ╕ïΦ╜╜τ║┐τ¿ï
    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, music_url);
    
    // σ╝ÇσºïµÆ¡µö╛τ║┐τ¿ï∩╝êΣ╝Üτ¡ëσ╛àτ╝ôσå▓σî║µ£ëΦ╢│σñƒµò░µì«∩╝ë
    is_playing_ = true;
    play_thread_ = std::thread(&Esp32Music::PlayAudioStream, this);
    
    ESP_LOGI(TAG, "Streaming threads started successfully");
    
    return true;
}

// σü£µ¡óµ╡üσ╝ÅµÆ¡µö╛
bool Esp32Music::StopStreaming() {
    ESP_LOGI(TAG, "Stopping music streaming - current state: downloading=%d, playing=%d", 
            is_downloading_.load(), is_playing_.load());

    // Θçìτ╜«Θççµá╖τÄçσê░σÄƒσºïσÇ╝
    ResetSampleRate();
    
    // µúÇµƒÑµÿ»σÉªµ£ëµ╡üσ╝ÅµÆ¡µö╛µ¡úσ£¿Φ┐¢Φíî
    if (!is_playing_ && !is_downloading_) {
        ESP_LOGW(TAG, "No streaming in progress");
        return true;
    }
    
    // σü£µ¡óΣ╕ïΦ╜╜σÆîµÆ¡µö╛µáçσ┐ù
    is_downloading_ = false;
    is_playing_ = false;
    
    // 清空显示信息
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        // display->SetMusicInfo("");  // TODO: 需要实现这个方法
        ESP_LOGI(TAG, "Cleared song name display");
    }
    
    // ΘÇÜτƒÑµëÇµ£ëτ¡ëσ╛àτÜäτ║┐τ¿ï
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // τ¡ëσ╛àτ║┐τ¿ïτ╗ôµ¥ƒ∩╝êΘü┐σàìΘçìσñìΣ╗úτáü∩╝îΦ«⌐StopStreamingΣ╣ƒΦâ╜τ¡ëσ╛àτ║┐τ¿ïσ«îσà¿σü£µ¡ó∩╝ë
    if (download_thread_.joinable()) {
        download_thread_.join();
        ESP_LOGI(TAG, "Download thread joined in StopStreaming");
    }
    
    // τ¡ëσ╛àµÆ¡µö╛τ║┐τ¿ïτ╗ôµ¥ƒ∩╝îΣ╜┐τö¿µ¢┤σ«ëσà¿τÜäµû╣σ╝Å
    if (play_thread_.joinable()) {
        // σàêΦ«╛τ╜«σü£µ¡óµáçσ┐ù
        is_playing_ = false;
        
        // ΘÇÜτƒÑµ¥íΣ╗╢σÅÿΘçÅ∩╝îτí«Σ┐¥τ║┐τ¿ïΦâ╜σñƒΘÇÇσç║
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();
        }
        
        // Σ╜┐τö¿Φ╢àµù╢µ£║σê╢τ¡ëσ╛àτ║┐τ¿ïτ╗ôµ¥ƒ∩╝îΘü┐σàìµ¡╗Θöü
        bool thread_finished = false;
        int wait_count = 0;
        const int max_wait = 100; // µ£ÇσñÜτ¡ëσ╛à1τºÆ
        
        while (!thread_finished && wait_count < max_wait) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_count++;
            
            // µúÇµƒÑτ║┐τ¿ïµÿ»σÉªΣ╗ìτä╢σÅ»join
            if (!play_thread_.joinable()) {
                thread_finished = true;
                break;
            }
        }
        
        if (play_thread_.joinable()) {
            if (wait_count >= max_wait) {
                ESP_LOGW(TAG, "Play thread join timeout, detaching thread");
                play_thread_.detach();
            } else {
                play_thread_.join();
                ESP_LOGI(TAG, "Play thread joined in StopStreaming");
            }
        }
    }
    
    // σ£¿τ║┐τ¿ïσ«îσà¿τ╗ôµ¥ƒσÉÄ∩╝îσÅ¬σ£¿ΘóæΦ░▒µ¿íσ╝ÅΣ╕ïσü£µ¡óFFTµÿ╛τñ║
    if (display && display_mode_ == DISPLAY_MODE_SPECTRUM) {
        // display->stopFft();  // TODO: 需要实现这个方法
        ESP_LOGI(TAG, "Stopped FFT display in StopStreaming (spectrum mode)");
    } else if (display) {
        ESP_LOGI(TAG, "Not in spectrum mode, skipping FFT stop in StopStreaming");
    }
    
    ESP_LOGI(TAG, "Music streaming stop signal sent");
    return true;
}

// µ╡üσ╝ÅΣ╕ïΦ╜╜Θƒ│Θóæµò░µì«
void Esp32Music::DownloadAudioStream(const std::string& music_url) {
    ESP_LOGD(TAG, "Starting audio stream download from: %s", music_url.c_str());
    
    // Θ¬îΦ»üURLµ£ëµòêµÇº
    if (music_url.empty() || music_url.find("http") != 0) {
        ESP_LOGE(TAG, "Invalid URL format: %s", music_url.c_str());
        is_downloading_ = false;
        return;
    }
    
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    // Φ«╛τ╜«σƒ║µ£¼Φ»╖µ▒éσñ┤
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Range", "bytes=0-");  // µö»µîüµû¡τé╣τ╗¡Σ╝á
    
    // µ╖╗σèáESP32Φ«ñΦ»üσñ┤
    add_auth_headers(http.get());
    
    if (!http->Open("GET", music_url)) {
        ESP_LOGE(TAG, "Failed to connect to music stream URL");
        is_downloading_ = false;
        return;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206) {  // 206 for partial content
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "Started downloading audio stream, status: %d", status_code);
    
    // σêåσ¥ùΦ»╗σÅûΘƒ│Θóæµò░µì«
    const size_t chunk_size = 4096;  // 4KBµ»Åσ¥ù
    char buffer[chunk_size];
    size_t total_downloaded = 0;
    
    while (is_downloading_ && is_playing_) {
        int bytes_read = http->Read(buffer, chunk_size);
        if (bytes_read < 0) {
            ESP_LOGE(TAG, "Failed to read audio data: error code %d", bytes_read);
            break;
        }
        if (bytes_read == 0) {
            ESP_LOGI(TAG, "Audio stream download completed, total: %d bytes", total_downloaded);
            break;
        }
        
        // µëôσì░µò░µì«σ¥ùΣ┐íµü»
        // ESP_LOGI(TAG, "Downloaded chunk: %d bytes at offset %d", bytes_read, total_downloaded);
        
        // σ«ëσà¿σ£░µëôσì░µò░µì«σ¥ùτÜäσìüσà¡Φ┐¢σê╢σåàσ«╣∩╝êσëì16σ¡ùΦèé∩╝ë
        if (bytes_read >= 16) {
            // ESP_LOGI(TAG, "Data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X ...", 
            //         (unsigned char)buffer[0], (unsigned char)buffer[1], (unsigned char)buffer[2], (unsigned char)buffer[3],
            //         (unsigned char)buffer[4], (unsigned char)buffer[5], (unsigned char)buffer[6], (unsigned char)buffer[7],
            //         (unsigned char)buffer[8], (unsigned char)buffer[9], (unsigned char)buffer[10], (unsigned char)buffer[11],
            //         (unsigned char)buffer[12], (unsigned char)buffer[13], (unsigned char)buffer[14], (unsigned char)buffer[15]);
        } else {
            ESP_LOGI(TAG, "Data chunk too small: %d bytes", bytes_read);
        }
        
        // σ░¥Φ»òµúÇµ╡ïµûçΣ╗╢µá╝σ╝Å∩╝êµúÇµƒÑµûçΣ╗╢σñ┤∩╝ë
        if (total_downloaded == 0 && bytes_read >= 4) {
            if (memcmp(buffer, "ID3", 3) == 0) {
                ESP_LOGI(TAG, "Detected MP3 file with ID3 tag");
            } else if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0) {
                ESP_LOGI(TAG, "Detected MP3 file header");
            } else if (memcmp(buffer, "RIFF", 4) == 0) {
                ESP_LOGI(TAG, "Detected WAV file");
            } else if (memcmp(buffer, "fLaC", 4) == 0) {
                ESP_LOGI(TAG, "Detected FLAC file");
            } else if (memcmp(buffer, "OggS", 4) == 0) {
                ESP_LOGI(TAG, "Detected OGG file");
            } else {
                ESP_LOGI(TAG, "Unknown audio format, first 4 bytes: %02X %02X %02X %02X", 
                        (unsigned char)buffer[0], (unsigned char)buffer[1], 
                        (unsigned char)buffer[2], (unsigned char)buffer[3]);
            }
        }
        
        // σê¢σ╗║Θƒ│Θóæµò░µì«σ¥ù
        uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for audio chunk");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);
        
        // τ¡ëσ╛àτ╝ôσå▓σî║µ£ëτ⌐║Θù┤
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });
            
            if (is_downloading_) {
                audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;
                
                // ΘÇÜτƒÑµÆ¡µö╛τ║┐τ¿ïµ£ëµû░µò░µì«
                buffer_cv_.notify_one();
                
                if (total_downloaded % (256 * 1024) == 0) {  // µ»Å256KBµëôσì░Σ╕Çµ¼íΦ┐¢σ║ª
                    ESP_LOGI(TAG, "Downloaded %d bytes, buffer size: %d", total_downloaded, buffer_size_);
                }
            } else {
                heap_caps_free(chunk_data);
                break;
            }
        }
    }
    
    http->Close();
    is_downloading_ = false;
    
    // ΘÇÜτƒÑµÆ¡µö╛τ║┐τ¿ïΣ╕ïΦ╜╜σ«îµêÉ
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    ESP_LOGI(TAG, "Audio stream download thread finished");
}

// µ╡üσ╝ÅµÆ¡µö╛Θƒ│Θóæµò░µì«
void Esp32Music::PlayAudioStream() {
    ESP_LOGI(TAG, "Starting audio stream playback");
    
    // σê¥σºïσîûµù╢Θù┤Φ╖ƒΦ╕¬σÅÿΘçÅ
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;
    
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec || !codec->output_enabled()) {
        ESP_LOGE(TAG, "Audio codec not available or not enabled");
        is_playing_ = false;
        return;
    }
    
    if (!mp3_decoder_initialized_) {
        ESP_LOGE(TAG, "MP3 decoder not initialized");
        is_playing_ = false;
        return;
    }
    
    
    // τ¡ëσ╛àτ╝ôσå▓σî║µ£ëΦ╢│σñƒµò░µì«σ╝ÇσºïµÆ¡µö╛
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this] { 
            return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty()); 
        });
    }
    
    ESP_LOGI(TAG, "σ░ÅµÖ║σ╝Çµ║ÉΘƒ│Σ╣Éσ¢║Σ╗╢qqΣ║ñµ╡üτ╛ñ:826072986");
    ESP_LOGI(TAG, "Starting playback with buffer size: %d", buffer_size_);
    
    size_t total_played = 0;
    uint8_t* mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t* read_ptr = nullptr;
    
    // σêåΘàìMP3Φ╛ôσàÑτ╝ôσå▓σî║
    mp3_input_buffer = (uint8_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer) {
        ESP_LOGE(TAG, "Failed to allocate MP3 input buffer");
        is_playing_ = false;
        return;
    }
    
    // µáçΦ«░µÿ»σÉªσ╖▓τ╗ÅσñäτÉåΦ┐çID3µáçτ¡╛
    bool id3_processed = false;
    
    while (is_playing_) {
        // µúÇµƒÑΦ«╛σñçτè╢µÇü∩╝îσÅ¬µ£ëσ£¿τ⌐║Θù▓τè╢µÇüµëìµÆ¡µö╛Θƒ│Σ╣É
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();
        
        // τè╢µÇüΦ╜¼µìó∩╝ÜΦ»┤Φ»¥Σ╕¡-πÇïΦüåσÉ¼Σ╕¡-πÇïσ╛àµ£║τè╢µÇü-πÇïµÆ¡µö╛Θƒ│Σ╣É
        if (current_state == kDeviceStateListening || current_state == kDeviceStateSpeaking) {
            if (current_state == kDeviceStateSpeaking) {
                ESP_LOGI(TAG, "Device is in speaking state, switching to listening state for music playback");
            }
            if (current_state == kDeviceStateListening) {
                ESP_LOGI(TAG, "Device is in listening state, switching to idle state for music playback");
            }
            // σêçµìóτè╢µÇü
            app.ToggleChatState(); // σÅÿµêÉσ╛àµ£║τè╢µÇü
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        } else if (current_state != kDeviceStateIdle) { // Σ╕ìµÿ»σ╛àµ£║τè╢µÇü∩╝îσ░▒Σ╕Çτ¢┤σìíσ£¿Φ┐ÖΘçî∩╝îΣ╕ìΦ«⌐µÆ¡µö╛Θƒ│Σ╣É
            ESP_LOGD(TAG, "Device state is %d, pausing music playback", current_state);
            // σªéµ₧£Σ╕ìµÿ»τ⌐║Θù▓τè╢µÇü∩╝îµÜéσü£µÆ¡µö╛
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        // Φ«╛σñçτè╢µÇüµúÇµƒÑΘÇÜΦ┐ç∩╝îµÿ╛τñ║σ╜ôσëìµÆ¡µö╛τÜäµ¡îσÉì
        if (!song_name_displayed_ && !current_song_name_.empty()) {
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            if (display) {
                // 格式化显示信息为：《歌曲》播放中...
                std::string formatted_song_name = "《" + current_song_name_ + "》播放中...";
                // display->SetMusicInfo(formatted_song_name.c_str());  // TODO: 需要实现这个方法
                ESP_LOGI(TAG, "Displaying song name: %s", formatted_song_name.c_str());
                song_name_displayed_ = true;
            }

            // µá╣µì«µÿ╛τñ║µ¿íσ╝ÅσÉ»σè¿τ¢╕σ║öτÜäµÿ╛τñ║σèƒΦâ╜
            if (display) {
                if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
                    // display->start();  // TODO: 需要实现这个方法
                    ESP_LOGI(TAG, "Display start() called for spectrum visualization");
                } else {
                    ESP_LOGI(TAG, "Lyrics display mode active, FFT visualization disabled");
                }
            }
        }
        
        // σªéµ₧£Θ£ÇΦªüµ¢┤σñÜMP3µò░µì«∩╝îΣ╗Äτ╝ôσå▓σî║Φ»╗σÅû
        if (bytes_left < 4096) {  // Σ┐¥µîüΦç│σ░æ4KBµò░µì«τö¿Σ║ÄΦºúτáü
            AudioChunk chunk;
            
            // Σ╗Äτ╝ôσå▓σî║ΦÄ╖σÅûΘƒ│Θóæµò░µì«
            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (audio_buffer_.empty()) {
                    if (!is_downloading_) {
                        // Σ╕ïΦ╜╜σ«îµêÉΣ╕öτ╝ôσå▓σî║Σ╕║τ⌐║∩╝îµÆ¡µö╛τ╗ôµ¥ƒ
                        ESP_LOGI(TAG, "Playback finished, total played: %d bytes", total_played);
                        break;
                    }
                    // τ¡ëσ╛àµû░µò░µì«
                    buffer_cv_.wait(lock, [this] { return !audio_buffer_.empty() || !is_downloading_; });
                    if (audio_buffer_.empty()) {
                        continue;
                    }
                }
                
                chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;
                
                // ΘÇÜτƒÑΣ╕ïΦ╜╜τ║┐τ¿ïτ╝ôσå▓σî║µ£ëτ⌐║Θù┤
                buffer_cv_.notify_one();
            }
            
            // σ░åµû░µò░µì«µ╖╗σèáσê░MP3Φ╛ôσàÑτ╝ôσå▓σî║
            if (chunk.data && chunk.size > 0) {
                // τº╗σè¿σë⌐Σ╜Öµò░µì«σê░τ╝ôσå▓σî║σ╝Çσñ┤
                if (bytes_left > 0 && read_ptr != mp3_input_buffer) {
                    memmove(mp3_input_buffer, read_ptr, bytes_left);
                }
                
                // µúÇµƒÑτ╝ôσå▓σî║τ⌐║Θù┤
                size_t space_available = 8192 - bytes_left;
                size_t copy_size = std::min(chunk.size, space_available);
                
                // σñìσê╢µû░µò░µì«
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;
                
                // µúÇµƒÑσ╣╢Φ╖│Φ┐çID3µáçτ¡╛∩╝êΣ╗àσ£¿σ╝Çσºïµù╢σñäτÉåΣ╕Çµ¼í∩╝ë
                if (!id3_processed && bytes_left >= 10) {
                    size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
                    if (id3_skip > 0) {
                        read_ptr += id3_skip;
                        bytes_left -= id3_skip;
                        ESP_LOGI(TAG, "Skipped ID3 tag: %u bytes", (unsigned int)id3_skip);
                    }
                    id3_processed = true;
                }
                
                // Θçèµö╛chunkσåàσ¡ÿ
                heap_caps_free(chunk.data);
            }
        }
        
        // σ░¥Φ»òµë╛σê░MP3σ╕ºσÉîµ¡Ñ
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0) {
            ESP_LOGW(TAG, "No MP3 sync word found, skipping %d bytes", bytes_left);
            bytes_left = 0;
            continue;
        }
        
        // Φ╖│Φ┐çσê░σÉîµ¡ÑΣ╜ìτ╜«
        if (sync_offset > 0) {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }
        
        // ΦºúτáüMP3σ╕º
        int16_t pcm_buffer[2304];
        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);
        
        if (decode_result == 0) {
            // ΦºúτáüµêÉσèƒ∩╝îΦÄ╖σÅûσ╕ºΣ┐íµü»
            MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
            total_frames_decoded_++;
            
            // σƒ║µ£¼τÜäσ╕ºΣ┐íµü»µ£ëµòêµÇºµúÇµƒÑ∩╝îΘÿ▓µ¡óΘÖñΘ¢╢ΘöÖΦ»»
            if (mp3_frame_info_.samprate == 0 || mp3_frame_info_.nChans == 0) {
                ESP_LOGW(TAG, "Invalid frame info: rate=%d, channels=%d, skipping", 
                        mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }
            
            // Φ«íτ«ùσ╜ôσëìσ╕ºτÜäµîüτ╗¡µù╢Θù┤(µ»½τºÆ)
            int frame_duration_ms = (mp3_frame_info_.outputSamps * 1000) / 
                                  (mp3_frame_info_.samprate * mp3_frame_info_.nChans);
            
            // µ¢┤µû░σ╜ôσëìµÆ¡µö╛µù╢Θù┤
            current_play_time_ms_ += frame_duration_ms;
            
            ESP_LOGD(TAG, "Frame %d: time=%lldms, duration=%dms, rate=%d, ch=%d", 
                    total_frames_decoded_, current_play_time_ms_, frame_duration_ms,
                    mp3_frame_info_.samprate, mp3_frame_info_.nChans);
            
            // µ¢┤µû░µ¡îΦ»ìµÿ╛τñ║
            int buffer_latency_ms = 600; // σ«₧µ╡ïΦ░âµò┤σÇ╝
            UpdateLyricDisplay(current_play_time_ms_ + buffer_latency_ms);
            
            // σ░åPCMµò░µì«σÅæΘÇüσê░ApplicationτÜäΘƒ│ΘóæΦºúτáüΘÿƒσêù
            if (mp3_frame_info_.outputSamps > 0) {
                int16_t* final_pcm_data = pcm_buffer;
                int final_sample_count = mp3_frame_info_.outputSamps;
                std::vector<int16_t> mono_buffer;
                
                // σªéµ₧£µÿ»σÅîΘÇÜΘüô∩╝îΦ╜¼µìóΣ╕║σìòΘÇÜΘüôµ╖╖σÉê
                if (mp3_frame_info_.nChans == 2) {
                    // σÅîΘÇÜΘüôΦ╜¼σìòΘÇÜΘüô∩╝Üσ░åσ╖ªσÅ│σú░Θüôµ╖╖σÉê
                    int stereo_samples = mp3_frame_info_.outputSamps;  // σîàσÉ½σ╖ªσÅ│σú░ΘüôτÜäµÇ╗µá╖µ£¼µò░
                    int mono_samples = stereo_samples / 2;  // σ«₧ΘÖàτÜäσìòσú░Θüôµá╖µ£¼µò░
                    
                    mono_buffer.resize(mono_samples);
                    
                    for (int i = 0; i < mono_samples; ++i) {
                        // µ╖╖σÉêσ╖ªσÅ│σú░Θüô (L + R) / 2
                        int left = pcm_buffer[i * 2];      // σ╖ªσú░Θüô
                        int right = pcm_buffer[i * 2 + 1]; // σÅ│σú░Θüô
                        mono_buffer[i] = (int16_t)((left + right) / 2);
                    }
                    
                    final_pcm_data = mono_buffer.data();
                    final_sample_count = mono_samples;

                    ESP_LOGD(TAG, "Converted stereo to mono: %d -> %d samples", 
                            stereo_samples, mono_samples);
                } else if (mp3_frame_info_.nChans == 1) {
                    // σ╖▓τ╗Åµÿ»σìòσú░Θüô∩╝îµùáΘ£ÇΦ╜¼µìó
                    ESP_LOGD(TAG, "Already mono audio: %d samples", final_sample_count);
                } else {
                    ESP_LOGW(TAG, "Unsupported channel count: %d, treating as mono", 
                            mp3_frame_info_.nChans);
                }
                
                // σê¢σ╗║AudioStreamPacket
                AudioStreamPacket packet;
                packet.sample_rate = mp3_frame_info_.samprate;
                packet.frame_duration = 60;  // Σ╜┐τö¿ApplicationΘ╗ÿΦ«ñτÜäσ╕ºµù╢Θò┐
                packet.timestamp = 0;
                
                // σ░åint16_t PCMµò░µì«Φ╜¼µìóΣ╕║uint8_tσ¡ùΦèéµò░τ╗ä
                size_t pcm_size_bytes = final_sample_count * sizeof(int16_t);
                packet.payload.resize(pcm_size_bytes);
                memcpy(packet.payload.data(), final_pcm_data, pcm_size_bytes);

                if (final_pcm_data_fft == nullptr) {
                    final_pcm_data_fft = (int16_t*)heap_caps_malloc(
                        final_sample_count * sizeof(int16_t),
                        MALLOC_CAP_SPIRAM
                    );
                }
                
                memcpy(
                    final_pcm_data_fft,
                    final_pcm_data,
                    final_sample_count * sizeof(int16_t)
                );
                
                ESP_LOGD(TAG, "Sending %d PCM samples (%d bytes, rate=%d, channels=%d->1) to Application", 
                        final_sample_count, pcm_size_bytes, mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                
                // 发送到Application的音频处理链
                app.AddAudioData(std::move(packet));
                total_played += pcm_size_bytes;
                
                // µëôσì░µÆ¡µö╛Φ┐¢σ║ª
                if (total_played % (128 * 1024) == 0) {
                    ESP_LOGI(TAG, "Played %d bytes, buffer size: %d", total_played, buffer_size_);
                }
            }
            
        } else {
            // Φºúτáüσñ▒Φ┤Ñ
            ESP_LOGW(TAG, "MP3 decode failed with error: %d", decode_result);
            
            // Φ╖│Φ┐çΣ╕ÇΣ║¢σ¡ùΦèéτ╗ºτ╗¡σ░¥Φ»ò
            if (bytes_left > 1) {
                read_ptr++;
                bytes_left--;
            } else {
                bytes_left = 0;
            }
        }
    }
    
    // µ╕àτÉå
    if (mp3_input_buffer) {
        heap_caps_free(mp3_input_buffer);
    }
    
    // µÆ¡µö╛τ╗ôµ¥ƒµù╢Φ┐¢Φíîσƒ║µ£¼µ╕àτÉå∩╝îΣ╜åΣ╕ìΦ░âτö¿StopStreamingΘü┐σàìτ║┐τ¿ïΦç¬µêæτ¡ëσ╛à
    ESP_LOGI(TAG, "Audio stream playback finished, total played: %d bytes", total_played);
    ESP_LOGI(TAG, "Performing basic cleanup from play thread");
    
    // σü£µ¡óµÆ¡µö╛µáçσ┐ù
    is_playing_ = false;
    
    // σÅ¬σ£¿ΘóæΦ░▒µÿ╛τñ║µ¿íσ╝ÅΣ╕ïµëìσü£µ¡óFFTµÿ╛τñ║
    if (display_mode_ == DISPLAY_MODE_SPECTRUM) {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            // display->stopFft();  // TODO: 需要实现这个方法
            ESP_LOGI(TAG, "Stopped FFT display from play thread (spectrum mode)");
        }
    } else {
        ESP_LOGI(TAG, "Not in spectrum mode, skipping FFT stop");
    }
}

// µ╕àτ⌐║Θƒ│Θóæτ╝ôσå▓σî║
void Esp32Music::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    while (!audio_buffer_.empty()) {
        AudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
    }
    
    buffer_size_ = 0;
    ESP_LOGI(TAG, "Audio buffer cleared");
}

// σê¥σºïσîûMP3ΦºúτáüσÖ¿
bool Esp32Music::InitializeMp3Decoder() {
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        mp3_decoder_initialized_ = false;
        return false;
    }
    
    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
    return true;
}

// µ╕àτÉåMP3ΦºúτáüσÖ¿
void Esp32Music::CleanupMp3Decoder() {
    if (mp3_decoder_ != nullptr) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
    ESP_LOGI(TAG, "MP3 decoder cleaned up");
}

// Θçìτ╜«Θççµá╖τÄçσê░σÄƒσºïσÇ╝
void Esp32Music::ResetSampleRate() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    // TODO: 需要实现 original_output_sample_rate() 和 SetOutputSampleRate() 方法
    /*
    if (codec && codec->original_output_sample_rate() > 0 && 
        codec->output_sample_rate() != codec->original_output_sample_rate()) {
        ESP_LOGI(TAG, "重置采样率从：%d Hz 重置到原始值 %d Hz", 
                codec->output_sample_rate(), codec->original_output_sample_rate());
        if (codec->SetOutputSampleRate(-1)) {  // -1 意味重置到原始值
            ESP_LOGI(TAG, "成功重置采样率到原始值: %d Hz", codec->output_sample_rate());
        } else {
            ESP_LOGW(TAG, "失败重置采样率到原始值");
        }
    }
    */
    if (codec) {
        ESP_LOGI(TAG, "AudioCodec methods not implemented yet");
    }
}

// Φ╖│Φ┐çMP3µûçΣ╗╢σ╝Çσñ┤τÜäID3µáçτ¡╛
size_t Esp32Music::SkipId3Tag(uint8_t* data, size_t size) {
    if (!data || size < 10) {
        return 0;
    }
    
    // µúÇµƒÑID3v2µáçτ¡╛σñ┤ "ID3"
    if (memcmp(data, "ID3", 3) != 0) {
        return 0;
    }
    
    // Φ«íτ«ùµáçτ¡╛σñºσ░Å∩╝êsynchsafe integerµá╝σ╝Å∩╝ë
    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7)  |
                        ((uint32_t)(data[9] & 0x7F));
    
    // ID3v2σñ┤Θâ¿(10σ¡ùΦèé) + µáçτ¡╛σåàσ«╣
    size_t total_skip = 10 + tag_size;
    
    // τí«Σ┐¥Σ╕ìΦ╢àΦ┐çσÅ»τö¿µò░µì«σñºσ░Å
    if (total_skip > size) {
        total_skip = size;
    }
    
    ESP_LOGI(TAG, "Found ID3v2 tag, skipping %u bytes", (unsigned int)total_skip);
    return total_skip;
}

// Σ╕ïΦ╜╜µ¡îΦ»ì
bool Esp32Music::DownloadLyrics(const std::string& lyric_url) {
    ESP_LOGI(TAG, "Downloading lyrics from: %s", lyric_url.c_str());
    
    // µúÇµƒÑURLµÿ»σÉªΣ╕║τ⌐║
    if (lyric_url.empty()) {
        ESP_LOGE(TAG, "Lyric URL is empty!");
        return false;
    }
    
    // µ╖╗σèáΘçìΦ»òΘÇ╗Φ╛æ
    const int max_retries = 3;
    int retry_count = 0;
    bool success = false;
    std::string lyric_content;
    std::string current_url = lyric_url;
    int redirect_count = 0;
    const int max_redirects = 5;  // µ£ÇσñÜσàüΦ«╕5µ¼íΘçìσ«ÜσÉæ
    
    while (retry_count < max_retries && !success && redirect_count < max_redirects) {
        if (retry_count > 0) {
            ESP_LOGI(TAG, "Retrying lyric download (attempt %d of %d)", retry_count + 1, max_retries);
            // ΘçìΦ»òσëìµÜéσü£Σ╕ÇΣ╕ï
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        // Σ╜┐τö¿BoardµÅÉΣ╛¢τÜäHTTPσ«óµê╖τ½»
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(0);
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for lyric download");
            retry_count++;
            continue;
        }
        
        // Φ«╛τ╜«σƒ║µ£¼Φ»╖µ▒éσñ┤
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "text/plain");
        
        // µ╖╗σèáESP32Φ«ñΦ»üσñ┤
        add_auth_headers(http.get());
        
        // µëôσ╝ÇGETΦ┐₧µÄÑ
        ESP_LOGI(TAG, "σ░ÅµÖ║σ╝Çµ║ÉΘƒ│Σ╣Éσ¢║Σ╗╢qqΣ║ñµ╡üτ╛ñ:826072986");
        if (!http->Open("GET", current_url)) {
            ESP_LOGE(TAG, "Failed to open HTTP connection for lyrics");
            // τº╗ΘÖñdelete http; σ¢áΣ╕║unique_ptrΣ╝ÜΦç¬σè¿τ«íτÉåσåàσ¡ÿ
            retry_count++;
            continue;
        }
        
        // µúÇµƒÑHTTPτè╢µÇüτáü
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "Lyric download HTTP status code: %d", status_code);
        
        // σñäτÉåΘçìσ«ÜσÉæ - τö▒Σ║ÄHttpτ▒╗µ▓íµ£ëGetHeaderµû╣µ│ò∩╝îµêæΣ╗¼σÅ¬Φâ╜µá╣µì«τè╢µÇüτáüσêñµû¡
        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
            // τö▒Σ║Äµùáµ│òΦÄ╖σÅûLocationσñ┤∩╝îσÅ¬Φâ╜µèÑσæèΘçìσ«ÜσÉæΣ╜åµùáµ│òτ╗ºτ╗¡
            ESP_LOGW(TAG, "Received redirect status %d but cannot follow redirect (no GetHeader method)", status_code);
            http->Close();
            retry_count++;
            continue;
        }
        
        // Θ¥₧200τ│╗σêùτè╢µÇüτáüΦºåΣ╕║ΘöÖΦ»»
        if (status_code < 200 || status_code >= 300) {
            ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
            http->Close();
            retry_count++;
            continue;
        }
        
        // Φ»╗σÅûσôìσ║ö
        lyric_content.clear();
        char buffer[1024];
        int bytes_read;
        bool read_error = false;
        int total_read = 0;
        
        // τö▒Σ║Äµùáµ│òΦÄ╖σÅûContent-LengthσÆîContent-Typeσñ┤∩╝îµêæΣ╗¼Σ╕ìτƒÑΘüôΘóäµ£ƒσñºσ░ÅσÆîσåàσ«╣τ▒╗σ₧ï
        ESP_LOGD(TAG, "Starting to read lyric content");
        
        while (true) {
            bytes_read = http->Read(buffer, sizeof(buffer) - 1);
            // ESP_LOGD(TAG, "Lyric HTTP read returned %d bytes", bytes_read); // µ│¿ΘçèµÄëΣ╗ÑσçÅσ░æµùÑσ┐ùΦ╛ôσç║
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                lyric_content += buffer;
                total_read += bytes_read;
                
                // σ«Üµ£ƒµëôσì░Σ╕ïΦ╜╜Φ┐¢σ║ª - µö╣Σ╕║DEBUGτ║ºσê½σçÅσ░æΦ╛ôσç║
                if (total_read % 4096 == 0) {
                    ESP_LOGD(TAG, "Downloaded %d bytes so far", total_read);
                }
            } else if (bytes_read == 0) {
                // µ¡úσ╕╕τ╗ôµ¥ƒ∩╝îµ▓íµ£ëµ¢┤σñÜµò░µì«
                ESP_LOGD(TAG, "Lyric download completed, total bytes: %d", total_read);
                success = true;
                break;
            } else {
                // bytes_read < 0∩╝îσÅ»Φâ╜µÿ»ESP-IDFτÜäσ╖▓τƒÑΘù«Θóÿ
                // σªéµ₧£σ╖▓τ╗ÅΦ»╗σÅûσê░Σ║åΣ╕ÇΣ║¢µò░µì«∩╝îσêÖΦ«ñΣ╕║Σ╕ïΦ╜╜µêÉσèƒ
                if (!lyric_content.empty()) {
                    ESP_LOGW(TAG, "HTTP read returned %d, but we have data (%d bytes), continuing", bytes_read, lyric_content.length());
                    success = true;
                    break;
                } else {
                    ESP_LOGE(TAG, "Failed to read lyric data: error code %d", bytes_read);
                    read_error = true;
                    break;
                }
            }
        }
        
        http->Close();
        
        if (read_error) {
            retry_count++;
            continue;
        }
        
        // σªéµ₧£µêÉσèƒΦ»╗σÅûµò░µì«∩╝îΦ╖│σç║ΘçìΦ»òσ╛¬τÄ»
        if (success) {
            break;
        }
    }
    
    // µúÇµƒÑµÿ»σÉªΦ╢àΦ┐çΣ║åµ£ÇσñºΘçìΦ»òµ¼íµò░
    if (retry_count >= max_retries) {
        ESP_LOGE(TAG, "Failed to download lyrics after %d attempts", max_retries);
        return false;
    }
    
    // Φ«░σ╜òσëìσçáΣ╕¬σ¡ùΦèéτÜäµò░µì«∩╝îσ╕«σè⌐Φ░âΦ»ò
    if (!lyric_content.empty()) {
        size_t preview_size = std::min(lyric_content.size(), size_t(50));
        std::string preview = lyric_content.substr(0, preview_size);
        ESP_LOGD(TAG, "Lyric content preview (%d bytes): %s", lyric_content.length(), preview.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to download lyrics or lyrics are empty");
        return false;
    }
    
    ESP_LOGI(TAG, "Lyrics downloaded successfully, size: %d bytes", lyric_content.length());
    return ParseLyrics(lyric_content);
}

// Φºúµ₧Éµ¡îΦ»ì
bool Esp32Music::ParseLyrics(const std::string& lyric_content) {
    ESP_LOGI(TAG, "Parsing lyrics content");
    
    // Σ╜┐τö¿ΘöüΣ┐¥µèñlyrics_µò░τ╗äΦ«┐Θù«
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    lyrics_.clear();
    
    // µîëΦíîσêåσë▓µ¡îΦ»ìσåàσ«╣
    std::istringstream stream(lyric_content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // σÄ╗ΘÖñΦíîσ░╛τÜäσ¢₧Φ╜ªτ¼ª
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Φ╖│Φ┐çτ⌐║Φíî
        if (line.empty()) {
            continue;
        }
        
        // Φºúµ₧ÉLRCµá╝σ╝Å: [mm:ss.xx]µ¡îΦ»ìµûçµ£¼
        if (line.length() > 10 && line[0] == '[') {
            size_t close_bracket = line.find(']');
            if (close_bracket != std::string::npos) {
                std::string tag_or_time = line.substr(1, close_bracket - 1);
                std::string content = line.substr(close_bracket + 1);
                
                // µúÇµƒÑµÿ»σÉªµÿ»σàâµò░µì«µáçτ¡╛ΦÇîΣ╕ìµÿ»µù╢Θù┤µê│
                // σàâµò░µì«µáçτ¡╛ΘÇÜσ╕╕µÿ» [ti:µáçΘóÿ], [ar:Φë║µ£»σ«╢], [al:Σ╕ôΦ╛æ] τ¡ë
                size_t colon_pos = tag_or_time.find(':');
                if (colon_pos != std::string::npos) {
                    std::string left_part = tag_or_time.substr(0, colon_pos);
                    
                    // µúÇµƒÑσåÆσÅ╖σ╖ªΦ╛╣µÿ»σÉªµÿ»µù╢Θù┤∩╝êµò░σ¡ù∩╝ë
                    bool is_time_format = true;
                    for (char c : left_part) {
                        if (!isdigit(c)) {
                            is_time_format = false;
                            break;
                        }
                    }
                    
                    // σªéµ₧£Σ╕ìµÿ»µù╢Θù┤µá╝σ╝Å∩╝îΦ╖│Φ┐çΦ┐ÖΣ╕ÇΦíî∩╝êσàâµò░µì«µáçτ¡╛∩╝ë
                    if (!is_time_format) {
                        // σÅ»Σ╗Ñσ£¿Φ┐ÖΘçîσñäτÉåσàâµò░µì«∩╝îΣ╛ïσªéµÅÉσÅûµáçΘóÿπÇüΦë║µ£»σ«╢τ¡ëΣ┐íµü»
                        ESP_LOGD(TAG, "Skipping metadata tag: [%s]", tag_or_time.c_str());
                        continue;
                    }
                    
                    // µÿ»µù╢Θù┤µá╝σ╝Å∩╝îΦºúµ₧Éµù╢Θù┤µê│
                    try {
                        int minutes = std::stoi(tag_or_time.substr(0, colon_pos));
                        float seconds = std::stof(tag_or_time.substr(colon_pos + 1));
                        int timestamp_ms = minutes * 60 * 1000 + (int)(seconds * 1000);
                        
                        // σ«ëσà¿σñäτÉåµ¡îΦ»ìµûçµ£¼∩╝îτí«Σ┐¥UTF-8τ╝ûτáüµ¡úτí«
                        std::string safe_lyric_text;
                        if (!content.empty()) {
                            // σê¢σ╗║σ«ëσà¿σë»µ£¼σ╣╢Θ¬îΦ»üσ¡ùτ¼ªΣ╕▓
                            safe_lyric_text = content;
                            // τí«Σ┐¥σ¡ùτ¼ªΣ╕▓Σ╗Ñnullτ╗ôσ░╛
                            safe_lyric_text.shrink_to_fit();
                        }
                        
                        lyrics_.push_back(std::make_pair(timestamp_ms, safe_lyric_text));
                        
                        if (!safe_lyric_text.empty()) {
                            // ΘÖÉσê╢µùÑσ┐ùΦ╛ôσç║Θò┐σ║ª∩╝îΘü┐σàìΣ╕¡µûçσ¡ùτ¼ªµê¬µû¡Θù«Θóÿ
                            size_t log_len = std::min(safe_lyric_text.length(), size_t(50));
                            std::string log_text = safe_lyric_text.substr(0, log_len);
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] %s", timestamp_ms, log_text.c_str());
                        } else {
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] (empty)", timestamp_ms);
                        }
                    } catch (const std::exception& e) {
                        ESP_LOGW(TAG, "Failed to parse time: %s", tag_or_time.c_str());
                    }
                }
            }
        }
    }
    
    // µîëµù╢Θù┤µê│µÄÆσ║Å
    std::sort(lyrics_.begin(), lyrics_.end());
    
    ESP_LOGI(TAG, "Parsed %d lyric lines", lyrics_.size());
    return !lyrics_.empty();
}

// µ¡îΦ»ìµÿ╛τñ║τ║┐τ¿ï
void Esp32Music::LyricDisplayThread() {
    ESP_LOGI(TAG, "Lyric display thread started");
    
    if (!DownloadLyrics(current_lyric_url_)) {
        ESP_LOGE(TAG, "Failed to download or parse lyrics");
        is_lyric_running_ = false;
        return;
    }
    
    // σ«Üµ£ƒµúÇµƒÑµÿ»σÉªΘ£ÇΦªüµ¢┤µû░µÿ╛τñ║(ΘóæτÄçσÅ»Σ╗ÑΘÖìΣ╜Ä)
    while (is_lyric_running_ && is_playing_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    ESP_LOGI(TAG, "Lyric display thread finished");
}

void Esp32Music::UpdateLyricDisplay(int64_t current_time_ms) {
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    if (lyrics_.empty()) {
        return;
    }
    
    // µƒÑµë╛σ╜ôσëìσ║öΦ»Ñµÿ╛τñ║τÜäµ¡îΦ»ì
    int new_lyric_index = -1;
    
    // Σ╗Äσ╜ôσëìµ¡îΦ»ìτ┤óσ╝òσ╝ÇσºïµƒÑµë╛∩╝îµÅÉΘ½ÿµòêτÄç
    int start_index = (current_lyric_index_.load() >= 0) ? current_lyric_index_.load() : 0;
    
    // µ¡úσÉæµƒÑµë╛∩╝Üµë╛σê░µ£ÇσÉÄΣ╕ÇΣ╕¬µù╢Θù┤µê│σ░ÅΣ║Äτ¡ëΣ║Äσ╜ôσëìµù╢Θù┤τÜäµ¡îΦ»ì
    for (int i = start_index; i < (int)lyrics_.size(); i++) {
        if (lyrics_[i].first <= current_time_ms) {
            new_lyric_index = i;
        } else {
            break;  // µù╢Θù┤µê│σ╖▓Φ╢àΦ┐çσ╜ôσëìµù╢Θù┤
        }
    }
    
    // σªéµ₧£µ▓íµ£ëµë╛σê░(σÅ»Φâ╜σ╜ôσëìµù╢Θù┤µ»öτ¼¼Σ╕ÇσÅÑµ¡îΦ»ìΦ┐ÿµù⌐)∩╝îµÿ╛τñ║τ⌐║
    if (new_lyric_index == -1) {
        new_lyric_index = -1;
    }
    
    // σªéµ₧£µ¡îΦ»ìτ┤óσ╝òσÅæτöƒσÅÿσîû∩╝îµ¢┤µû░µÿ╛τñ║
    if (new_lyric_index != current_lyric_index_) {
        current_lyric_index_ = new_lyric_index;
        
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            std::string lyric_text;
            
            if (current_lyric_index_ >= 0 && current_lyric_index_ < (int)lyrics_.size()) {
                lyric_text = lyrics_[current_lyric_index_].second;
            }
            
            // µÿ╛τñ║µ¡îΦ»ì
            display->SetChatMessage("lyric", lyric_text.c_str());
            
            ESP_LOGD(TAG, "Lyric update at %lldms: %s", 
                    current_time_ms, 
                    lyric_text.empty() ? "(no lyric)" : lyric_text.c_str());
        }
    }
}

// σêáΘÖñσñìµ¥éτÜäΦ«ñΦ»üσê¥σºïσîûµû╣µ│ò∩╝îΣ╜┐τö¿τ«ÇσìòτÜäΘ¥ÖµÇüσç╜µò░

// σêáΘÖñσñìµ¥éτÜäτ▒╗µû╣µ│ò∩╝îΣ╜┐τö¿τ«ÇσìòτÜäΘ¥ÖµÇüσç╜µò░

/**
 * @brief µ╖╗σèáΦ«ñΦ»üσñ┤σê░HTTPΦ»╖µ▒é
 * @param http_client HTTPσ«óµê╖τ½»µîçΘÆê
 * 
 * µ╖╗σèáτÜäΦ«ñΦ»üσñ┤σîàµï¼∩╝Ü
 * - X-MAC-Address: Φ«╛σñçMACσ£░σ¥Ç
 * - X-Chip-ID: Φ«╛σñçΦè»τëçID
 * - X-Timestamp: σ╜ôσëìµù╢Θù┤µê│
 * - X-Dynamic-Key: σè¿µÇüτöƒµêÉτÜäσ»åΘÆÑ
 */
// σêáΘÖñσñìµ¥éτÜäAddAuthHeadersµû╣µ│ò∩╝îΣ╜┐τö¿τ«ÇσìòτÜäΘ¥ÖµÇüσç╜µò░

// σêáΘÖñσñìµ¥éτÜäΦ«ñΦ»üΘ¬îΦ»üσÆîΘàìτ╜«µû╣µ│ò∩╝îΣ╜┐τö¿τ«ÇσìòτÜäΘ¥ÖµÇüσç╜µò░

// µÿ╛τñ║µ¿íσ╝ÅµÄºσê╢µû╣µ│òσ«₧τÄ░
void Esp32Music::SetDisplayMode(DisplayMode mode) {
    DisplayMode old_mode = display_mode_.load();
    display_mode_ = mode;
    
    ESP_LOGI(TAG, "Display mode changed from %s to %s", 
            (old_mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "LYRICS",
            (mode == DISPLAY_MODE_SPECTRUM) ? "SPECTRUM" : "LYRICS");
}
