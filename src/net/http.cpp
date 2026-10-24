// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// HTTP on the system's networking (design.md §13.3): WinHTTP on Windows, libcurl elsewhere (when
// available). Requests block a worker thread each; results go to the UI thread.

#include <elg/http.h>

#include "core/log.h"
#include "core/ui_tasks.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#elif __has_include(<curl/curl.h>)
#define ELG_HAVE_CURL 1
#include <curl/curl.h>
#endif

namespace elg::http {

struct Task::State {
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
#if defined(_WIN32)
    HINTERNET handle = nullptr; // the request, while it runs: closing it aborts a blocking call
#endif
};

std::string Response::header(std::string_view name) const {
    const auto same = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    };
    for (const auto& [key, value] : headers) {
        if (same(key, name)) {
            return value;
        }
    }
    return {};
}

void Task::cancel() const {
    if (!state_) {
        return;
    }
    state_->cancelled = true;
#if defined(_WIN32)
    std::lock_guard lock(state_->mutex);
    if (state_->handle) {
        WinHttpCloseHandle(state_->handle); // the worker's blocking call fails; it won't close it again
        state_->handle = nullptr;
    }
#endif
}

namespace {

// A few threads that each run one request at a time.
class Workers {
public:
    static Workers& instance() {
        static Workers workers;
        return workers;
    }

    void post(std::function<void()> job) {
        {
            std::lock_guard lock(mutex_);
            jobs_.push_back(std::move(job));
            if (threads_.size() < kMaxThreads && busy_ + jobs_.size() > threads_.size()) {
                threads_.emplace_back([this](std::stop_token stop) { run(stop); });
            }
        }
        ready_.notify_one();
    }

    ~Workers() {
        {
            std::lock_guard lock(mutex_);
            jobs_.clear();
        }
        for (std::jthread& thread : threads_) {
            thread.request_stop();
        }
        ready_.notify_all();
    }

private:
    static constexpr std::size_t kMaxThreads = 6;

    void run(std::stop_token stop) {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, stop, [&] { return !jobs_.empty(); });
                if (stop.stop_requested()) {
                    return;
                }
                job = std::move(jobs_.front());
                jobs_.pop_front();
                ++busy_;
            }
            job();
            std::lock_guard lock(mutex_);
            --busy_;
        }
    }

    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::deque<std::function<void()>> jobs_;
    std::vector<std::jthread> threads_;
    std::size_t busy_ = 0;
};

#if defined(_WIN32)

std::wstring widen(std::string_view text) {
    std::wstring wide(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                                      static_cast<int>(wide.size()));
    wide.resize(static_cast<std::size_t>(std::max(n, 0)));
    return wide;
}

std::string narrow(std::wstring_view text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(n, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

// What went wrong, in words: WinHTTP's own messages live in winhttp.dll.
std::string describe(DWORD error) {
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT: return "the request timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "the server's name couldn't be resolved";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "couldn't connect to the server";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "the connection was reset";
    case ERROR_WINHTTP_SECURE_FAILURE: return "the secure connection failed (certificate or TLS)";
    case ERROR_WINHTTP_OPERATION_CANCELLED: return "cancelled";
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
    case ERROR_WINHTTP_INVALID_URL: return "not an http(s) URL";
    default: break;
    }
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE |
                                            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        GetModuleHandleW(L"winhttp.dll"), error, 0, reinterpret_cast<LPWSTR>(&text), 0,
                                        nullptr);
    std::string message = length ? narrow(std::wstring_view(text, length)) : std::format("network error {}", error);
    LocalFree(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == '.')) {
        message.pop_back();
    }
    return message;
}

