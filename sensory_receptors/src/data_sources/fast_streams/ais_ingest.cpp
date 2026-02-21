#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>
#include <libwebsockets.h>
#include <cstdlib> // For std::getenv

using json = nlohmann::json;

// --- CONFIGURATION ---
// 1. CRITICAL: Check for the Environment Variable
const std::string AIS_API_KEY = std::getenv("AIS_API_KEY") ? std::getenv("AIS_API_KEY") : "REMOVED";

const char* REDIS_HOST = "corpus_callosum";
const int REDIS_PORT = 6379;
const char* AIS_HOST = "stream.aisstream.io";
const int AIS_PORT = 443;
const char* AIS_PATH = "/v0/stream";

struct AISContext {
    redisContext* redis;
    struct lws* wsi;
    bool subscription_sent;
};

static int callback_ais(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
    AISContext* ctx = (AISContext*)lws_context_user(lws_get_context(wsi));

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            std::cout << "[MARITIME] Connection Established." << std::endl;
            ctx->subscription_sent = false;
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (!ctx->subscription_sent) {
                // 2. Debug: Print exactly what we are sending
                json sub = {
                    {"APIKey", AIS_API_KEY},
                    {"BoundingBoxes", {{{-90, -180}, {90, 180}}}}, 
                    {"FilterMessageTypes", {"PositionReport", "ShipStaticData", "StaticDataReport"}}
                };
                std::string msg = sub.dump();
                
                std::cout << "[MARITIME] Sending Subscription: " << msg << std::endl;

                unsigned char buf[LWS_PRE + 2048];
                unsigned char* p = &buf[LWS_PRE];
                size_t n = msg.length();
                memcpy(p, msg.c_str(), n);
                
                if (lws_write(wsi, p, n, LWS_WRITE_TEXT) < (ssize_t)n) {
                    std::cerr << "[MARITIME] Write failed." << std::endl;
                    return -1;
                }
                ctx->subscription_sent = true;
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // 3. Debug: Print what the server says (It might be an error message!)
            std::string raw_msg((char*)in, len);
            // std::cout << "[MARITIME] RX: " << raw_msg << std::endl;

            if (ctx->redis) {
                redisReply* reply = (redisReply*)redisCommand(ctx->redis, "LPUSH maritime_ais %s", raw_msg.c_str());
                if (reply) freeReplyObject(reply);
                redisReply* pub_reply = (redisReply*)redisCommand(ctx->redis, "PUBLISH maritime_ais %s", raw_msg.c_str());
                if (pub_reply) freeReplyObject(pub_reply);
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            std::cerr << "[MARITIME] Connect Error: " << (in ? (char*)in : "(null)") << std::endl;
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            std::cout << "[MARITIME] Connection Closed." << std::endl;
            if (ctx) ctx->wsi = NULL;
            break;

        default: break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    { "ais-protocol", callback_ais, 0, 4096 },
    { NULL, NULL, 0, 0 }
};

int main() {
    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

    // 4. Safety Check: Don't run if key is missing
    if (AIS_API_KEY == "YOUR_API_KEY") {
        std::cerr << "[MARITIME] CRITICAL: AIS_API_KEY is not set in Docker environment!" << std::endl;
        std::cerr << "[MARITIME] Sleeping indefinitely..." << std::endl;
        while(true) std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    std::cout << "[MARITIME] Starting Ingest. Key: " << AIS_API_KEY.substr(0, 5) << "..." << std::endl;

    AISContext ais_ctx;
    ais_ctx.redis = redisConnect(REDIS_HOST, REDIS_PORT);
    
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1; info.uid = -1;
    info.client_ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt";
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.user = &ais_ctx;

    struct lws_context* context = lws_create_context(&info);
    if (!context) return 1;

    while (true) {
        struct lws_client_connect_info ccinfo;
        memset(&ccinfo, 0, sizeof(ccinfo));
        ccinfo.context = context;
        ccinfo.address = AIS_HOST;
        ccinfo.port = AIS_PORT;
        ccinfo.path = AIS_PATH;
        ccinfo.host = ccinfo.address;
        ccinfo.origin = ccinfo.address;
        ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
        ccinfo.protocol = protocols[0].name;
        ccinfo.pwsi = &ais_ctx.wsi;

        lws_client_connect_via_info(&ccinfo);

        while (lws_service(context, 100) >= 0) {
            if (!ais_ctx.wsi) break;
        }
        
        std::cout << "[MARITIME] Reconnecting in 5s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    lws_context_destroy(context);
    return 0;
}