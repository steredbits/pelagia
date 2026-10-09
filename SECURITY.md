# Security policy

## Reporting a vulnerability

Please **do not open a public issue** for a security problem. Report it privately with
GitHub's *Private vulnerability reporting*:

1. Open the **Security** tab of the repository.
2. Click **Report a vulnerability** and describe the problem (direct link:
   <https://github.com/steredbits/pelagia/security/advisories/new>).

Useful details: the Pelagia version (Options > About, or the release tag), what you
did, what you expected, what happened, and how it could be abused. Please do not include
real server addresses, user names, passwords or access tokens: replace them with
placeholders. If a log is needed, check it first (Pelagia masks tokens in its logs, but
a report is not the place to find out that it missed one).

This is a spare-time project: expect an acknowledgement within about a week, and a fix or
a decision as soon as it is reasonably possible. Reporters are credited in the release
notes unless they prefer to stay anonymous.

## Supported versions

Only the latest release receives fixes.

## Scope

In scope: the Pelagia application and the files of this repository (code, build and
release workflows, release archive). Out of scope: vulnerabilities of Jellyfin, of the PS5
firmware or of the jailbreak and homebrew tools it runs on (report those to their own
projects), and of your own network setup. Pelagia talks to your Jellyfin server over plain
HTTP for now (HTTPS is planned): that is a known limitation, not a vulnerability, but
suggestions for hardening it are welcome.
