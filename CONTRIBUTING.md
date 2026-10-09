# Contributing to Pelagia

Thanks for your interest! Pelagia is an unofficial Jellyfin client for jailbroken PS5 consoles. Bug reports,
test results on other setups and pull requests are welcome.

## Reporting a problem

Open an issue with:

- your PS5 firmware, jailbreak and loaded tools, and your Jellyfin server version;
- what you did, what you expected, what happened;
- the log, `/data/homebrew/Pelagia/logs/pelagia.log` (read it over FTP). Tokens and passwords are masked,
  but **remove your server address** and anything else private before posting it.

Never post a token, a password or a link containing an `api_key`.

## Building and testing

See the *Building from source* section of the [README](README.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

Before opening a pull request:

```bash
cmake -B build-linux -DPLATFORM=linux && cmake --build build-linux -j
ctest --test-dir build-linux --output-on-failure   # all tests must pass
./ci/build-sanitize.sh                             # assertions + ASan + UBSan: must pass for core changes
ci/build-ps5.sh                                    # if you touched platform/ps5 or the build (needs the SDK)
python3 tools/spdx_headers.py --check              # licence header on new source files
```

## Guidelines

- **Test what you can on Linux.** The PS5 can't be tested in CI: keep `platform/ps5` as thin as possible and
  put logic in portable code with a unit test (parsing, URLs, player state — not rendering).
- **Don't guess a PS5 API.** If you are unsure about a function or header of the homebrew SDK, say so in the
  pull request and propose a way to check it.
- **Only the open-source homebrew SDK**: no Sony proprietary or leaked libraries, headers or code, and no
  code copied from projects whose license is incompatible with GPL-3.0-or-later.
- `core/` includes only `platform/platform.h`, never a platform-specific header.
- C++17, no exceptions, no RTTI in the core. Keep functions short and modules decoupled; no unrelated
  refactoring in a pull request.
- Logs go through `util/log`, with levels. Never write the token, the password or a URL with credentials.
- The UI never makes a blocking call (network, disk, decoding) in the display loop.
- After a visible UI change, run `tools/update_screenshots.sh` and commit the new screenshots.
- Never commit credentials, tokens or the address of a real server (use `192.168.1.x`-style examples).
- Code, comments, logs, documentation (except `README.fr.md`) and commit messages are in English.
- Texts shown by the interface live in the string catalogs `core/util/i18n/strings_en.def` and
  `strings_fr.def` (same identifiers, same order, same `%s`/`%d` placeholders; `test_i18n` checks
  it). No literal text in the screens: use `util::tr` / `util::trf`.

## Licensing

By contributing you agree that your contribution is licensed under the project's license, GPL-3.0-or-later
(see [LICENSE](LICENSE)). New source files start with the SPDX header used elsewhere in the repository
(`python3 tools/spdx_headers.py --apply` adds it).
