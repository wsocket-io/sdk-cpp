/**
 * wSocket C++ SDK — Realtime Pub/Sub client with Presence, History, and Push.
 *
 * @file wsocket.hpp
 * @brief Public API header for the wSocket C++ SDK.
 *
 * Usage:
 *   wsocket::Client client("ws://localhost:9001", "your-api-key");
 *   client.connect();
 *   auto ch = client.pubsub().channel("chat");
 *   ch->subscribe([](auto& data, auto& meta) { std::cout << data; });
 *   ch->publish({{"text", "hello"}});
 */

#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

namespace wsocket {

using json = nlohmann::json;

// ─── Types ──────────────────────────────────────────────────

struct MessageMeta {
    std::string id;
    std::string channel;
    int64_t timestamp = 0;
};

struct PresenceMember {
    std::string client_id;
    json data;
    int64_t joined_at = 0;
};

struct HistoryMessage {
    std::string id;
    std::string channel;
    json data;
    std::string publisher_id;
    int64_t timestamp = 0;
    int64_t sequence = 0;
};

struct HistoryResult {
    std::string channel;
    std::vector<HistoryMessage> messages;
    bool has_more = false;
};

struct Options {
    bool auto_reconnect = true;
    int max_reconnect_attempts = 10;
    int reconnect_delay_ms = 1000;
    std::string token;
    bool recover = true;
};

// ─── Callback types ─────────────────────────────────────────

using MessageCallback = std::function<void(const json&, const MessageMeta&)>;
using HistoryCallback = std::function<void(const HistoryResult&)>;
using PresenceCallback = std::function<void(const PresenceMember&)>;
using MembersCallback = std::function<void(const std::vector<PresenceMember>&)>;
using ConnectCallback = std::function<void()>;
using DisconnectCallback = std::function<void(int code)>;
using ErrorCallback = std::function<void(const std::string&)>;

// ─── Forward declarations ───────────────────────────────────

class Client;

// ─── Presence ───────────────────────────────────────────────

class Presence {
public:
    Presence(const std::string& channel, std::function<void(const json&)> send_fn);

    Presence& enter(const json& data = nullptr);
    Presence& leave();
    Presence& update(const json& data);
    Presence& get();

    Presence& on_enter(PresenceCallback cb);
    Presence& on_leave(PresenceCallback cb);
    Presence& on_update(PresenceCallback cb);
    Presence& on_members(MembersCallback cb);

    void handle_event(const std::string& action, const json& data);

private:
    std::string channel_name_;
    std::function<void(const json&)> send_;
    std::mutex mu_;
    std::vector<PresenceCallback> enter_cbs_, leave_cbs_, update_cbs_;
    std::vector<MembersCallback> members_cbs_;

    PresenceMember parse_member(const json& d) const;
};

// ─── Channel ────────────────────────────────────────────────

class Channel {
public:
    Channel(const std::string& name, std::function<void(const json&)> send_fn);

    Channel& subscribe(MessageCallback cb = nullptr);
    Channel& unsubscribe();
    Channel& publish(const json& data, bool persist = false);
    Channel& history(int limit = 0, int64_t before = 0, int64_t after = 0,
                     const std::string& direction = "");
    Channel& on_history(HistoryCallback cb);

    Presence& presence();

    void handle_message(const json& data, const MessageMeta& meta);
    void handle_history(const HistoryResult& result);

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::function<void(const json&)> send_;
    std::mutex mu_;
    std::vector<MessageCallback> message_cbs_;
    std::vector<HistoryCallback> history_cbs_;
    Presence presence_;
};

// ─── PubSub ─────────────────────────────────────────────────

class PubSub {
public:
    explicit PubSub(Client& client);
    std::shared_ptr<Channel> channel(const std::string& name);
private:
    Client& client_;
};

// ─── Push Client ────────────────────────────────────────────

class PushClient {
public:
    PushClient(const std::string& base_url, const std::string& token, const std::string& app_id);

    int register_fcm(const std::string& device_token, const std::string& member_id);
    int register_apns(const std::string& device_token, const std::string& member_id);
    int send_to_member(const std::string& member_id, const json& payload);
    int broadcast(const json& payload);
    int unregister(const std::string& member_id, const std::string& platform = "");

private:
    std::string base_url_;
    std::string token_;
    std::string app_id_;

    int post(const std::string& path, const json& body);
};

// ─── Client ─────────────────────────────────────────────────

class Client {
public:
    Client(const std::string& url, const std::string& api_key, Options options = {});
    ~Client();

    Client& connect();
    void disconnect();
    void run();   // Blocking event loop
    bool is_connected() const;

    std::shared_ptr<Channel> channel(const std::string& name);
    PubSub& pubsub();

    PushClient configure_push(const std::string& base_url, const std::string& token,
                              const std::string& app_id);

    Client& on_connect(ConnectCallback cb);
    Client& on_disconnect(DisconnectCallback cb);
    Client& on_error(ErrorCallback cb);

private:
    std::string url_;
    std::string api_key_;
    Options options_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    int reconnect_attempts_ = 0;
    int64_t last_message_ts_ = 0;

    std::map<std::string, std::shared_ptr<Channel>> channels_;
    std::set<std::string> subscribed_;
    std::mutex mu_;

    std::vector<ConnectCallback> connect_cbs_;
    std::vector<DisconnectCallback> disconnect_cbs_;
    std::vector<ErrorCallback> error_cbs_;

    PubSub pubsub_;
    void* ws_handle_ = nullptr;

    void send(const json& msg);
    void handle_message(const std::string& raw);
    void maybe_reconnect();

    friend class PubSub;
};

} // namespace wsocket
