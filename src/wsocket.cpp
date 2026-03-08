/**
 * wSocket C++ SDK — Implementation
 *
 * @file wsocket.cpp
 */

#include <wsocket/wsocket.hpp>
#include <iostream>
#include <sstream>
#include <cstring>
#include <curl/curl.h>

namespace wsocket {

// ─── Utilities ──────────────────────────────────────────────

static std::string generate_uuid() {
    static const char hex[] = "0123456789abcdef";
    std::string uuid(36, '-');
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        uuid[i] = hex[rand() % 16];
    }
    uuid[14] = '4'; // version
    return uuid;
}

// ─── Presence ───────────────────────────────────────────────

Presence::Presence(const std::string& channel, std::function<void(const json&)> send_fn)
    : channel_name_(channel), send_(std::move(send_fn)) {}

Presence& Presence::enter(const json& data) {
    json msg = {{"action", "presence.enter"}, {"channel", channel_name_}};
    if (!data.is_null()) msg["data"] = data;
    send_(msg);
    return *this;
}

Presence& Presence::leave() {
    send_({{"action", "presence.leave"}, {"channel", channel_name_}});
    return *this;
}

Presence& Presence::update(const json& data) {
    send_({{"action", "presence.update"}, {"channel", channel_name_}, {"data", data}});
    return *this;
}

Presence& Presence::get() {
    send_({{"action", "presence.get"}, {"channel", channel_name_}});
    return *this;
}

Presence& Presence::on_enter(PresenceCallback cb) {
    std::lock_guard<std::mutex> lock(mu_);
    enter_cbs_.push_back(std::move(cb));
    return *this;
}

Presence& Presence::on_leave(PresenceCallback cb) {
    std::lock_guard<std::mutex> lock(mu_);
    leave_cbs_.push_back(std::move(cb));
    return *this;
}

Presence& Presence::on_update(PresenceCallback cb) {
    std::lock_guard<std::mutex> lock(mu_);
    update_cbs_.push_back(std::move(cb));
    return *this;
}

Presence& Presence::on_members(MembersCallback cb) {
    std::lock_guard<std::mutex> lock(mu_);
    members_cbs_.push_back(std::move(cb));
    return *this;
}

void Presence::handle_event(const std::string& action, const json& data) {
    std::lock_guard<std::mutex> lock(mu_);
    if (action == "presence.enter") {
        auto m = parse_member(data);
        for (auto& cb : enter_cbs_) cb(m);
    } else if (action == "presence.leave") {
        auto m = parse_member(data);
        for (auto& cb : leave_cbs_) cb(m);
    } else if (action == "presence.update") {
        auto m = parse_member(data);
        for (auto& cb : update_cbs_) cb(m);
    } else if (action == "presence.members") {
        std::vector<PresenceMember> members;
        if (data.contains("members") && data["members"].is_array()) {
            for (auto& d : data["members"]) {
                members.push_back(parse_member(d));
            }
        }
        for (auto& cb : members_cbs_) cb(members);
    }
}

PresenceMember Presence::parse_member(const json& d) const {
    return {
        d.value("clientId", ""),
        d.value("data", json(nullptr)),
        d.value("joinedAt", int64_t(0))
    };
}

// ─── Channel ────────────────────────────────────────────────

Channel::Channel(const std::string& name, std::function<void(const json&)> send_fn)
    : name_(name), send_(send_fn), presence_(name, send_fn) {}

Channel& Channel::subscribe(MessageCallback cb) {
    if (cb) {
        std::lock_guard<std::mutex> lock(mu_);
        message_cbs_.push_back(std::move(cb));
    }
    send_({{"action", "subscribe"}, {"channel", name_}});
    return *this;
}

Channel& Channel::unsubscribe() {
    send_({{"action", "unsubscribe"}, {"channel", name_}});
    std::lock_guard<std::mutex> lock(mu_);
    message_cbs_.clear();
    return *this;
}

Channel& Channel::publish(const json& data, bool persist) {
    json msg = {
        {"action", "publish"},
        {"channel", name_},
        {"data", data},
        {"id", generate_uuid()}
    };
    if (persist) msg["persist"] = true;
    send_(msg);
    return *this;
}

Channel& Channel::history(int limit, int64_t before, int64_t after,
                           const std::string& direction) {
    json msg = {{"action", "history"}, {"channel", name_}};
    if (limit > 0) msg["limit"] = limit;
    if (before > 0) msg["before"] = before;
    if (after > 0) msg["after"] = after;
    if (!direction.empty()) msg["direction"] = direction;
    send_(msg);
    return *this;
}

