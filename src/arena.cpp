#include "arena.h"

#include "http.h"
#include "http_auth.h"
#include "json_util.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>

namespace stellaverse_arena {
namespace {

constexpr std::size_t kMaxChatLog = 100;
constexpr std::size_t kMaxFrameBytes = 64 * 1024; // server drops larger frames
constexpr auto kScoreMinInterval = std::chrono::milliseconds(100); // 10 Hz

// Client -> server message ids.
enum ClientMsg : std::uint8_t {
    kCtsSelectedChart = 1,
    kCtsScore = 2,
    kCtsChartCancelled = 3,
    kCtsReady = 4,
    kCtsChat = 6,
    kCtsMissingChart = 7,
    kCtsItem = 10,
};

// Server -> client message ids.
enum ServerMsg : std::uint8_t {
    kStcPlayersScore = 1,
    kStcPlayersReadyUpdate = 2,
    kStcSelectedChart = 3,
    kStcUserlist = 4,
    kStcWelcome = 5,
    kStcChat = 6,
    kStcMissingChart = 7,
    kStcItem = 8,
    kStcItemSettings = 9,
    kStcError = 10,
};

struct ParsedWsUrl {
    bool secure{};
    std::wstring host;
    std::string hostAscii;
    unsigned short port{};
    std::string target; // path + query (ASCII)
};

// One live WebSocket session. Handles are closed by Close(); the recv thread is
// joined there too, so Close() must never be called from the recv thread.
struct Connection {
    HINTERNET session{};
    HINTERNET connect{};
    HINTERNET ws{};
    std::thread recvThread;
    std::atomic<bool> stopping{false};
    std::atomic<bool> connected{false};
    std::mutex sendMutex;

    ~Connection() { Close(); }

    void Close() {
        stopping = true;
        connected = false;
        if (ws != nullptr) {
            // Best-effort close frame; skip if a send is stuck.
            if (sendMutex.try_lock()) {
                WinHttpWebSocketShutdown(ws, 1000, nullptr, 0);
                sendMutex.unlock();
            }
            // Closing the handle also aborts a blocked WinHttpWebSocketReceive.
            WinHttpCloseHandle(ws);
            ws = nullptr;
        }
        if (recvThread.joinable()) {
            recvThread.join();
        }
        if (connect != nullptr) {
            WinHttpCloseHandle(connect);
            connect = nullptr;
        }
        if (session != nullptr) {
            WinHttpCloseHandle(session);
            session = nullptr;
        }
    }
};

std::mutex g_lifecycleMutex; // serializes Join / Leave
std::mutex g_connPtrMutex;   // guards g_conn
std::shared_ptr<Connection> g_conn;

std::mutex g_stateMutex;
openlr2::ArenaRoomState g_state;

std::mutex g_scoreThrottleMutex;
std::chrono::steady_clock::time_point g_lastScoreSent{};

std::shared_ptr<Connection> CurrentConnection() {
    std::lock_guard lock(g_connPtrMutex);
    return g_conn;
}

std::string UrlEncode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '.' || c == '_' || c == '~';
        if (unreserved) {
            out += static_cast<char>(c);
        } else {
            out += std::format("%{:02X}", static_cast<unsigned>(c));
        }
    }
    return out;
}

bool QueryHasParam(std::string_view query, std::string_view name) {
    std::size_t pos = 0;
    while (pos <= query.size()) {
        std::size_t end = query.find('&', pos);
        if (end == std::string_view::npos) {
            end = query.size();
        }
        const std::string_view part = query.substr(pos, end - pos);
        if (part.size() > name.size() && part.substr(0, name.size()) == name && part[name.size()] == '=') {
            return true;
        }
        pos = end + 1;
    }
    return false;
}

