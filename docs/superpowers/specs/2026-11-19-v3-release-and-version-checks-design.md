# v3.0.0 Coordinated Release and Version-Mismatch Warnings: Design

**Date:** 2026-11-19. **Status:** approved in conversation.

## Intent

The WebSocket simplification changes player behavior that the Home Assistant integration and card rely on: busy refusals, replies going only to the requester, `stop` events, and reconnect timing. The three repos will ship together as one matching major version, so users can tell which releases go together. If they are mixed anyway, users are warned instead of being left with a quietly degraded experience.

## Decisions

- **Hold the release.** Nothing ships to users until the HA integration and card work is done.
- **Order of work:**
  1. remote-pianobar work on `feature/ws-simplification`: logging (#80), S1–S6, version field (S7), leaks, build-time.
  2. Dev build, then real-account testing.
  3. Merge #74 to `main`, with **no release tag**.
  4. HA plan Tasks 1–10 and card Tasks 2b/2c.
  5. Test all three dev builds together.
  6. Release.
- **Version number:** `v3.0.0` for all three repos.
  - Today the tags are remote-pianobar `v2.7.3`, HA `v2.7.0` and card `v2.7.0`.
  - HA `manifest.json` (`1.0.3`) and the card's `package.json` (`1.0.0`) are stale. Both are bumped to `3.0.0` in the release PRs.
- **Dev tags:**
  - remote-pianobar uses `v3.0.0-dev.N`, starting at dev.1. The unpublished `v2.8.0-dev.2` draft is dropped.
  - HA and the card get their own `v3.0.0-dev.N` tags, for installing HACS beta builds side by side.
- **Release day:** tag `v3.0.0` in all three repos. Publish the player first, then HA, then the card. The release notes link to each other.
- **Mismatch warnings:** warn, never refuse.

## Version checks

| Mismatch | Detected by | Where the user sees it |
|---|---|---|
| Player ↔ integration | HA integration | HA Repairs issue plus a log warning |
| Integration ↔ card | Card, in the browser | Dismissible banner inside the card |
| Player ↔ card | None directly | Covered by the two checks above |

### Player (remote-pianobar, server item S7)

- The `process` event gains `"version": "<VERSION>"`. `VERSION` comes from `src/config.h`: `"dev"` for local builds, and the release workflow sets it from the tag.
- `process` is already sent to every client on connect, so this needs no new event.
- Document it in `WEBSOCKET_API.md`. Test it with a unit test on the `process` JSON.
- No user-visible strings.

### HA integration (HA plan Task 10)

- **Parsing:** read `version` from `process` and compare its major version with the integration's own, taken from `manifest.json`.
- **When the majors differ:**
  - create a Repairs issue (`issue_registry.async_create_issue`, translatable, severity `warning`, not fixable) keyed per config entry. Text along the lines of: "remote-pianobar {player_version} is connected, but this integration expects {expected_major}.x. Update both to the same major version."
  - log one warning per connection.
- **No `version` field:** treat as a player older than v3 and warn the same way.
- **`"dev"` or an unparseable value:** no warning, debug log only.
- **When the majors match, or the config entry is removed:** delete the issue.
- **Entity attribute:** expose `integration_version` (from the manifest) on the media player entity, for the card.
- **Strings:** new text goes in `translations/en.json` under `issues`.

### Card (card plan Task 2c)

- **Embed the version:** the card currently embeds only a build timestamp (`BUILD_TIMESTAMP_RAW` in `pianobar-card-editor.ts`). Inject the `package.json` version at build time as a constant.
- **Compare:** check it against the entity's `integration_version` attribute.
- **When the majors differ:**
  - show a small dismissible banner in the card, e.g. "Card {card_version} and integration {integration_version} don't match. Update both to the same major version."
  - dismissal lasts for the browser session.
- **No attribute:** treat as an older integration and warn.
- **No warning** when either version is unparseable.
- **Strings:** new text goes in `src/translations/en.json` via `cardLocalizeFormat`.
- **Stale browser cache:** this check also catches a browser still running a cached old card after an update.

## Approaches considered

- **How the server publishes its version:**
  - **A. Field in `process` (chosen):** one field on an existing connect-time event.
  - B. A new `hello` event: every client has to learn a new event.
  - C. An HTTP `/version` endpoint: needs an extra request and doesn't fit how HA connects.
- **How HA shows the warning:**
  - **A Repairs issue (chosen):** persistent, translatable, and clears itself.
  - A persistent notification: rejected because it nags. It was already avoided for the busy notice.
- **How the card is checked:**
  - **A. In-card banner from an entity attribute (chosen):** reflects what each browser actually loaded.
  - B. The card reports its version to a service, and the integration raises a Repairs issue: rejected. Each device reports its own cached version, so the issue flaps and lingers.

## Non-goals

- Refusing to connect on a mismatch.
- Comparing minor or patch versions.
- A player ↔ card check.

## Left to the builder

- Exact wording of the issue and banner text, within the meaning above.
- How the card build injects its version (rollup replace or define).
- Banner styling. It should match the card's existing notices.
