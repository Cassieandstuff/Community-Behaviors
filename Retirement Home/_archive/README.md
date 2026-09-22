# _archive — retired code, parked not deleted

The leading `_` means SctSources excludes every file here from the build (the project-wide
"`_` = kept in git, visible, NOT compiled" rule) and the orchestrators never `add_subdirectory` it.

Firesale policy (see ~/.claude/plans/havok-core-org-pass-manifest.md): as the typed havok-core
backbone is retired, its files are `git mv`'d HERE rather than hard-deleted — self-documenting where
they went, revivable in place without a git archaeology dig. Each archival is gated: the code must
have no live caller (schema replaced it; the typed fallback branch excised from the converter, the
CLI verb dropped/rehomed) and the build + master regen must stay green after the move.

Layout mirrors the origin path, e.g. _archive/havok-core/src/sct/BehaviorDecompiler.cpp.
