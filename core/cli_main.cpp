// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 steredbits and Pelagia contributors
// pelagia-cli: API validation tool (and a way to find the identifiers
// for pelagia-play --item).
// Authenticates against the Jellyfin server and lists the libraries, movies
// and series, each with its identifier; --episodes <series-id> lists the
// episodes of a series (only the identifier of a movie or an episode is
// playable). Configuration through environment variables:
//   JELLYFIN_URL   http://ip:8096
//   JELLYFIN_USER  nom d'utilisateur
//   JELLYFIN_PASS  password
// The listing goes to stdout (it is the program's result),
// diagnostics go through util/log.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "api/http_curl.h"
#include "api/jellyfin_client.h"
#include "api/jellyfin_urls.h"
#include "util/log.h"

namespace {

const char* env_or_null(const char* name) {
    const char* value = std::getenv(name);
    return (value && value[0] != '\0') ? value : nullptr;
}

// API error on a single line; the HTTP code is only shown if there is one
// (0 = transport error, the message is enough).
void log_api_error(const char* prefix, const api::ApiResult& result) {
    if (result.http_status > 0) {
        LOG_ERROR("%s : %s (HTTP %ld)", prefix, result.message.c_str(),
                  result.http_status);
    } else {
        LOG_ERROR("%s : %s", prefix, result.message.c_str());
    }
}

void print_runtime(long long ticks) {
    // 1 Jellyfin tick = 100 ns -> 600,000,000 ticks per minute.
    long long minutes = ticks / 600000000LL;
    if (minutes > 0) {
        std::printf(" — %lldh%02lld", minutes / 60, minutes % 60);
    }
}

// One line per item, identifier at the end of the line: this is the value to give to
// pelagia-play --item. The duration is only shown for a playable media
// (a series' duration makes no sense).
void print_item(const api::MediaItem& item, bool with_runtime) {
    std::printf("  - %s", item.name.c_str());
    if (item.production_year > 0) {
        std::printf(" (%d)", item.production_year);
    }
    if (with_runtime) {
        print_runtime(item.runtime_ticks);
    }
    std::printf("  [%s]\n", item.id.c_str());
}

void print_usage(std::FILE* out) {
    std::fputs(
        "Usage: pelagia-cli [--verbose] [--episodes <series-id>]\n"
        "\n"
        "Lists the libraries, movies and series of the server, each with its\n"
        "identifier (in brackets) to give to pelagia-play --item <id>.\n"
        "  --episodes <id>  lists the episodes of series <id> (with their\n"
        "                   identifiers: a series identifier is not playable)\n"
        "  -v, --verbose    detailed logs\n"
        "  -h, --help       shows this help\n"
        "\n"
        "Variables d'environnement : JELLYFIN_URL, JELLYFIN_USER, JELLYFIN_PASS.\n"
        "Exit codes: 0 success, 1 server error, 2 arguments or configuration\n"
        "incorrects.\n",
        out);
}

// Episodes of a series: "S01E03  Title  [id] - 0h42".
int list_episodes(api::JellyfinClient* client, const char* series_id) {
    api::ItemList episodes;
    const api::ApiResult result = client->fetch_episodes(series_id, "", &episodes);
    if (!result.ok()) {
        log_api_error("Failed to list the episodes", result);
        return 1;
    }
    std::printf("Episodes (%zu):\n", episodes.items.size());
    for (const api::MediaItem& ep : episodes.items) {
        if (ep.parent_index_number >= 0 && ep.index_number >= 0) {
            std::printf("  S%02dE%02d  %s", ep.parent_index_number, ep.index_number,
                        ep.name.c_str());
        } else {
            std::printf("  %s", ep.name.c_str());
        }
        print_runtime(ep.runtime_ticks);
        std::printf("  [%s]\n", ep.id.c_str());
    }
    if (!episodes.items.empty()) {
        std::printf("\nTo play an episode: pelagia-play --item <id>\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Help takes precedence over any other argument error.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(stdout);
            return 0;
        }
    }
    const char* episodes_of = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0) {
            util::log_set_level(util::LogLevel::Debug);
        } else if (std::strcmp(argv[i], "--episodes") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                std::fprintf(stderr, "--episodes expects the identifier of a series\n");
                return 2;
            }
            episodes_of = argv[++i];
        } else {
            std::fprintf(stderr, "Unknown option: %s\nTry: pelagia-cli --help\n",
                         argv[i]);
            return 2;
        }
    }

    const char* url = env_or_null("JELLYFIN_URL");
    const char* user = env_or_null("JELLYFIN_USER");
    const char* pass = env_or_null("JELLYFIN_PASS");
    if (!url || !user || !pass) {
        std::fprintf(stderr,
                     "Variables d'environnement requises : JELLYFIN_URL, "
                     "JELLYFIN_USER, JELLYFIN_PASS\n"
                     "Exemple :\n"
                     "  JELLYFIN_URL=http://192.168.1.10:8096 JELLYFIN_USER=moi \\\n"
                     "  JELLYFIN_PASS=secret ./build-linux/pelagia-cli\n");
        return 2;
    }

    api::HttpCurlTransport transport;
    api::JellyfinClient client(transport, url);

    api::ApiResult result = client.authenticate(user, pass);
    if (!result.ok()) {
        log_api_error("Authentication failed", result);
        return 1;
    }
    std::printf("Connected to %s as %s\n\n", client.server_url().c_str(),
                client.session().user_name.c_str());

    if (episodes_of) {
        return list_episodes(&client, episodes_of);
    }

    std::vector<api::Library> libraries;
    result = client.fetch_libraries(&libraries);
    if (!result.ok()) {
        log_api_error("Failed to list the libraries", result);
        return 1;
    }
    std::printf("Libraries (%zu):\n", libraries.size());
    for (const api::Library& lib : libraries) {
        std::printf("  - %s [%s]\n", lib.name.c_str(),
                    lib.collection_type.empty() ? "?" : lib.collection_type.c_str());
    }

    api::ItemsQuery query;
    query.include_item_types = "Movie";
    query.recursive = true;
    // Without this parameter, the server replaces the movies of a collection with
    // their BoxSet (no MediaSource, so not playable) and hides others.
    query.collapse_box_set_items = false;
    api::ItemList movies;
    result = client.fetch_items(query, &movies);
    if (!result.ok()) {
        log_api_error("Failed to list the movies", result);
        return 1;
    }

    // Safeguard: only count and display real movies.
    int movie_count = 0;
    for (const api::MediaItem& item : movies.items) {
        if (item.type == "Movie") {
            ++movie_count;
        }
    }
    std::printf("\nFilms (%d) :\n", movie_count);
    const api::MediaItem* first_movie = nullptr;
    for (const api::MediaItem& movie : movies.items) {
        if (movie.type != "Movie") {
            continue;
        }
        if (!first_movie) {
            first_movie = &movie;
        }
        print_item(movie, true);
    }

    api::ItemsQuery series_query;
    series_query.include_item_types = "Series";
    series_query.recursive = true;
    api::ItemList series;
    result = client.fetch_items(series_query, &series);
    if (!result.ok()) {
        log_api_error("Failed to list the series", result);
        return 1;
    }
    int series_count = 0;
    for (const api::MediaItem& item : series.items) {
        if (item.type == "Series") {
            ++series_count;
        }
    }
    std::printf("\nSeries (%d):\n", series_count);
    for (const api::MediaItem& item : series.items) {
        if (item.type == "Series") {
            print_item(item, false);
        }
    }

    if (first_movie) {
        // The token goes through the Authorization header (pelagia-play default):
        // the URL does not contain it. mask_api_key() stays there as a precaution.
        std::printf("\nExample stream URL (H.264 1080p + AAC transcoding; the token"
                    "goes through the Authorization header, not the URL):\n%s\n",
                    api::mask_api_key(client.stream_url(first_movie->id, 0, "", false)).c_str());
    }
    if (first_movie || series_count > 0) {
        std::printf("\nPour lire un film : pelagia-play --item <id>\n"
                    "To list the episodes of a series: pelagia-cli --episodes <id>\n");
    }
    return 0;
}