// WinHttpCrackUrl does not understand ws/wss, so parse by hand.
bool ParseWsUrl(std::string_view url, std::string_view roomId, std::string_view token, ParsedWsUrl& out) {
    out = {};
    const std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return false;
    }
    const std::string_view scheme = url.substr(0, schemeEnd);
    if (scheme == "wss" || scheme == "https") {
        out.secure = true;
    } else if (scheme == "ws" || scheme == "http") {
        out.secure = false;
    } else {
        return false;
    }
    std::string_view rest = url.substr(schemeEnd + 3);
    if (const std::size_t frag = rest.find('#'); frag != std::string_view::npos) {
        rest = rest.substr(0, frag);
    }
    const std::size_t authorityEnd = rest.find_first_of("/?");
    const std::string_view authority = rest.substr(0, authorityEnd);
    std::string_view pathAndQuery = authorityEnd == std::string_view::npos ? std::string_view{} : rest.substr(authorityEnd);
    if (authority.empty()) {
        return false;
    }

    std::string_view hostPart = authority;
    unsigned long port = out.secure ? 443 : 80;
    if (!authority.empty() && authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        hostPart = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                return false;
            }
            port = std::strtoul(std::string(authority.substr(close + 2)).c_str(), nullptr, 10);
        }
    } else if (const std::size_t colon = authority.rfind(':'); colon != std::string_view::npos) {
        hostPart = authority.substr(0, colon);
        port = std::strtoul(std::string(authority.substr(colon + 1)).c_str(), nullptr, 10);
    }
    if (hostPart.empty() || port == 0 || port > 65535) {
        return false;
    }
    out.hostAscii.assign(hostPart.begin(), hostPart.end());
    out.host.assign(hostPart.begin(), hostPart.end());
    out.port = static_cast<unsigned short>(port);

    std::string path;
    std::string query;
    if (pathAndQuery.empty()) {
        path = "/";
    } else if (pathAndQuery.front() == '?') {
        path = "/";
        query = std::string(pathAndQuery.substr(1));
    } else {
        const std::size_t q = pathAndQuery.find('?');
        path = std::string(pathAndQuery.substr(0, q));
        if (q != std::string_view::npos) {
            query = std::string(pathAndQuery.substr(q + 1));
        }
    }

    // The server needs both roomId and token in the query.
    if (!QueryHasParam(query, "roomId") && !roomId.empty()) {
        if (!query.empty()) {
            query += '&';
        }
        query += "roomId=" + UrlEncode(roomId);
    }
    if (!QueryHasParam(query, "token")) {
        if (token.empty()) {
            return false;
        }
        if (!query.empty()) {
            query += '&';
        }
        query += "token=" + UrlEncode(token);
    }
    out.target = path + "?" + query;
    return true;
}

openlr2::ArenaPeerScore ParseScore(const json& j) {
    openlr2::ArenaPeerScore s{};
    if (!j.is_object()) {
        return s;
    }
    s.poor = JsonFieldOr<int>(j, {"poor"}, 0);
    s.bad = JsonFieldOr<int>(j, {"bad"}, 0);
    s.good = JsonFieldOr<int>(j, {"good"}, 0);
    s.great = JsonFieldOr<int>(j, {"great"}, 0);
    s.pgreat = JsonFieldOr<int>(j, {"pgreat"}, 0);
    s.maxCombo = JsonFieldOr<int>(j, {"maxCombo"}, 0);
    s.score = JsonFieldOr<int>(j, {"score"}, 0);
    s.currentNotes = JsonFieldOr<int>(j, {"currentNotes"}, 0);
    return s;
}

json ScoreToJson(const openlr2::ArenaPeerScore& s, bool finalScore) {
    json j = {
        {"poor", s.poor},
        {"bad", s.bad},
        {"good", s.good},
        {"great", s.great},
        {"pgreat", s.pgreat},
        {"maxCombo", s.maxCombo},
        {"score", s.score},
        {"currentNotes", s.currentNotes},
    };
    if (finalScore) {
        j["final"] = true;
    }
    return j;
}

openlr2::ArenaPeer* FindPeerLocked(int playerId) {
    for (auto& p : g_state.players) {
        if (p.playerId == playerId) {
            return &p;
        }
    }
    return nullptr;
}

void AppendChatLocked(std::string line) {
    g_state.chatLog.push_back(std::move(line));
    if (g_state.chatLog.size() > kMaxChatLog) {
        g_state.chatLog.erase(
            g_state.chatLog.begin(),
            g_state.chatLog.begin() + static_cast<std::ptrdiff_t>(g_state.chatLog.size() - kMaxChatLog)
        );
    }
}

