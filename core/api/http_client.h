// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_API_HTTP_CLIENT_H
#define PELAGIA_CORE_API_HTTP_CLIENT_H

// Abstraction of the Jellyfin client's HTTP transport.
// The API code only depends on this interface: tests inject a
// mocked transport, the real executable uses HttpCurlTransport (libcurl).

#include <string>
#include <vector>

namespace api {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpResponse {
    bool ok = false;     // true if an HTTP response was received (transport OK)
    long status = 0;     // HTTP status code (200, 401, ...) if ok
    std::string body;    // response body
    std::string error;   // transport error message if !ok
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;

    virtual HttpResponse get(const std::string& url,
                             const std::vector<HttpHeader>& headers) = 0;

    // body is sent as is with the given Content-Type header.
    virtual HttpResponse post(const std::string& url,
                              const std::vector<HttpHeader>& headers,
                              const std::string& body,
                              const std::string& content_type) = 0;

    // DELETE without a body (e.g. stopping a transcoding job).
    virtual HttpResponse del(const std::string& url,
                             const std::vector<HttpHeader>& headers) = 0;
};

}  // namespace api

#endif  // PELAGIA_CORE_API_HTTP_CLIENT_H
