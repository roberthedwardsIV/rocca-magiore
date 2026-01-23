#include "YtStream.hpp"
#include <regex>
#include <iostream>

namespace bp = boost::process;

YtStream::YtStream(const std::string& id, const std::string& url) 
    : channel_id(id), stream_url(url) {
        
        redis_ctx = redisConnect("corpus_callosum", 6379);
        if (redis_ctx == nullptr || redis_ctx->err) {
        std::cerr << "Redis Connection Error [" << channel_id << "]: " 
                  << (redis_ctx ? redis_ctx->errstr : "Allocation failed") << std::endl;
    }
    }


YtStream::~YtStream() {
    stop();
    if (redis_ctx) redisFree(redis_ctx);
}

void YtStream::process_loop() {
    try {
        bp::ipstream pipe_stream;
        bp::child c("yt-dlp", "--skip-download", "--write-auto-subs", "--stdout", 
                   "--sub-format", "ttml", stream_url, bp::std_out > pipe_stream);

        std::string line;
        while (running && std::getline(pipe_stream, line)) {
            if (line.empty()) continue;

            std::string clean_text = clean_and_dedup(line);
            if (!clean_text.empty() && redis_ctx && !redis_ctx->err) {
                // Publish to Redis: channel format "raw:news:CHANNEL_ID"
                std::string redis_channel = "raw:news:" + channel_id;
                freeReplyObject(redisCommand(redis_ctx, "PUBLISH %s %s", 
                               redis_channel.c_str(), clean_text.c_str()));
            }
        }
        if (c.running()) c.terminate();
    } catch (const std::exception& e) {
        running = false;
    }
}

std::string YtStream::clean_and_dedup(const std::string& raw) {
    // 1. Strip XML/TTML tags (basic regex for speed)
    std::string text = std::regex_replace(raw, std::regex("<[^>]*>"), "");
    
    // 2. Remove leading/trailing whitespace
    text = std::regex_replace(text, std::regex("^\\s+|\\s+$"), "");

    if (text.empty()) return "";

    std::lock_guard<std::mutex> lock(data_mutex);
    
    // 3. Deduplication Logic
    // If the new line starts with our previous text, only return the new suffix.
    if (!last_sent_text.empty() && text.find(last_sent_text) == 0) {
        std::string diff = text.substr(last_sent_text.length());
        last_sent_text = text;
        return diff;
    }

    last_sent_text = text;
    return text;
}