// STC 2 / STC 4 room state.
void ApplyRoomStateLocked(const json& j) {
    if (const auto roomId = JsonField<std::string>(j, {"roomId"}); roomId && !roomId->empty()) {
        g_state.roomId = *roomId;
    }
    if (const auto hostId = JsonField<int>(j, {"hostId"})) {
        g_state.hostId = *hostId;
    }
    const auto it = j.find("players");
    if (it == j.end() || !it->is_array()) {
        return;
    }
    std::vector<openlr2::ArenaPeer> peers;
    peers.reserve(it->size());
    for (const auto& e : *it) {
        if (!e.is_object()) {
            continue;
        }
        openlr2::ArenaPeer peer{};
        peer.playerId = JsonFieldOr<int>(e, {"playerId"}, 0);
        if (peer.playerId <= 0) {
            continue;
        }
        peer.username = JsonFieldOr<std::string>(e, {"username"}, {});
        peer.isHost = JsonFieldOr<bool>(e, {"isHost"}, peer.playerId == g_state.hostId);
        peer.ready = JsonFieldOr<bool>(e, {"ready"}, false);
        peer.selectedHash = JsonFieldOr<std::string>(e, {"selectedHash"}, {});
        peer.option = JsonFieldOr<unsigned>(e, {"option"}, 0u);
        peer.gauge = JsonFieldOr<unsigned>(e, {"gauge"}, 0u);
        if (const auto s = e.find("score"); s != e.end()) {
            peer.score = ParseScore(*s);
        }
        peers.push_back(std::move(peer));
    }
    g_state.players = std::move(peers);
}

std::string PeerNameLocked(int playerId, const json& j) {
    if (const auto name = JsonField<std::string>(j, {"username"}); name && !name->empty()) {
        return *name;
    }
    if (const auto* peer = FindPeerLocked(playerId)) {
        if (!peer->username.empty()) {
            return peer->username;
        }
    }
    return std::format("player{}", playerId);
}

void HandleMessage(const std::vector<std::uint8_t>& frame) {
    if (frame.empty()) {
        return;
    }
    const std::uint8_t id = frame[0];
    json payload = json::object();
    if (frame.size() > 1) {
        json parsed = json::parse(frame.begin() + 1, frame.end(), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            DebugLog("WARN", "arena_stc_bad_json", std::format("id={} size={}", id, frame.size()));
            return;
        }
        payload = std::move(parsed);
    }

    std::lock_guard lock(g_stateMutex);
    switch (id) {
    case kStcPlayersScore: {
        const int playerId = JsonFieldOr<int>(payload, {"playerId"}, 0);
        if (auto* peer = FindPeerLocked(playerId)) {
            if (const auto s = payload.find("score"); s != payload.end()) {
                peer->score = ParseScore(*s);
            }
        }
        break;
    }
    case kStcPlayersReadyUpdate:
    case kStcUserlist:
        ApplyRoomStateLocked(payload);
        break;
    case kStcSelectedChart:
        g_state.randomSeed = static_cast<int>(JsonFieldOr<std::int64_t>(payload, {"randomSeed"}, 0));
        g_state.chartHash = JsonFieldOr<std::string>(payload, {"hash"}, {});
        g_state.chartTitle = JsonFieldOr<std::string>(payload, {"title"}, {});
        g_state.chartArtist = JsonFieldOr<std::string>(payload, {"artist"}, {});
        g_state.itemModeEnabled = JsonFieldOr<bool>(payload, {"itemModeEnabled"}, false);
        break;
    case kStcWelcome:
        g_state.selfPlayerId = JsonFieldOr<int>(payload, {"playerId"}, g_state.selfPlayerId);
        g_state.hostId = JsonFieldOr<int>(payload, {"hostId"}, g_state.hostId);
        if (const auto roomId = JsonField<std::string>(payload, {"roomId"}); roomId && !roomId->empty()) {
            g_state.roomId = *roomId;
        }
        g_state.connected = true;
        break;
    case kStcChat: {
        const int playerId = JsonFieldOr<int>(payload, {"playerId"}, 0);
        const std::string message = JsonFieldOr<std::string>(payload, {"message"}, {});
        if (message.empty()) {
            break;
        }
        if (JsonFieldOr<bool>(payload, {"system"}, false)) {
            AppendChatLocked(message);
        } else {
            AppendChatLocked(std::format("{}: {}", PeerNameLocked(playerId, payload), message));
        }
        break;
    }
    case kStcMissingChart: {
        const int playerId = JsonFieldOr<int>(payload, {"playerId"}, 0);
        AppendChatLocked(std::format("{} does not have the selected chart", PeerNameLocked(playerId, payload)));
        break;
    }
    case kStcError:
        DebugLog(
            "WARN",
            "arena_stc_error",
            std::format(
                "code={} message={}",
                JsonFieldOr<std::string>(payload, {"code"}, {}),
                JsonFieldOr<std::string>(payload, {"message"}, {})
            )
        );
        AppendChatLocked(std::format("[arena error] {}", JsonFieldOr<std::string>(payload, {"code"}, "unknown")));
        break;
    case kStcItem:
    case kStcItemSettings:
        // ArenaRoomState has no item channel yet (see known gaps).
        break;
    default:
        break;
    }
}

