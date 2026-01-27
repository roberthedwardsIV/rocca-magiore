#include "RadioStream.hpp"
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// -- LibCurl Helper for API --
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

RadioStream::RadioStream(std::string name, std::string country, std::string tag)
    : station_name(name), country_code(country), tag(tag) {
    
    // Connect to Redis
    redis_ctx = redisConnect("corpus_callosum", 6379);
    
    // Initialize Whisper
    // Ensure the model path matches what we put in Dockerfile
    ctx = whisper_init_from_file("/app/models/ggml-small.en.bin");
    if (!ctx) {
        std::cerr << "[CRITICAL] Failed to load Whisper model!" << std::endl;
    }
    
    // Configure Whisper (Standard English Translation)
    wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_timestamps = false;
    wparams.language = "en"; 
}

RadioStream::~RadioStream() {
    stop();
    if (redis_ctx) redisFree(redis_ctx);
    if (ctx) whisper_free(ctx);
}

// -- 1. DISCOVERY: Find a working URL from Radio-Browser --
std::string RadioStream::resolve_stream_url() {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string readBuffer;
    // Search for highest clicked station matching tag/country
    std::string query = "https://de1.api.radio-browser.info/json/stations/search?limit=1&order=clickcount&reverse=true&countrycode=" + country_code + "&tag=" + tag;
    
    curl_easy_setopt(curl, CURLOPT_URL, query.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    try {
        auto j = json::parse(readBuffer);
        if (!j.empty() && j[0].contains("url_resolved")) {
            std::string url = j[0]["url_resolved"];
            std::cout << "[RADIO] Resolved " << station_name << " to: " << url << std::endl;
            return url;
        }
    } catch (...) {}
    
    return "";
}

void RadioStream::start() {
    running = true;
    worker_thread = std::thread(&RadioStream::stream_loop, this);
    worker_thread.detach();
}

void RadioStream::stop() {
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

// -- 2. CONSUMPTION: The Libav Loop --
void RadioStream::stream_loop() {
    resolved_url = resolve_stream_url();
    if (resolved_url.empty()) {
        std::cerr << "[RADIO ERR] Could not resolve URL for " << station_name << std::endl;
        running = false;
        return;
    }

    // FFmpeg Init
    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (avformat_open_input(&fmt_ctx, resolved_url.c_str(), NULL, NULL) < 0) {
        std::cerr << "[RADIO ERR] Failed to open stream: " << resolved_url << std::endl;
        return;
    }
    
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) return;

    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = i;
            break;
        }
    }
    if (audio_stream_idx == -1) return;

    AVCodecParameters* codecpar = fmt_ctx->streams[audio_stream_idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codecpar);
    avcodec_open2(codec_ctx, codec, NULL);

    // Resampler: Whisper expects 16kHz, 1 channel, Float32
    SwrContext* swr_ctx = swr_alloc();
    av_opt_set_int(swr_ctx, "in_channel_layout", codecpar->ch_layout.u.mask, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", codecpar->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", codec_ctx->sample_fmt, 0);
    
    av_opt_set_int(swr_ctx, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", 16000, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
    swr_init(swr_ctx);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    
    std::vector<float> pcm_buffer;
    // Buffer 30 seconds of audio (16000 samples/sec * 30)
    const size_t CHUNK_SIZE = 16000 * 30; 

    while (running) {
        if (av_read_frame(fmt_ctx, packet) < 0) {
            // Reconnect logic would go here
            break;
        }

        if (packet->stream_index == audio_stream_idx) {
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    // Resample to 16k Float Mono
                    float* output_buffer;
                    av_samples_alloc((uint8_t**)&output_buffer, NULL, 1, frame->nb_samples, AV_SAMPLE_FMT_FLT, 0);
                    
                    int out_samples = swr_convert(swr_ctx, (uint8_t**)&output_buffer, frame->nb_samples, 
                                                (const uint8_t**)frame->data, frame->nb_samples);

                    // Append to main buffer
                    pcm_buffer.insert(pcm_buffer.end(), output_buffer, output_buffer + out_samples);
                    av_freep(&output_buffer);

                    // -- 3. TRANSCRIPTION TRIGGER --
                    if (pcm_buffer.size() >= CHUNK_SIZE) {
                        transcribe_segment(pcm_buffer);
                        pcm_buffer.clear(); // Flush buffer after processing
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
}

// -- 4. TRANSCRIPTION: Whisper.cpp --
void RadioStream::transcribe_segment(const std::vector<float>& pcm_data) {
    if (whisper_full(ctx, wparams, pcm_data.data(), pcm_data.size()) != 0) {
        std::cerr << "[WHISPER] failed to process audio" << std::endl;
        return;
    }

    const int n_segments = whisper_full_n_segments(ctx);
    std::string full_text = "";
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(ctx, i);
        full_text += std::string(text);
    }

    if (!full_text.empty() && redis_ctx) {
        std::cout << "[RADIO RX] " << station_name << ": " << full_text << std::endl;
        
        // Push as JSON so existing python brains can parse it easily
        json j;
        j["source"] = "radio";
        j["station"] = station_name;
        j["text"] = full_text;
        
        // Publish to the same channel the Youtube bot used, or a new one
        redisCommand(redis_ctx, "PUBLISH raw:news:%s %s", station_name.c_str(), j.dump().c_str());
    }
}