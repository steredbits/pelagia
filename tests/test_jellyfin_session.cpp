// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// Tests of the session resume by token
// (/Users/Me), sign-out, "Continue watching", posters, sorted lists.

#include <string>

#include "api/jellyfin_client.h"
#include "api/jellyfin_json.h"
#include "api/jellyfin_urls.h"
#include "mock_transport.h"
#include "test_framework.h"

using mock::contains;
using mock::header_value;
using mock::MockTransport;

static void test_restore_session() {
  MockTransport t;
  t.queue_response(200, R"({"Id":"user-1","Name":"alice","ServerId":"srv"})");
  api::JellyfinClient client(t, "http://srv:8096", "dev");
  CHECK(client.restore_session("tok-abc").ok());
  CHECK(client.is_authenticated());
  CHECK_EQ(client.session().user_id, "user-1");
  CHECK_EQ(client.session().user_name, "alice");
  CHECK_EQ(client.session().access_token, "tok-abc");
  CHECK_EQ(t.requests[0].url, "http://srv:8096/Users/Me");
  CHECK(contains(header_value(t.requests[0], "Authorization"), "Token=\"tok-abc\""));

  // Revoked token: 401, session closed.
  MockTransport t2;
  t2.queue_response(401, "");
  api::JellyfinClient revoked(t2, "http://srv:8096", "dev");
  const api::ApiResult r = revoked.restore_session("tok-old");
  CHECK(!r.ok());
  CHECK_EQ(r.http_status, 401);
  CHECK(!revoked.is_authenticated());
  // Server unreachable: transport error (to tell apart from a rejected token).
  MockTransport t3;
  t3.queue_transport_error("Connection refused");
  api::JellyfinClient offline(t3, "http://srv:8096", "dev");
  CHECK(offline.restore_session("tok").error == api::ApiError::Transport);
  CHECK(!offline.is_authenticated());
  // No token: no request.
  MockTransport t4;
  api::JellyfinClient empty(t4, "http://srv:8096", "dev");
  CHECK(!empty.restore_session("").ok());
  CHECK(t4.requests.empty());
}

static void test_logout() {
  MockTransport t;
  t.queue_response(200, R"({"Id":"user-1","Name":"alice"})");
  t.queue_response(204, "");
  api::JellyfinClient client(t, "http://srv:8096", "dev");
  CHECK(client.restore_session("tok-abc").ok());
  CHECK(client.logout().ok());
  CHECK(!client.is_authenticated());
  CHECK_EQ(t.requests[1].method, "POST");
  CHECK_EQ(t.requests[1].url, "http://srv:8096/Sessions/Logout");
  CHECK(contains(header_value(t.requests[1], "Authorization"), "Token=\"tok-abc\""));
  // Server unreachable: the local session is closed anyway.
  MockTransport t2;
  t2.queue_response(200, R"({"Id":"u","Name":"a"})");
  t2.queue_transport_error("timeout");
  api::JellyfinClient c2(t2, "http://srv:8096", "dev");
  c2.restore_session("tok");
  CHECK(!c2.logout().ok());
  CHECK(!c2.is_authenticated());
}

static void test_resume() {
  MockTransport t;
  t.queue_response(200, R"({"Id":"user-1","Name":"alice"})");
  t.queue_response(200, R"({"Items":[{"Id":"ep1","Name":"Pilote","Type":"Episode",
      "SeriesId":"s1","SeriesName":"Série","SeriesPrimaryImageTag":"stag",
      "IndexNumber":1,"ParentIndexNumber":2,"RunTimeTicks":15000000000,
      "UserData":{"PlaybackPositionTicks":6000000000,"Played":false}}],
      "TotalRecordCount":1})");
  api::JellyfinClient client(t, "http://srv:8096", "dev");
  client.restore_session("tok");
  api::ItemList list;
  CHECK(client.fetch_resume(12, &list).ok());
  CHECK_EQ(list.items.size(), 1u);
  const api::MediaItem& ep = list.items[0];
  CHECK_EQ(ep.series_id, "s1");
  CHECK_EQ(ep.series_primary_image_tag, "stag");
  CHECK_EQ(ep.playback_position_ticks, 6000000000LL);
  CHECK(contains(t.requests[1].url, "/Users/user-1/Items/Resume?"));
  CHECK(contains(t.requests[1].url, "mediaTypes=Video"));
  CHECK(contains(t.requests[1].url, "limit=12"));

  // Route missing (404): fall back to /UserItems/Resume.
  MockTransport t2;
  t2.queue_response(200, R"({"Id":"user-1","Name":"alice"})");
  t2.queue_response(404, "");
  t2.queue_response(200, R"({"Items":[],"TotalRecordCount":0})");
  api::JellyfinClient c2(t2, "http://srv:8096", "dev");
  c2.restore_session("tok");
  api::ItemList empty;  // parsing appends to the list: new list
  CHECK(c2.fetch_resume(5, &empty).ok());
  CHECK(empty.items.empty());
  CHECK(contains(t2.requests[2].url, "/UserItems/Resume?userId=user-1&"));
}

