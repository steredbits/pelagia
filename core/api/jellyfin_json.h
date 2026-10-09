// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 KevinJCode and Pelagia contributors
#ifndef PELAGIA_CORE_API_JELLYFIN_JSON_H
#define PELAGIA_CORE_API_JELLYFIN_JSON_H

// cJSON parsing of Jellyfin API responses: pure functions,
// tested with mocked responses. They return false if the JSON is
// invalid or a required field is missing.

#include <string>
#include <vector>

#include "api/jellyfin_models.h"

namespace api {

// Response of POST /Users/AuthenticateByName (AuthenticationResult).
bool parse_auth_response(const std::string& json, AuthSession* out);

// Response of GET /Users/Me (UserDto): Id, Name (the token is unchanged).
bool parse_user_response(const std::string& json, AuthSession* out);

// Response of GET /Users/{userId}/Views (list of libraries).
bool parse_views_response(const std::string& json, std::vector<Library>* out);

// Response of GET .../Items, /Shows/{id}/Seasons or /Shows/{id}/Episodes
// (BaseItemDtoQueryResult : Items + TotalRecordCount).
bool parse_items_response(const std::string& json, ItemList* out);

// Response of GET /Users/{userId}/Items/{itemId} (a BaseItemDto, with
// UserData and MediaSources).
bool parse_item_response(const std::string& json, MediaItem* out);

// Serializes the body of the authentication request {"Username", "Pw"}.
std::string build_auth_body(const std::string& username, const std::string& password);

}  // namespace api

#endif  // PELAGIA_CORE_API_JELLYFIN_JSON_H
