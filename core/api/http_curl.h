// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_API_HTTP_CURL_H
#define PELAGIA_CORE_API_HTTP_CURL_H

// libcurl implementation of HttpTransport. libcurl is portable (Linux and
// the PS5 homebrew SDK), so this file stays in the core.

#include "api/http_client.h"

namespace api {

class HttpCurlTransport final : public HttpTransport {
public:
    HttpCurlTransport();

    HttpResponse get(const std::string& url,
                     const std::vector<HttpHeader>& headers) override;

    HttpResponse post(const std::string& url,
                      const std::vector<HttpHeader>& headers,
                      const std::string& body,
                      const std::string& content_type) override;

    HttpResponse del(const std::string& url,
                     const std::vector<HttpHeader>& headers) override;

    // Timeouts in seconds (connection / whole request).
    void set_timeouts(long connect_timeout_s, long total_timeout_s);

private:
    HttpResponse perform(const char* method, const std::string& url,
                         const std::vector<HttpHeader>& headers,
                         const std::string* post_body,
                         const std::string& content_type);

    long connect_timeout_s_ = 10;
    long total_timeout_s_ = 30;
};

}  // namespace api

#endif  // PELAGIA_CORE_API_HTTP_CURL_H
