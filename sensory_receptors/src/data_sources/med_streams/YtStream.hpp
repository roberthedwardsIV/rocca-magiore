#ifndef YT_STREAM_HPP
#define YT_STREAM_HPP

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <boost/process.hpp>

class YtStream {
public:
    YtStream(const std::string& id, const std::string& url);
    ~YtStream();

    void start();  
    void stop();   
    bool is_active() const;

private:
    void process_loop();
    std::string clean_and_dedup(const std::string& raw_line);

    std::string channel_id;
    std::string stream_url;
    std::string last_sent_text;
    
    redisContext* redis_ctx = nullptr;
    std::atomic<bool> running{false};
    std::thread worker_thread;
    mutable std::mutex data_mutex; 
};

#endif