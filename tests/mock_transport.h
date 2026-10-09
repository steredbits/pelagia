// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_TESTS_MOCK_TRANSPORT_H
#define PELAGIA_TESTS_MOCK_TRANSPORT_H

// Mocked HTTP transport (same model as tests/test_jellyfin_client.cpp):
// records the requests and replays queued responses.

#include <mutex>
#include <string>
#include <vector>

#include "api/http_client.h"

namespace mock {

struct RecordedRequest {
    std::string method;
    std::string url;
    std::vector<api::HttpHeader> headers;
    std::string body;
};

// Mocked transport: records the requests and replays queued responses.
class MockTransport final : public api::HttpTransport {
public:
    api::HttpResponse get(const std::string& url,
                          const std::vector<api::HttpHeader>& headers) override {
        requests.push_back({"GET", url, headers, ""});
        return next_response();
    }

    api::HttpResponse post(const std::string& url,
                           const std::vector<api::HttpHeader>& headers,
                           const std::string& body,
                           const std::string& /*content_type*/) override {
        requests.push_back({"POST", url, headers, body});
        return next_response();
    }

    api::HttpResponse del(const std::string& url,
                          const std::vector<api::HttpHeader>& headers) override {
        requests.push_back({"DELETE", url, headers, ""});
        return next_response();
    }

    void queue_response(long status, const std::string& body) {
        api::HttpResponse r;
        r.ok = true;
        r.status = status;
        r.body = body;
        responses.push_back(r);
    }

    void queue_transport_error(const std::string& message) {
        api::HttpResponse r;
        r.ok = false;
        r.error = message;
        responses.push_back(r);
    }

    std::vector<RecordedRequest> requests;
    std::vector<api::HttpResponse> responses;

private:
    api::HttpResponse next_response() {
        if (responses.empty()) {
            api::HttpResponse r;
            r.error = "mock: no queued response";
            return r;
        }
        api::HttpResponse r = responses.front();
        responses.erase(responses.begin());
        return r;
    }
};

inline std::string header_value(const RecordedRequest& req, const std::string& name) {
    for (const api::HttpHeader& h : req.headers) {
        if (h.name == name) {
            return h.value;
        }
    }
    return "";
}

inline bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace mock

#endif  // PELAGIA_TESTS_MOCK_TRANSPORT_H