void RecvLoop(Connection* conn, HINTERNET ws) {
    std::vector<std::uint8_t> message;
    bool oversized = false;
    std::vector<std::uint8_t> chunk(8192);

    while (!conn->stopping) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
        const DWORD err = WinHttpWebSocketReceive(ws, chunk.data(), static_cast<DWORD>(chunk.size()), &read, &type);
        if (err == ERROR_WINHTTP_TIMEOUT) {
            continue;
        }
        if (err != NO_ERROR) {
            if (!conn->stopping) {
                DebugLogWin32Error("arena_ws_recv_fail", err);
            }
            break;
        }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            USHORT status = 0;
            DWORD reasonLen = 0;
            char reason[123]{};
            WinHttpWebSocketQueryCloseStatus(ws, &status, reason, sizeof(reason) - 1, &reasonLen);
            DebugLog("INFO", "arena_ws_closed_by_peer", std::format("status={} reason={}", status, reason));
            break;
        }

        if (!oversized) {
            if (message.size() + read > kMaxFrameBytes) {
                oversized = true;
                message.clear();
            } else {
                message.insert(message.end(), chunk.begin(), chunk.begin() + read);
            }
        }

        const bool finalFragment = type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE
            || type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        if (finalFragment) {
            if (!oversized) {
                HandleMessage(message);
            } else {
                DebugLog("WARN", "arena_stc_oversized_dropped", {});
            }
            message.clear();
            oversized = false;
        }
    }

    if (!conn->stopping) {
        conn->connected = false;
        std::lock_guard lock(g_stateMutex);
        g_state.connected = false;
        DebugLog("INFO", "arena_ws_disconnected", {});
    }
}

std::wstring ToWide(std::string_view ascii) {
    return std::wstring(ascii.begin(), ascii.end());
}

// Opens the WebSocket and starts the recv thread. Returns nullptr on failure.
std::shared_ptr<Connection> OpenConnection(const ParsedWsUrl& url) {
    const std::wstring userAgent = HttpAuth_UserAgent();
    auto conn = std::make_shared<Connection>();

    conn->session = WinHttpOpen(
        userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (conn->session == nullptr) {
        DebugLogWin32Error("arena_ws_open_fail");
        return nullptr;
    }
    // WebSocket upgrade requires HTTP/1.1: do not enable HTTP/2 on this session.
    WinHttpSetTimeouts(conn->session, 10000, 10000, 10000, 15000);
    DWORD dwSslProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(conn->session, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwSslProtocols, sizeof(dwSslProtocols));

    conn->connect = WinHttpConnect(conn->session, url.host.c_str(), url.port, 0);
    if (conn->connect == nullptr) {
        DebugLogWin32Error("arena_ws_connect_fail");
        return nullptr;
    }

    const std::wstring target = ToWide(url.target);
    HINTERNET request = WinHttpOpenRequest(
        conn->connect,
        L"GET",
        target.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        url.secure ? WINHTTP_FLAG_SECURE : 0
    );
    if (request == nullptr) {
        DebugLogWin32Error("arena_ws_request_fail");
        return nullptr;
    }

    bool ok = WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)
        && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(request, nullptr);
    if (!ok) {
        DebugLogWin32Error("arena_ws_handshake_send_fail");
        WinHttpCloseHandle(request);
        return nullptr;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX
    );
    if (statusCode != 101) {
        DebugLog("WARN", "arena_ws_handshake_status", std::format("status_code={}", statusCode));
        WinHttpCloseHandle(request);
        return nullptr;
    }

    conn->ws = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    if (conn->ws == nullptr) {
        DebugLogWin32Error("arena_ws_upgrade_fail");
        return nullptr;
    }

    // Idle sockets are normal between messages; do not time out receives.
    DWORD noTimeout = 0;
    WinHttpSetOption(conn->ws, WINHTTP_OPTION_RECEIVE_TIMEOUT, &noTimeout, sizeof(noTimeout));
#ifdef WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL
    DWORD keepAliveMs = 20000;
    WinHttpSetOption(conn->ws, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &keepAliveMs, sizeof(keepAliveMs));
#endif

    conn->connected = true;
    Connection* raw = conn.get();
    const HINTERNET ws = conn->ws;
    conn->recvThread = std::thread([raw, ws] { RecvLoop(raw, ws); });
    return conn;
}

