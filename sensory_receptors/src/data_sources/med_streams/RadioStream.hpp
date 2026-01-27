#ifndef RADIO_STREAM_HPP
#define RADIO_STREAM_HPP

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <hiredis/hiredis.h>
#include <whisper.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

class RadioStream {
public:
    RadioStream(std::string name, std::string country_code, std::string tag);
    ~RadioStream();

    void start();
    void stop();
    bool is_active() const { return running; }

private:
    void stream_loop();
    std::string resolve_stream_url(); // Queries Radio-Browser API
    void transcribe_segment(const std::vector<float>& pcm_data);

    // Config
    std::string station_name;
    std::string country_code;
    std::string tag;
    std::string resolved_url;

    // State
    std::atomic<bool> running{false};
    std::thread worker_thread;
    redisContext* redis_ctx;

    // Whisper Context
    struct whisper_context* ctx;
    struct whisper_full_params wparams;
};

#endif