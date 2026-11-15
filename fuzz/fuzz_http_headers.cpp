// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// HTTP header fields and cache directives. The input is a response, read by Beast's parser with
// the limits downloads set (net/fetch.cpp), whose fields the cache then reads as it would a
// download's. The input's lines also go straight to the cache as field values, so the directive
// and date parsers see bytes that Beast would refuse.

#include "net/http_cache.h"

#include <boost/asio/buffer.hpp>
#include <boost/beast/http.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace bhttp = boost::beast::http;
namespace net = elg::detail::net;

constexpr std::int64_t kNow = 784111777; // Sun, 06 Nov 1994 08:49:37 GMT

// Every value of a field, joined with ", " (as downloads read Cache-Control and Vary).
template <class Fields>
std::string all_values(const Fields& fields, bhttp::field name) {
    std::string out;
    for (auto [it, end] = fields.equal_range(name); it != end; ++it) {
        if (!out.empty()) {
            out += ", ";
        }
        out += std::string_view(it->value());
    }
    return out;
}

// What the cache does with a response's fields: an entry for a 200 or a redirect, the update of a
// stored entry by a 304, and the dates on their own.
void cache_reads(unsigned status, const std::string& location, const net::CacheHeaders& headers) {
    for (const unsigned as : {200u, status}) {
        if (auto entry = net::make_cache_entry("https://example.com/a", as, location, headers, {1, 2}, kNow)) {
            static_cast<void>(entry->fresh(kNow + 3600));
            net::refresh_cache_entry(*entry, headers, kNow + 60);
            static_cast<void>(entry->fresh(kNow + 120));
        }
    }
    auto stored = net::make_cache_entry("https://example.com/a", 200, {}, {.cache_control = "max-age=60", .etag = "e"},
                                        {1}, kNow);
    if (!stored) {
        std::abort(); // a plain fresh response is always kept
    }
    net::refresh_cache_entry(*stored, headers, kNow + 30);
    static_cast<void>(net::forbids_storing(headers));
    for (const std::string* date : {&headers.date, &headers.expires, &headers.last_modified}) {
        static_cast<void>(net::parse_http_date(*date));
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // As a response.
    bhttp::response_parser<bhttp::vector_body<std::uint8_t>> parser;
    parser.header_limit(64 * 1024);
    parser.body_limit(1 << 20);
    parser.eager(true);
    boost::system::error_code error;
    parser.put(boost::asio::buffer(data, size), error);
    if (!error && !parser.is_done()) {
        parser.put_eof(error);
    }
    if (parser.is_header_done()) {
        const auto& response = parser.get();
        cache_reads(response.result_int(), std::string(response[bhttp::field::location]),
                    {
                        .cache_control = all_values(response, bhttp::field::cache_control),
                        .expires = std::string(response[bhttp::field::expires]),
                        .date = std::string(response[bhttp::field::date]),
                        .age = std::string(response[bhttp::field::age]),
                        .etag = std::string(response[bhttp::field::etag]),
                        .last_modified = std::string(response[bhttp::field::last_modified]),
                        .vary = all_values(response, bhttp::field::vary),
                    });
    }

    // As field values, one a line.
    std::vector<std::string> lines(8);
    std::size_t line = 0;
    for (std::size_t i = 0; i < size && line < lines.size(); ++i) {
        if (data[i] == '\n') {
            ++line;
        } else {
            lines[line] += static_cast<char>(data[i]);
        }
    }
    const unsigned status = lines[7].empty() ? 301 : 300 + static_cast<unsigned char>(lines[7][0]) % 10;
    cache_reads(status, lines[6], {lines[0], lines[1], lines[2], lines[3], lines[4], lines[5], lines[6]});
    return 0;
}