// Tears down the current WebSocket (if any) and resets the shared room state.
void DisconnectLocal() {
    std::shared_ptr<Connection> old;
    {
        std::lock_guard lock(g_connPtrMutex);
        old = std::move(g_conn);
    }
    if (old) {
        old->Close();
    }
    std::lock_guard lock(g_stateMutex);
    g_state = {};
}

// Opens the WS for `roomId` using credentials from the HTTP response.
bool ConnectWithCredentials(const openlr2::ArenaJoinResult& creds) {
    ParsedWsUrl url;
    if (!ParseWsUrl(creds.wsUrl, creds.roomId, creds.token, url)) {
        DebugLog("ERROR", "arena_ws_url_invalid", {});
        return false;
    }
    DebugLog(
        "INFO",
        "arena_ws_connect_begin",
        std::format("host={} port={} secure={}", std::string(url.hostAscii), url.port, url.secure ? 1 : 0)
    );

    auto conn = OpenConnection(url);
    if (!conn) {
        return false;
    }
    {
        std::lock_guard lock(g_stateMutex);
        g_state = {};
        g_state.roomId = creds.roomId;
        g_state.connected = true;
    }
    {
        std::lock_guard lock(g_connPtrMutex);
        g_conn = std::move(conn);
    }
    DebugLog("INFO", "arena_ws_connected", std::format("room={}", creds.roomId));
    return true;
}

openlr2::GetStatus FromHttp(HttpStatus s) {
    switch (s) {
    case HttpStatus::Ok: return openlr2::GetStatus::Ok;
    case HttpStatus::Retry: return openlr2::GetStatus::Retry;
    default: return openlr2::GetStatus::Fail;
    }
}

// Parses {roomId?, wsUrl, token}; roomId falls back to `fallbackRoomId`.
bool ParseJoinResponse(const std::string& body, std::string_view fallbackRoomId, openlr2::ArenaJoinResult& out) {
    const json parsed = json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return false;
    }
    out.roomId = JsonFieldOr<std::string>(parsed, {"roomId"}, std::string(fallbackRoomId));
    out.wsUrl = JsonFieldOr<std::string>(parsed, {"wsUrl"}, {});
    out.token = JsonFieldOr<std::string>(parsed, {"token"}, {});
    return !out.roomId.empty() && !out.wsUrl.empty() && !out.token.empty();
}

// Serialized frame send. Silently ignored when not connected.
void SendFrame(std::uint8_t id, const json& payload) {
    const auto conn = CurrentConnection();
    if (!conn || !conn->connected || conn->ws == nullptr) {
        return;
    }
    std::string frame;
    frame.push_back(static_cast<char>(id));
    frame += payload.dump(-1, ' ', false, json::error_handler_t::replace);
    if (frame.size() > kMaxFrameBytes) {
        DebugLog("WARN", "arena_cts_too_large", std::format("id={} size={}", id, frame.size()));
        return;
    }
    std::lock_guard lock(conn->sendMutex);
    if (conn->stopping || conn->ws == nullptr) {
        return;
    }
    const DWORD err = WinHttpWebSocketSend(
        conn->ws,
        WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
        frame.data(),
        static_cast<DWORD>(frame.size())
    );
    if (err != NO_ERROR) {
        DebugLogWin32Error("arena_ws_send_fail", err);
    }
}

std::string SafeStr(const char* s) {
    return s != nullptr ? std::string(s) : std::string{};
}

} // namespace

