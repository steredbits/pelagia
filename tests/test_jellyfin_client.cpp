// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// End-to-end tests of the Jellyfin client, with a mocked HTTP transport:
// checks the URLs called, the authentication headers and error
// handling (HTTP 401, network down, invalid JSON).

#include "api/jellyfin_client.h"
#include "api/jellyfin_reporter.h"
#include "api/jellyfin_stream.h"

#include <string>
#include <vector>

#include "test_framework.h"
#include "util/version.h"

namespace {

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
            r.error = "mock : aucune réponse en file";
            return r;
        }
        api::HttpResponse r = responses.front();
        responses.erase(responses.begin());
        return r;
    }
};

const char kAuthResponse[] = R"json({
  "User": { "Name": "alice", "Id": "user-1" },
  "AccessToken": "tok-123",
  "ServerId": "srv-1"
})json";

const char kViewsResponse[] = R"json({
  "Items": [ { "Name": "Films", "Id": "lib-1", "CollectionType": "movies" } ],
  "TotalRecordCount": 1
})json";

const char kMoviesResponse[] = R"json({
  "Items": [ { "Name": "Sintel", "Id": "movie-1", "Type": "Movie" } ],
  "TotalRecordCount": 1
})json";

const char kItemResponse[] = R"json({
  "Id": "movie-1", "Name": "Sintel", "Type": "Movie", "RunTimeTicks": 8880000000,
  "UserData": { "PlaybackPositionTicks": 3000000000, "Played": false },
  "MediaSources": [ { "Id": "source-1", "Container": "mkv" } ]
})json";

