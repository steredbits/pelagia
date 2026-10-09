// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#include "api/http_curl.h"

#include <curl/curl.h>

#include "util/log.h"

namespace api {

namespace {

// curl_global_init must be called exactly once before any easy handle.
void ensure_curl_global_init() {
    static bool initialized = false;
    if (!initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        initialized = true;
    }
}

size_t write_to_string(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

}  // namespace

HttpCurlTransport::HttpCurlTransport() {
    ensure_curl_global_init();
}

void HttpCurlTransport::set_timeouts(long connect_timeout_s, long total_timeout_s) {
    connect_timeout_s_ = connect_timeout_s;
    total_timeout_s_ = total_timeout_s;
}

HttpResponse HttpCurlTransport::get(const std::string& url,
                                    const std::vector<HttpHeader>& headers) {
    return perform("GET", url, headers, nullptr, "");
}

HttpResponse HttpCurlTransport::post(const std::string& url,
                                     const std::vector<HttpHeader>& headers,
                                     const std::string& body,
                                     const std::string& content_type) {
    return perform("POST", url, headers, &body, content_type);
}

HttpResponse HttpCurlTransport::del(const std::string& url,
                                    const std::vector<HttpHeader>& headers) {
    return perform("DELETE", url, headers, nullptr, "");
}

HttpResponse HttpCurlTransport::perform(const char* method, const std::string& url,
                                        const std::vector<HttpHeader>& headers,
                                        const std::string* post_body,
                                        const std::string& content_type) {
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist* header_list = nullptr;
    for (const HttpHeader& h : headers) {
        std::string line = h.name + ": " + h.value;
        header_list = curl_slist_append(header_list, line.c_str());
    }
    if (post_body && !content_type.empty()) {
        std::string line = "Content-Type: " + content_type;
        header_list = curl_slist_append(header_list, line.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_s_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, total_timeout_s_);
    // No SIGALRM for DNS timeouts: concurrent calls from
    // several threads (demux, reporting).
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (std::string(method) == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    if (post_body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(post_body->size()));
    }

    CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        response.ok = true;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    } else {
        // Diagnostics only: the error is returned through response.error and
        // the caller reports it (a single visible error line).
        response.error = curl_easy_strerror(rc);
        LOG_DEBUG("HTTP %s : %s", method, response.error.c_str());
    }

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return response;
}

}  // namespace api
