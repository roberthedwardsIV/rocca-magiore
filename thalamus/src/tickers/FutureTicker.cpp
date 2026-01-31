#include "FutureTicker.hpp"

FutureTicker::FutureTicker(std::string sym, std::string exch, double mult) {
    this->symbol = sym;
    this->instrument_type = "future";
    this->exchange = exch;
    this->current_state.multiplier = mult;
    this->current_state.price = 0.0;
}

void FutureTicker::process_quote(const json& quote) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    current_state.price = quote.value("price", current_state.price);
    current_state.expiry_ts = quote.value("expiry", 0LL);
}

json FutureTicker::get_json_state() const {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    return {
        {"symbol", symbol},
        {"price", current_state.price},
        {"notional", current_state.price * current_state.multiplier},
        {"type", "future"}
    };
}