Channel& Channel::on_history(HistoryCallback cb) {
    std::lock_guard<std::mutex> lock(mu_);
    history_cbs_.push_back(std::move(cb));
    return *this;
}

Presence& Channel::presence() { return presence_; }

void Channel::handle_message(const json& data, const MessageMeta& meta) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& cb : message_cbs_) cb(data, meta);
}

void Channel::handle_history(const HistoryResult& result) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& cb : history_cbs_) cb(result);
}

// ─── PubSub ─────────────────────────────────────────────────

PubSub::PubSub(Client& client) : client_(client) {}

std::shared_ptr<Channel> PubSub::channel(const std::string& name) {
    return client_.channel(name);
}

// ─── Push Client ────────────────────────────────────────────

PushClient::PushClient(const std::string& base_url, const std::string& token,
                       const std::string& app_id)
    : base_url_(base_url), token_(token), app_id_(app_id) {}

int PushClient::post(const std::string& path, const json& body) {
    return request("POST", base_url_ + "/api/push/" + path, body);
}

int PushClient::request(const std::string& method, const std::string& url,
                        const json& body) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    std::string body_str = body.is_null() ? "" : body.dump();
    std::string auth = "Bearer " + token_;
    std::string response_data;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: " + auth).c_str());
    headers = curl_slist_append(headers, ("X-App-Id: " + app_id_).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());

    if (!body_str.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
        +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            auto* s = static_cast<std::string*>(userp);
            s->append(static_cast<char*>(contents), size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? 0 : -1;
}

std::string PushClient::request_with_response(const std::string& method,
                                               const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "{}";

    std::string auth = "Bearer " + token_;
    std::string response_data;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: " + auth).c_str());
    headers = curl_slist_append(headers, ("X-App-Id: " + app_id_).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
        +[](void* contents, size_t size, size_t nmemb, void* userp) -> size_t {
            auto* s = static_cast<std::string*>(userp);
            s->append(static_cast<char*>(contents), size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? response_data : "{}";
}

int PushClient::register_fcm(const std::string& device_token, const std::string& member_id) {
    return post("register", {
        {"memberId", member_id}, {"platform", "fcm"},
        {"subscription", {{"deviceToken", device_token}}}
    });
}

int PushClient::register_apns(const std::string& device_token, const std::string& member_id) {
    return post("register", {
        {"memberId", member_id}, {"platform", "apns"},
        {"subscription", {{"deviceToken", device_token}}}
    });
}

int PushClient::send_to_member(const std::string& member_id, const json& payload) {
    return post("send", {{"memberId", member_id}, {"payload", payload}});
}

int PushClient::broadcast(const json& payload) {
    return post("broadcast", {{"payload", payload}});
}

int PushClient::unregister(const std::string& member_id, const std::string& platform) {
    json body = {{"memberId", member_id}};
    if (!platform.empty()) body["platform"] = platform;
    return post("unregister", body);
}

int PushClient::delete_subscription(const std::string& subscription_id) {
    std::string url = base_url_ + "/api/push/subscriptions/" + subscription_id;
    return request("DELETE", url);
}

int PushClient::add_channel(const std::string& subscription_id, const std::string& channel) {
    return post("channels/add", {{"subscriptionId", subscription_id}, {"channel", channel}});
}

int PushClient::remove_channel(const std::string& subscription_id, const std::string& channel) {
    return post("channels/remove", {{"subscriptionId", subscription_id}, {"channel", channel}});
}

std::string PushClient::get_vapid_key() {
    std::string url = base_url_ + "/api/push/vapid-key";
    return request_with_response("GET", url);
}

std::string PushClient::list_subscriptions(const std::string& member_id) {
    std::string url = base_url_ + "/api/push/subscriptions?memberId=" + member_id;
    return request_with_response("GET", url);
}

// ─── Client ─────────────────────────────────────────────────

Client::Client(const std::string& url, const std::string& api_key, Options options)
    : url_(url), api_key_(api_key), options_(std::move(options)), pubsub_(*this) {}

Client::~Client() {
    disconnect();
}

Client& Client::connect() {
    std::string ws_url = url_;
    ws_url += (url_.find('?') != std::string::npos) ? "&" : "?";
    ws_url += "key=" + api_key_;
    if (!options_.token.empty()) ws_url += "&token=" + options_.token;

    /*
     * WebSocket connection — implementation depends on chosen library.
     * With websocketpp:
     *   websocketpp::client<websocketpp::config::asio_tls_client> ws;
     *   ws.connect(ws_url);
     * With Boost.Beast:
     *   beast::websocket::stream<beast::ssl_stream<tcp::socket>> ws;
     *
     * This is a structural SDK — extend with your WebSocket library.
     */

    connected_ = true;
    reconnect_attempts_ = 0;

    // Re-subscribe
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& ch : subscribed_) {
            send({{"action", "subscribe"}, {"channel", ch}});
        }
    }

    for (auto& cb : connect_cbs_) cb();
    return *this;
}

void Client::disconnect() {
    connected_ = false;
    running_ = false;
}

void Client::run() {
    running_ = true;
    while (running_ && connected_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool Client::is_connected() const {
    return connected_;
}

std::shared_ptr<Channel> Client::channel(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = channels_.find(name);
    if (it != channels_.end()) return it->second;
    auto ch = std::make_shared<Channel>(name, [this](const json& msg) { send(msg); });
    channels_[name] = ch;
    return ch;
}

PubSub& Client::pubsub() { return pubsub_; }

PushClient Client::configure_push(const std::string& base_url, const std::string& token,
                                   const std::string& app_id) {
    return PushClient(base_url, token, app_id);
}

Client& Client::on_connect(ConnectCallback cb) {
    connect_cbs_.push_back(std::move(cb));
    return *this;
}

Client& Client::on_disconnect(DisconnectCallback cb) {
    disconnect_cbs_.push_back(std::move(cb));
    return *this;
}

Client& Client::on_error(ErrorCallback cb) {
    error_cbs_.push_back(std::move(cb));
    return *this;
}

void Client::send(const json& msg) {
    if (!connected_) return;
    /* Send via WebSocket — depends on library */
    (void)msg;
}

void Client::handle_message(const std::string& raw) {
    try {
        auto msg = json::parse(raw);
        auto action = msg.value("action", "");
        auto channel_name = msg.value("channel", "");

        if (action == "message") {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = channels_.find(channel_name);
            if (it == channels_.end()) return;
            auto ts = msg.value("timestamp", int64_t(0));
            if (ts > last_message_ts_) last_message_ts_ = ts;
            MessageMeta meta{msg.value("id", ""), channel_name, ts};
            it->second->handle_message(msg.value("data", json(nullptr)), meta);
        }
        else if (action == "subscribed") {
            std::lock_guard<std::mutex> lock(mu_);
            subscribed_.insert(channel_name);
        }
        else if (action == "unsubscribed") {
            std::lock_guard<std::mutex> lock(mu_);
            subscribed_.erase(channel_name);
        }
        else if (action == "history") {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = channels_.find(channel_name);
            if (it == channels_.end()) return;
            HistoryResult result;
            result.channel = channel_name;
            result.has_more = msg.value("hasMore", false);
            if (msg.contains("messages") && msg["messages"].is_array()) {
                for (auto& m : msg["messages"]) {
                    result.messages.push_back({
                        m.value("id", ""), channel_name,
                        m.value("data", json(nullptr)),
                        m.value("publisherId", ""),
                        m.value("timestamp", int64_t(0)),
                        m.value("sequence", int64_t(0))
                    });
                }
            }
            it->second->handle_history(result);
        }
        else if (action.substr(0, 9) == "presence.") {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = channels_.find(channel_name);
            if (it == channels_.end()) return;
            it->second->presence().handle_event(action, msg);
        }
        else if (action == "error") {
            auto err = msg.value("error", "Unknown error");
            for (auto& cb : error_cbs_) cb(err);
        }
    } catch (const std::exception& e) {
        for (auto& cb : error_cbs_) cb(e.what());
    }
}

void Client::maybe_reconnect() {
    if (!options_.auto_reconnect) return;
    if (reconnect_attempts_ >= options_.max_reconnect_attempts) return;
    reconnect_attempts_++;
    auto delay = std::chrono::milliseconds(options_.reconnect_delay_ms * reconnect_attempts_);
    std::thread([this, delay]() {
        std::this_thread::sleep_for(delay);
        if (!connected_) connect();
    }).detach();
}

} // namespace wsocket
