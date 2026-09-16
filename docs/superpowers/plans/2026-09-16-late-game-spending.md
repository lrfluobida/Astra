# Late-game Spending Implementation Plan

**Goal:** Complete the authorized weapon → front wall → station upgrade sequence, then spend surplus gold on enemy-wave summon orders.

**Policy:** Preserve three rockets, rear-first weapon upgrades, and the front half-wall. Once rockets are all level 3 and the half-wall is built, upgrade each front wall to level 3 in the layout's pressure-facing order, then the station. Missing or downgraded defenses preempt offense. At full defense levels and with a living observed enemy station, retain 300 gold and choose a large/BOSS mix maximizing added HP, then attack, under actual shop prices, the ten-order daily cap, inventory, and usable daylight. Use held orders first; buy only quantities that can be used before return. No spending on small/medium summons. This is a deterministic pressure heuristic, not a measured win-rate optimum.

**Implementation:** Reuse the building voucher purchase/use path. Add internal per-day summon accounting to the validated session, with conservative reservations and explicit next-round failure refunds. Share exact item classification and enforce voucher targets and the summon cap in arbitration. All runtime policy is C++; no extra platform model calls.

**Work allocation:** Main session implements strategy and integration. One gpt-5.6-sol worker handles action validation/session accounting; gpt-6-astra reviews the combined change.

- [x] Add failing strategy tests for upgrade order, front-wall ordering, resource fallback, full-defense gating, large/BOSS allocation, existing inventory, daylight cutoff, and enemy-station state.
- [x] Extend building upgrade selection and add bounded daytime summon purchase/use planning with one worker.
- [x] Add and verify voucher validation and global daily summon accounting, including retry/failure/day reset cases.
- [x] Update README, run Debug/Release/Sanitizer suites, and verify extracted submission package build/HTTP behavior.
- [x] Regenerate the submission archive and review changes before delivery.

**Validation:** Debug, Release, and Address/UndefinedBehavior Sanitizer suites each passed all 87 C++ tests, HTTP checks, 1300-round replay, and package checks. The extracted archive compiled in an SDK-style sibling directory layout and passed a direct HTTP task-pipeline check. Code review approved the final change.

**Delivery:** Commit and push the existing branch; report any network failure separately.
