---
name: Step 9 – Summary and suggested priority
overview: Master ordering of work from the coding-practices deep analysis – quick wins first, then docs, dedup, constants, splits/tests as needed.
todos:
  - id: priority-quick
    content: Do Step 1 (naming) and standardize goto in system_volume.c to cleanup
  - id: priority-docs
    content: Do Step 2 (error-handling doc) and Step 3 (logging doc)
  - id: priority-dedup
    content: Do Step 5 (BarTransformIfShared unification, ALSA helper)
  - id: priority-const
    content: Do Step 4 (extract key magic numbers)
  - id: priority-later
    content: Do Step 6 (file splits) and Step 8 (tests) when modifying those areas
isProject: false
---

# Step 9: Summary and suggested priority

```mermaid
flowchart LR
  subgraph high [High impact, low risk]
    A[Naming: aoplayLock to decoderLock in docs]
    B[Document error-handling and goto convention]
    C[Deduplicate BarTransformIfShared]
    D[Deduplicate ALSA open/find in system_volume]
  end
  subgraph medium [Medium impact]
    E[Extract magic numbers to named constants]
    F[Document logging roles]
  end
  subgraph later [When touching code]
    G[Split socketio.c or ui_act.c]
    H[Add bar_state/player lock tests]
  end
  high --> medium
  medium --> later
```

**Suggested order of work:**

1. **Quick wins:** Align naming (aoplayLock → decoderLock in docs/comments/macros) and standardize goto label in `system_volume.c` to `cleanup`.
2. **Documentation:** Add a short error-handling and logging convention (sections 2 and 3).
3. **Deduplication:** BarTransformIfShared unification and ALSA helper (section 5).
4. **Constants:** Extract a few key magic numbers (section 4).
5. **As needed:** File splits (section 6) and extra tests (section 8) when modifying those areas.

This keeps the codebase consistent and easier to maintain without a large refactor.
