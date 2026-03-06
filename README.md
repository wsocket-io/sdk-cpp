# wSocket SDK for C++

Official C++ SDK for [wSocket](https://wsocket.io) — realtime pub/sub, presence, history, and push notifications.

[![GitHub Release](https://img.shields.io/github/v/release/wsocket-io/sdk-cpp)](https://github.com/wsocket-io/sdk-cpp/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Installation

### CMake (FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(wsocket_io
    GIT_REPOSITORY https://github.com/wsocket-io/sdk-cpp.git
    GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(wsocket_io)
target_link_libraries(your_app wsocket_io)
```

### Manual Build

```bash
cd sdks/cpp
mkdir build && cd build
cmake ..
make
sudo make install
```

## Dependencies

- [websocketpp](https://github.com/zaphoyd/websocketpp) or [Boost.Beast](https://www.boost.org/doc/libs/release/libs/beast/)
- [nlohmann/json](https://github.com/nlohmann/json)
- [libcurl](https://curl.se/libcurl/) (for Push)

On Ubuntu/Debian:

```bash
sudo apt install libwebsocketpp-dev nlohmann-json3-dev libcurl4-openssl-dev libboost-all-dev
```

## Quick Start

```cpp
#include <wsocket/wsocket.hpp>

int main() {
    wsocket::Client client("wss://node00.wsocket.online", "your-api-key");

    client.on_connect([] {
        std::cout << "Connected!" << std::endl;
    });

    client.connect();

    auto channel = client.pubsub().channel("chat");

    channel->subscribe([](const nlohmann::json& data, const wsocket::MessageMeta& meta) {
        std::cout << "Received: " << data.dump() << std::endl;
    });

    channel->publish({{"text", "Hello from C++!"}});

    client.run(); // blocks
    return 0;
}
```

## Presence

```cpp
auto ch = client.pubsub().channel("room");

ch->presence().enter({{"name", "Alice"}});

ch->presence().on_enter([](const wsocket::PresenceMember& m) {
    std::cout << m.client_id << " entered" << std::endl;
});

ch->presence().on_leave([](const wsocket::PresenceMember& m) {
    std::cout << m.client_id << " left" << std::endl;
});

ch->presence().get();
ch->presence().on_members([](const std::vector<wsocket::PresenceMember>& members) {
    std::cout << "Online: " << members.size() << std::endl;
});
```

## History

```cpp
ch->history(50);
ch->on_history([](const wsocket::HistoryResult& result) {
    for (const auto& msg : result.messages) {
        std::cout << msg.publisher_id << ": " << msg.data.dump() << std::endl;
    }
});
```

## Push Notifications

```cpp
wsocket::PushClient push("https://node00.wsocket.online", "secret", "app1");

push.register_fcm("device-token", "user-123");
push.send_to_member("user-123", {{"title", "Hello"}, {"body", "World"}});
push.broadcast({{"title", "News"}, {"body", "Update available"}});
```

## Requirements

- C++17 or later
- Boost 1.70+ (for Beast) or websocketpp
- nlohmann/json 3.0+
- libcurl 7.0+

## License

MIT
