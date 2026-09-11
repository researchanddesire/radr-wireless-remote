# AGENTS.md

## Technician Build PC

- Use the `$build-pc` skill for the Research + Desire Windows workstation that technicians use to build and flash devices for sale.
- Keep the workstation checkout on a clean, current `main`; preserve unexpected work with a stash or backup before synchronization.
- Repair build-machine tooling, dependencies, and caches there. Reproduce source defects there, but fix tracked code or project configuration in this development repository and land the correction through the normal branch workflow.

## Branch and Release Policy

- Persistent branches are lowercase `staging` and `main`.
- Fetch before branching. Start feature and release work from `origin/staging`, then open the pull request back into `staging`.
- Only urgent production bug fixes may target `main` directly. Start them from `origin/main`, open the pull request into `main`, and make the standalone word `hotfix` the first word of the PR title (case-insensitive).
- Promote normal releases with a same-repository `staging` to `main` pull request only after every required build, unit, integration, artifact-verification, and RADR hardware validation succeeds.
- Immediately merge every main hotfix back into `staging` with a `main` to `staging` pull request.
- Squash feature and hotfix pull requests into one focused commit with a concise, imperative title. Merge long-lived branch synchronization pull requests (`staging` to `main` and `main` to `staging`) with a merge commit; never squash or rebase them.
- Use matching branch names and linked pull requests for changes spanning RAD App or another firmware repository.
- Staging firmware reports the `staging` track and checks `https://staging.researchanddesire.com`. Main firmware reports `main` and checks `https://dashboard.researchanddesire.com`.
- Firmware is update-eligible only after its immutable Supabase artifacts and all required validation records verify. Missing hardware runners leave a release in `validating`; never bypass a gate.

## Memory Rules (RAD-2158)

- The RADR never opens a TLS socket while NimBLE is initialised. NimBLE plus the RAD BLE service cost ~100 KB of internal RAM at boot and a TLS handshake needs ~50 KB in one block; the largest free block with BLE up is ~24 KB. Network jobs about the OSSM (pairing, update) run on the OSSM over a BLE trigger; the RADR's own OTA tears BLE down first.
- Every task on an internal-RAM stack is created through `createInternalTask` (or `startTask`) so an out-of-memory condition logs the heap state and shows on screen. Never call `xTaskCreate*` bare and drop the result.
- Boot logs `[MEM] boot after BLE init: ... largest=...` at error level. A release whose largest block is below `RADR_MIN_LARGEST_BLOCK` (24 KB; measured 31.7 KB at boot on 2026-09-08) on the desk unit must not be promoted; record the number in the release PR.

## Development Safety

- Keep stable eFuse identity, update protocol, filesystem/application ordering, and rollback behavior covered by native tests.
- Never commit credentials, local build products, or generated secrets.