HINTERNET session() {
    static const HINTERNET handle = [] {
        // The system's proxy settings (automatic where Windows supports it).
        HINTERNET opened = WinHttpOpen(L"Elegance/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);
        if (!opened) {
            opened = WinHttpOpen(L"Elegance/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (opened) {
            DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
            WinHttpSetOption(opened, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
            DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
            WinHttpSetOption(opened, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols));
        }
        return opened;
    }();
    return handle;
}

Result perform(const Request& request, Task::State& state) {
    HINTERNET http = session();
    if (!http) {
        return std::unexpected(describe(GetLastError()));
    }
    const std::wstring url = widen(request.url);
    URL_COMPONENTS parts{sizeof(URL_COMPONENTS)};
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        return std::unexpected(describe(GetLastError()));
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    struct Handle {
        HINTERNET value = nullptr;
        ~Handle() {
            if (value) {
                WinHttpCloseHandle(value);
            }
        }
    };
    Handle connection{WinHttpConnect(http, host.c_str(), parts.nPort, 0)};
    if (!connection.value) {
        return std::unexpected(describe(GetLastError()));
    }
    HINTERNET handle = WinHttpOpenRequest(connection.value, widen(request.method).c_str(), path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          secure ? WINHTTP_FLAG_SECURE : 0);
    if (!handle) {
        return std::unexpected(describe(GetLastError()));
    }
    {
        std::lock_guard lock(state.mutex);
        if (state.cancelled) {
            WinHttpCloseHandle(handle);
            return std::unexpected("cancelled");
        }
        state.handle = handle; // cancel() closes it from now on
    }
    // Closes the request unless cancel() already did.
    struct Release {
        Task::State& state;
        ~Release() {
            std::lock_guard lock(state.mutex);
            if (state.handle) {
                WinHttpCloseHandle(state.handle);
                state.handle = nullptr;
            }
        }
    } release{state};

    const int timeout = static_cast<int>(std::max<std::int64_t>(request.timeout.count(), 1));
    WinHttpSetTimeouts(handle, timeout, timeout, timeout, timeout);
    for (const auto& [name, value] : request.headers) {
        const std::wstring line = widen(name + ": " + value);
        WinHttpAddRequestHeaders(handle, line.c_str(), static_cast<DWORD>(line.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    const auto failed = [&]() -> Result {
        return std::unexpected(state.cancelled ? std::string("cancelled") : describe(GetLastError()));
    };
    void* body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data());
    const auto body_size = static_cast<DWORD>(request.body.size());
    if (!WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, body_size, body_size, 0) ||
        !WinHttpReceiveResponse(handle, nullptr)) {
        return failed();
    }

    Response response;
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    response.status = static_cast<int>(status);
    DWORD raw_size = 0;
    WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &raw_size,
                        WINHTTP_NO_HEADER_INDEX);
    std::wstring raw(raw_size / sizeof(wchar_t), L'\0');
    if (raw_size && WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                        raw.data(), &raw_size, WINHTTP_NO_HEADER_INDEX)) {
        const std::string text = narrow(raw);
        std::size_t start = text.find("\r\n"); // past the status line
        while (start != std::string::npos && start + 2 < text.size()) {
            start += 2;
            const std::size_t end = text.find("\r\n", start);
            const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (const std::size_t colon = line.find(':'); colon != std::string::npos) {
                std::string value = line.substr(colon + 1);
                value.erase(0, value.find_first_not_of(' '));
                response.headers.emplace_back(line.substr(0, colon), std::move(value));
            }
            start = end;
        }
    }
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handle, &available)) {
            return failed();
        }
        if (available == 0) {
            break;
        }
        const std::size_t at = response.body.size();
        response.body.resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(handle, response.body.data() + at, available, &read)) {
            return failed();
        }
        response.body.resize(at + read);
    }
    return response;
}

#elif defined(ELG_HAVE_CURL)

Result perform(const Request& request, Task::State& state) {
    static const bool started = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!started) {
        return std::unexpected("libcurl couldn't start");
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::unexpected("libcurl couldn't start");
    }
    Response response;
    curl_slist* headers = nullptr;
    for (const auto& [name, value] : request.headers) {
        headers = curl_slist_append(headers, (name + ": " + value).c_str());
    }
    const auto write_body = +[](char* data, std::size_t size, std::size_t count, void* user) -> std::size_t {
        auto& out = *static_cast<std::vector<std::uint8_t>*>(user);
        out.insert(out.end(), data, data + size * count);
        return size * count;
    };
    const auto write_header = +[](char* data, std::size_t size, std::size_t count, void* user) -> std::size_t {
        const std::string line(data, size * count);
        if (const std::size_t colon = line.find(':'); colon != std::string::npos) {
            std::string value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
                value.pop_back();
            }
            static_cast<Headers*>(user)->emplace_back(line.substr(0, colon), std::move(value));
        }
        return size * count;
    };
    const auto progress = +[](void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
        return static_cast<Task::State*>(user)->cancelled ? 1 : 0;
    };
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout.count()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (!request.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) {
        return std::unexpected(state.cancelled ? std::string("cancelled") : std::string(curl_easy_strerror(code)));
    }
    response.status = static_cast<int>(status);
    return response;
}

#else

Result perform(const Request&, Task::State&) {
    return std::unexpected("HTTP isn't available in this build");
}

#endif

} // namespace

Task fetch(Request request, std::function<void(const Result&)> done) {
    auto state = std::make_shared<Task::State>();
    Workers::instance().post([request = std::move(request), done = std::move(done), state] {
        if (state->cancelled) {
            return;
        }
        Result result = perform(request, *state);
        if (state->cancelled || !done) {
            return;
        }
        detail::post_ui_task([state, done, result = std::move(result)] {
            if (!state->cancelled) {
                done(result);
            }
        });
    });
    return Task(state);
}

// --- Fetch ---

struct Fetch::State {
    Request request;
    Task task;
    elg::State<bool> loading{true};
    elg::State<std::shared_ptr<const Response>> response{std::shared_ptr<const Response>()};
    elg::State<std::string> error{std::string()};

    ~State() { task.cancel(); }

    void start(const std::shared_ptr<State>& self) {
        task.cancel();
        loading = true;
        task = fetch(request, [weak = std::weak_ptr(self)](const Result& result) {
            const auto state = weak.lock();
            if (!state) {
                return;
            }
            if (result) {
                state->response = std::make_shared<const Response>(*result);
                state->error = result->ok() ? std::string() : std::format("HTTP {}", result->status);
            } else {
                state->error = result.error();
            }
            state->loading = false;
        });
    }
};

Fetch::Fetch(Request request) : state_(std::make_shared<State>()) {
    state_->request = std::move(request);
    state_->start(state_);
}

bool Fetch::is_loading() const {
    return state_->loading();
}

bool Fetch::has_response() const {
    return state_->response() != nullptr;
}

bool Fetch::ok() const {
    const auto& response = state_->response();
    return response && response->ok();
}

Response Fetch::response() const {
    const auto& response = state_->response();
    return response ? *response : Response{};
}

std::string Fetch::error() const {
    return state_->error();
}

void Fetch::reload() const {
    state_->start(state_);
}

} // namespace elg::http