openlr2::GetStatus OLR2_IR_API JoinRoom(const char* roomId, openlr2::ArenaJoinResult& out) {
    out = {};
    if (roomId == nullptr || roomId[0] == '\0') {
        return openlr2::GetStatus::Fail;
    }
    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        DebugLog("WARN", "arena_join_no_api_key", {});
        return openlr2::GetStatus::Fail;
    }

    std::lock_guard lifecycle(g_lifecycleMutex);
    DisconnectLocal();

    const std::wstring path = HttpAuth_ArenaPath(HttpAuthEndpoint::ArenaJoinRoom, roomId);
    std::string body;
    unsigned long httpCode = 0;
    // Body is signed verbatim; join ignores its content.
    const HttpStatus status = HttpPostJsonPath(path, "{}", apiKey, body, &httpCode);
    DebugLog("INFO", "arena_join_http", std::format("status_code={}", httpCode));
    if (status != HttpStatus::Ok) {
        return FromHttp(status);
    }

    openlr2::ArenaJoinResult creds;
    if (!ParseJoinResponse(body, roomId, creds)) {
        DebugLog("ERROR", "arena_join_bad_response", {});
        return openlr2::GetStatus::Fail;
    }
    if (!ConnectWithCredentials(creds)) {
        return openlr2::GetStatus::Retry;
    }
    out = std::move(creds);
    return openlr2::GetStatus::Ok;
}

openlr2::GetStatus OLR2_IR_API JoinActiveRoom(openlr2::ArenaJoinResult& out) {
    out = {};
    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        DebugLog("WARN", "arena_active_no_api_key", {});
        return openlr2::GetStatus::Fail;
    }

    std::lock_guard lifecycle(g_lifecycleMutex);
    DisconnectLocal();

    std::string body;
    unsigned long httpCode = 0;
    const HttpStatus status = HttpGetJsonPath(
        HttpAuth_ArenaPath(HttpAuthEndpoint::ArenaActive, {}),
        {},
        apiKey,
        body,
        &httpCode
    );
    DebugLog("INFO", "arena_active_http", std::format("status_code={}", httpCode));
    if (status != HttpStatus::Ok) {
        return FromHttp(status);
    }

    // Response is JSON `null` when the user is not in an active room, otherwise
    // {roomId, wsUrl, token} (credentials included; no extra join needed).
    const json parsed = json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        DebugLog("INFO", "arena_active_none", {});
        return openlr2::GetStatus::Fail;
    }
    openlr2::ArenaJoinResult creds;
    if (!ParseJoinResponse(body, {}, creds)) {
        DebugLog("ERROR", "arena_active_bad_response", {});
        return openlr2::GetStatus::Fail;
    }
    if (!ConnectWithCredentials(creds)) {
        return openlr2::GetStatus::Retry;
    }
    out = std::move(creds);
    return openlr2::GetStatus::Ok;
}

void OLR2_IR_API Leave() {
    std::lock_guard lifecycle(g_lifecycleMutex);
    std::string roomId;
    {
        std::lock_guard lock(g_stateMutex);
        roomId = g_state.roomId;
    }
    DisconnectLocal();
    if (roomId.empty()) {
        return;
    }
    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        return;
    }
    std::string body;
    unsigned long httpCode = 0;
    HttpPostJsonPath(HttpAuth_ArenaPath(HttpAuthEndpoint::ArenaLeave, roomId), "{}", apiKey, body, &httpCode);
    DebugLog("INFO", "arena_leave_http", std::format("status_code={}", httpCode));
}

bool OLR2_IR_API IsConnected() {
    const auto conn = CurrentConnection();
    return conn && conn->connected;
}

void OLR2_IR_API PollState(openlr2::ArenaRoomState& out) {
    {
        std::lock_guard lock(g_stateMutex);
        out = g_state;
    }
    out.connected = IsConnected();
}

void OLR2_IR_API SendSelectedChart(
    int randomSeed,
    const char* hash,
    const char* title,
    const char* artist,
    unsigned option,
    unsigned gauge,
    bool itemModeEnabled
) {
    SendFrame(kCtsSelectedChart, json{
        {"randomSeed", randomSeed},
        {"hash", SafeStr(hash)},
        {"title", SafeStr(title)},
        {"artist", SafeStr(artist)},
        {"option", option},
        {"gauge", gauge},
        {"itemModeEnabled", itemModeEnabled},
    });
}