static void test_fetch_image() {
  MockTransport t;
  t.queue_response(200, R"({"Id":"u","Name":"a"})");
  t.queue_response(200, std::string("\xFF\xD8\xFF\x00jpeg", 8));
  t.queue_response(404, "");
  t.queue_response(200, "");
  api::JellyfinClient client(t, "http://srv:8096", "dev");
  client.restore_session("tok");
  std::string bytes;
  CHECK(client.fetch_image("m1", "tag9", 240, &bytes).ok());
  CHECK_EQ(bytes.size(), 8u);  // binary, zero included
  CHECK(contains(t.requests[1].url, "/Items/m1/Images/Primary?maxWidth=240"));
  CHECK(contains(t.requests[1].url, "tag=tag9"));
  CHECK(contains(header_value(t.requests[1], "Authorization"), "Token=\"tok\""));
  CHECK(!client.fetch_image("m2", "t", 240, &bytes).ok());  // 404
  CHECK(!client.fetch_image("m3", "t", 240, &bytes).ok());  // vide
  api::JellyfinClient anonymous(t, "http://srv:8096", "dev");
  CHECK(anonymous.fetch_image("m1", "t", 240, &bytes).error ==
        api::ApiError::NotAuthenticated);
}

static void test_items_query() {
  api::ItemsQuery q;
  q.parent_id = "lib";
  q.recursive = true;
  q.include_item_types = "Movie,Series";
  q.sort_by = "DateCreated";
  q.sort_descending = true;
  q.limit = 16;
  const std::string url = api::build_items_url("http://srv:8096", "u1", q);
  CHECK(contains(url, "sortBy=DateCreated&sortOrder=Descending"));
  CHECK(contains(url, "fields=Overview,ProductionYear,SortName"));
  CHECK(contains(url, "&limit=16"));
  // Unchanged default: ascending SortName sort, no limit.
  const std::string plain = api::build_items_url("http://srv:8096", "u1", api::ItemsQuery());
  CHECK(contains(plain, "sortBy=SortName&sortOrder=Ascending"));
  CHECK(!contains(plain, "limit="));
}

static void test_parse_user() {
  api::AuthSession s;
  CHECK(api::parse_user_response(R"({"Id":"u9","Name":"bob"})", &s));
  CHECK(s.user_id == "u9" && s.user_name == "bob");
  api::AuthSession bad;
  CHECK(!api::parse_user_response(R"({"Name":"x"})", &bad));
  CHECK(!api::parse_user_response("<html>", &bad));
  // SortName kept as is (cleaning happens at display and sort time).
  api::ItemList list;
  CHECK(api::parse_items_response(
      "{\"Items\":[{\"Id\":\"1\",\"Name\":\"\xE2\x80\x8E" "Amélie\","
      "\"SortName\":\"\xE2\x80\x8E" "amelie\"}]}",
      &list));
  CHECK_EQ(list.items[0].sort_name, "\xE2\x80\x8E" "amelie");
}

int main() {
  test_restore_session();
  test_logout();
  test_resume();
  test_fetch_image();
  test_items_query();
  test_parse_user();
  return testfw::test_failures();
}