std::string header_value(const RecordedRequest& req, const std::string& name) {
    for (const api::HttpHeader& h : req.headers) {
        if (h.name == name) {
            return h.value;
        }
    }
    return "";
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    // Nominal path: auth, libraries, movies, stream URL.
    {
        MockTransport mock;
        mock.queue_response(200, kAuthResponse);
        mock.queue_response(200, kViewsResponse);
        mock.queue_response(200, kMoviesResponse);

        api::JellyfinClient client(mock, "http://srv:8096/", "dev-test");
        CHECK(!client.is_authenticated());

        CHECK(client.authenticate("alice", "secret").ok());
        CHECK(client.is_authenticated());
        CHECK_EQ(client.session().access_token, "tok-123");
        CHECK_EQ(client.session().user_id, "user-1");

        // Auth request: right URL, right body, MediaBrowser header without Token.
        CHECK_EQ(mock.requests[0].method, "POST");
        CHECK_EQ(mock.requests[0].url, "http://srv:8096/Users/AuthenticateByName");
        CHECK(contains(mock.requests[0].body, "\"Username\":\"alice\""));
        CHECK(contains(mock.requests[0].body, "\"Pw\":\"secret\""));
        std::string auth_header = header_value(mock.requests[0], "Authorization");
        CHECK(contains(auth_header, "MediaBrowser Client=\"Pelagia\""));
        CHECK(contains(auth_header, "Device=\"Pelagia (PS5)\""));
        CHECK(contains(auth_header, std::string("Version=\"") + util::kVersion + "\""));
        CHECK(contains(auth_header, "DeviceId=\"dev-test\""));
        CHECK(!contains(auth_header, "Token="));

        std::vector<api::Library> libraries;
        CHECK(client.fetch_libraries(&libraries).ok());
        CHECK_EQ(libraries.size(), 1u);
        CHECK_EQ(mock.requests[1].url, "http://srv:8096/Users/user-1/Views");
        // Once the session is open, the token is sent.
        CHECK(contains(header_value(mock.requests[1], "Authorization"),
                       "Token=\"tok-123\""));

        api::ItemsQuery query;
        query.include_item_types = "Movie";
        query.recursive = true;
        api::ItemList movies;
        CHECK(client.fetch_items(query, &movies).ok());
        CHECK_EQ(movies.items.size(), 1u);
        CHECK(contains(mock.requests[2].url, "/Users/user-1/Items?"));
        CHECK(contains(mock.requests[2].url, "includeItemTypes=Movie"));

        std::string stream = client.stream_url("movie-1");
        CHECK(contains(stream, "/Videos/movie-1/stream.ts?"));
        CHECK(contains(stream, "videoCodec=h264"));
        CHECK(contains(stream, "api_key=tok-123"));
        CHECK(!contains(stream, "startTimeTicks"));
        CHECK(!contains(stream, "playSessionId"));

        // Resume at a position + dedicated playback session.
        std::string stream_seek = client.stream_url("movie-1", 6000000000LL, "play-1");
        CHECK(contains(stream_seek, "startTimeTicks=6000000000"));
        CHECK(contains(stream_seek, "playSessionId=play-1"));
    }

    // Item (resume), stream request depending on the authentication,
    // stop of a session's transcoding job.
    {
        MockTransport mock;
        mock.queue_response(200, kAuthResponse);
        mock.queue_response(200, kItemResponse);
        api::JellyfinClient client(mock, "http://srv:8096", "dev-test");
        CHECK(client.authenticate("alice", "secret").ok());

        api::MediaItem item;
        CHECK(client.fetch_item("movie-1", &item).ok());
        CHECK_EQ(mock.requests[1].url, "http://srv:8096/Users/user-1/Items/movie-1");
        CHECK_EQ(item.runtime_ticks, 8880000000LL);
        CHECK_EQ(item.playback_position_ticks, 3000000000LL);
        CHECK(!item.played);
        CHECK_EQ(item.media_source_id, "source-1");

        // 1 ms = 10,000 ticks; both = api_key + header.
        player::StreamRequest both = api::build_stream_request(
            client, "movie-1", 600000, "sess-1", api::StreamAuthMode::Both);
        CHECK(contains(both.url, "startTimeTicks=6000000000"));
        CHECK(contains(both.url, "playSessionId=sess-1"));
        CHECK(contains(both.url, "deviceId=dev-test"));
        CHECK(contains(both.url, "api_key=tok-123"));
        CHECK(contains(both.headers, "Authorization: MediaBrowser "));
        CHECK(contains(both.headers, "Token=\"tok-123\""));
        CHECK(both.headers.size() >= 2 &&
              both.headers.compare(both.headers.size() - 2, 2, "\r\n") == 0);
        CHECK(!contains(both.log_url, "tok-123"));
        CHECK(contains(both.log_url, "api_key=********"));

        player::StreamRequest header = api::build_stream_request(
            client, "movie-1", 0, "sess-2", api::StreamAuthMode::Header);
        CHECK(!contains(header.url, "api_key"));
        CHECK(!contains(header.url, "startTimeTicks"));
        CHECK(contains(header.headers, "Token=\"tok-123\""));

        player::StreamRequest query = api::build_stream_request(
            client, "movie-1", 0, "sess-3", api::StreamAuthMode::Query);
        CHECK(contains(query.url, "api_key=tok-123"));
        CHECK(query.headers.empty());

        api::StreamAuthMode mode = api::StreamAuthMode::Both;
        CHECK(api::parse_stream_auth_mode("header", &mode) && mode == api::StreamAuthMode::Header);
        CHECK(!api::parse_stream_auth_mode("cookie", &mode));

        // release(): DELETE with deviceId + playSessionId, MediaBrowser header.
        MockTransport control;
        control.queue_response(204, "");
        api::JellyfinStreamLocator locator(client, control, "movie-1", api::StreamAuthMode::Both);
        locator.release("sess-1");
        CHECK_EQ(control.requests.size(), 1u);
        CHECK_EQ(control.requests[0].method, "DELETE");
        CHECK_EQ(control.requests[0].url,
                 "http://srv:8096/Videos/ActiveEncodings?deviceId=dev-test&playSessionId=sess-1");
        CHECK(contains(header_value(control.requests[0], "Authorization"), "Token=\"tok-123\""));
        // A new session at each call.
        CHECK(locator.new_session_id() != locator.new_session_id());
        CHECK_EQ(locator.new_session_id().size(), 32u);

        // Tracks: taken at each locate(), changed without recreating the locator.
        player::StreamRequest first;
        CHECK(locator.locate(0, "s1", &first));
        CHECK(!contains(first.url, "audioStreamIndex"));
        api::TrackSelection tracks;
        tracks.media_source_id = "src-1";
        tracks.audio_index = 3;
        tracks.subtitle_index = 5;
        tracks.subtitle_delivery = api::SubtitleDelivery::Encode;
        locator.set_tracks(tracks);
        CHECK(locator.tracks() == tracks);
        player::StreamRequest second;
        CHECK(locator.locate(120000, "s2", &second));
        CHECK(contains(second.url, "mediaSourceId=src-1"));
        CHECK(contains(second.url, "audioStreamIndex=3"));
        CHECK(contains(second.url, "subtitleStreamIndex=5&subtitleMethod=Encode"));
        CHECK(contains(second.url, "startTimeTicks=1200000000"));
        CHECK(contains(second.url, "playSessionId=s2"));
    }

    // Subtitles: authenticated GET of the SRT route, body returned as is; HTTP errors reported.
    {
        MockTransport mock;
        mock.queue_response(200, kAuthResponse);
        api::JellyfinClient client(mock, "http://srv:8096", "dev-test");
        std::string srt;
        CHECK(client.fetch_subtitle("movie-1", "src-1", 4, &srt).error == api::ApiError::NotAuthenticated);
        CHECK(client.authenticate("alice", "secret").ok());
        mock.queue_response(200, "1\n00:00:01,000 --> 00:00:02,000\nSalut\n");
        CHECK(client.fetch_subtitle("movie-1", "src-1", 4, &srt).ok());
        CHECK(contains(srt, "Salut"));
        CHECK_EQ(mock.requests.back().method, "GET");
        CHECK_EQ(mock.requests.back().url,
                 "http://srv:8096/Videos/movie-1/src-1/Subtitles/4/Stream.srt");
        CHECK(contains(header_value(mock.requests.back(), "Authorization"), "Token=\"tok-123\""));
        mock.queue_response(404, "");
        api::ApiResult missing = client.fetch_subtitle("movie-1", "src-1", 9, &srt);
        CHECK(missing.error == api::ApiError::Http && missing.http_status == 404);
    }

    // Playback reports: URL, body, background sending and merging of the
    // pending progress reports.
    {
        MockTransport mock;
        mock.queue_response(200, kAuthResponse);
        api::JellyfinClient client(mock, "http://srv:8096", "dev-test");
        CHECK(client.authenticate("alice", "secret").ok());

        player::PlaybackReport progress;
        progress.kind = player::ReportKind::Progress;
        progress.position_ms = 61500;
        progress.paused = true;
        progress.session_id = "sess-9";
        const std::string body = api::build_playback_report_body(progress, "movie-1", "source-1");
        CHECK(contains(body, "\"ItemId\":\"movie-1\""));
        CHECK(contains(body, "\"MediaSourceId\":\"source-1\""));
        CHECK(contains(body, "\"PlaySessionId\":\"sess-9\""));
        CHECK(contains(body, "\"PositionTicks\":615000000"));
        CHECK(contains(body, "\"IsPaused\":true"));
        CHECK(contains(body, "\"PlayMethod\":\"Transcode\""));
        player::PlaybackReport stopped = progress;
        stopped.kind = player::ReportKind::Stopped;
        const std::string stop_body = api::build_playback_report_body(stopped, "movie-1", "");
        CHECK(contains(stop_body, "\"MediaSourceId\":\"movie-1\""));  // fallback on the item
        CHECK(!contains(stop_body, "PlayMethod"));
        // Without known tracks: neither AudioStreamIndex nor SubtitleStreamIndex.
        CHECK(!contains(body, "AudioStreamIndex"));
        CHECK(!contains(body, "SubtitleStreamIndex"));
        // Current tracks: the server remembers them (SubtitleStreamIndex -1 = none).
        player::PlaybackReport tracked = progress;
        tracked.has_tracks = true;
        tracked.media_source_id = "source-2";
        tracked.audio_index = 2;
        tracked.subtitle_index = 4;
        const std::string tracked_body =
            api::build_playback_report_body(tracked, "movie-1", "source-1");
        CHECK(contains(tracked_body, "\"MediaSourceId\":\"source-2\""));
        CHECK(contains(tracked_body, "\"AudioStreamIndex\":2"));
        CHECK(contains(tracked_body, "\"SubtitleStreamIndex\":4"));
        tracked.subtitle_index = -1;
        CHECK(contains(api::build_playback_report_body(tracked, "movie-1", ""),
                       "\"SubtitleStreamIndex\":-1"));
        tracked.kind = player::ReportKind::Stopped;
        CHECK(!contains(api::build_playback_report_body(tracked, "movie-1", ""), "StreamIndex"));
        CHECK_EQ(api::build_playback_report_url("http://srv:8096/", player::ReportKind::Started),
                 "http://srv:8096/Sessions/Playing");
        CHECK_EQ(api::build_playback_report_url("http://srv:8096", player::ReportKind::Progress),
                 "http://srv:8096/Sessions/Playing/Progress");
        CHECK_EQ(api::build_playback_report_url("http://srv:8096", player::ReportKind::Stopped),
                 "http://srv:8096/Sessions/Playing/Stopped");

        // Thread not started: the reports accumulate, consecutive progress
        // reports merge, Started/Stopped stay.
        MockTransport reports;
        for (int i = 0; i < 4; ++i) {
            reports.queue_response(204, "");
        }
        api::PlaybackReporter reporter(client, reports, "movie-1", "source-1");
        player::PlaybackReport started = progress;
        started.kind = player::ReportKind::Started;
        reporter.submit(started);
        progress.position_ms = 1000;
        reporter.submit(progress);
        progress.position_ms = 2000;
        reporter.submit(progress);  // replaces the previous one
        reporter.submit(stopped);
        reporter.start();
        CHECK(reporter.finish(3000));
        CHECK_EQ(reporter.sent_count(), 3);
        CHECK_EQ(reports.requests.size(), 3u);
        if (reports.requests.size() == 3) {
            CHECK_EQ(reports.requests[0].url, "http://srv:8096/Sessions/Playing");
            CHECK(contains(reports.requests[1].body, "\"PositionTicks\":20000000"));
            CHECK_EQ(reports.requests[2].url, "http://srv:8096/Sessions/Playing/Stopped");
            CHECK(contains(header_value(reports.requests[0], "Authorization"),
                           "Token=\"tok-123\""));
        }
    }

    // Credentials refused: HTTP 401, no session.
    {
        MockTransport mock;
        mock.queue_response(401, "");
        api::JellyfinClient client(mock, "http://srv:8096");
        api::ApiResult result = client.authenticate("alice", "mauvais");
        CHECK(!result.ok());
        CHECK(result.error == api::ApiError::Http);
        CHECK_EQ(result.http_status, 401);
        // The HTTP code is carried by http_status: the message does not repeat it.
        CHECK_EQ(result.message, "authentication refused");
        CHECK(!contains(result.message, "401"));
        CHECK(!client.is_authenticated());
    }

    // Transport error (server unreachable).
    {
        MockTransport mock;
        mock.queue_transport_error("connexion refusée");
        api::JellyfinClient client(mock, "http://srv:8096");
        api::ApiResult result = client.authenticate("alice", "secret");
        CHECK(!result.ok());
        CHECK(result.error == api::ApiError::Transport);
        CHECK_EQ(result.message, "connexion refusée");
    }

    // 200 response but invalid JSON.
    {
        MockTransport mock;
        mock.queue_response(200, "<html>pas du json</html>");
        api::JellyfinClient client(mock, "http://srv:8096");
        api::ApiResult result = client.authenticate("alice", "secret");
        CHECK(!result.ok());
        CHECK(result.error == api::ApiError::Parse);
        CHECK(!client.is_authenticated());
    }

    // Call without prior authentication.
    {
        MockTransport mock;
        api::JellyfinClient client(mock, "http://srv:8096");
        std::vector<api::Library> libraries;
        api::ApiResult result = client.fetch_libraries(&libraries);
        CHECK(!result.ok());
        CHECK(result.error == api::ApiError::NotAuthenticated);
        CHECK(mock.requests.empty());  // no network call attempted
    }

    return testfw::test_failures();
}