void OLR2_IR_API SendScore(const openlr2::ArenaPeerScore& score, bool finalScore) {
    if (!finalScore) {
        std::lock_guard lock(g_scoreThrottleMutex);
        const auto now = std::chrono::steady_clock::now();
        if (now - g_lastScoreSent < kScoreMinInterval) {
            return;
        }
        g_lastScoreSent = now;
    } else {
        std::lock_guard lock(g_scoreThrottleMutex);
        g_lastScoreSent = std::chrono::steady_clock::now();
    }
    SendFrame(kCtsScore, ScoreToJson(score, finalScore));
}

void OLR2_IR_API SendReady() {
    SendFrame(kCtsReady, json::object());
}

void OLR2_IR_API SendCancelled() {
    SendFrame(kCtsChartCancelled, json::object());
}

void OLR2_IR_API SendChat(const char* message) {
    if (message == nullptr || message[0] == '\0') {
        return;
    }
    SendFrame(kCtsChat, json{{"message", std::string(message)}});
}

void OLR2_IR_API SendMissingChart() {
    SendFrame(kCtsMissingChart, json::object());
}

void OLR2_IR_API SendItemJson(const char* jsonObject) {
    if (jsonObject == nullptr) {
        return;
    }
    const json parsed = json::parse(jsonObject, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        DebugLog("WARN", "arena_item_bad_json", {});
        return;
    }
    SendFrame(kCtsItem, parsed);
}

openlr2::GetStatus OLR2_IR_API SubmitRoomResult(const char* roomId, const char* chartHash, const char* resultJson) {
    if (resultJson == nullptr) {
        return openlr2::GetStatus::Fail;
    }
    json result = json::parse(resultJson, nullptr, false);
    if (result.is_discarded() || !result.is_object()) {
        DebugLog("WARN", "arena_result_bad_json", {});
        return openlr2::GetStatus::Fail;
    }

    std::string room = SafeStr(roomId);
    if (room.empty()) {
        std::lock_guard lock(g_stateMutex);
        room = g_state.roomId;
    }
    if (room.empty()) {
        return openlr2::GetStatus::Fail;
    }
    const std::string apiKey = LoadApiKey();
    if (apiKey.empty()) {
        return openlr2::GetStatus::Fail;
    }

    json body = json::object();
    if (chartHash != nullptr && chartHash[0] != '\0') {
        body["chartHash"] = std::string(chartHash);
    }
    body["result"] = std::move(result);

    std::string response;
    unsigned long httpCode = 0;
    const HttpStatus status = HttpPostJsonPath(
        HttpAuth_ArenaPath(HttpAuthEndpoint::ArenaResult, room),
        body.dump(-1, ' ', false, json::error_handler_t::replace),
        apiKey,
        response,
        &httpCode
    );
    DebugLog("INFO", "arena_result_http", std::format("status_code={}", httpCode));
    return FromHttp(status);
}

} // namespace stellaverse_arena

#else // !_WIN32

namespace stellaverse_arena {

openlr2::GetStatus OLR2_IR_API JoinRoom(const char*, openlr2::ArenaJoinResult&) { return openlr2::GetStatus::Fail; }
openlr2::GetStatus OLR2_IR_API JoinActiveRoom(openlr2::ArenaJoinResult&) { return openlr2::GetStatus::Fail; }
void OLR2_IR_API Leave() {}
bool OLR2_IR_API IsConnected() { return false; }
void OLR2_IR_API PollState(openlr2::ArenaRoomState& out) { out = {}; }
void OLR2_IR_API SendSelectedChart(int, const char*, const char*, const char*, unsigned, unsigned, bool) {}
void OLR2_IR_API SendScore(const openlr2::ArenaPeerScore&, bool) {}
void OLR2_IR_API SendReady() {}
void OLR2_IR_API SendCancelled() {}
void OLR2_IR_API SendChat(const char*) {}
void OLR2_IR_API SendMissingChart() {}
void OLR2_IR_API SendItemJson(const char*) {}
openlr2::GetStatus OLR2_IR_API SubmitRoomResult(const char*, const char*, const char*) { return openlr2::GetStatus::Fail; }

} // namespace stellaverse_arena

#endif
