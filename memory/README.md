# Project memory

Notes that AI agents working on Oyl keep for each other: decisions, plans,
and lessons that are not obvious from the code or the git history. Read
these before starting work; update them when something here changes.
`../config.yml` names this project's agent on the Arcana bus: `@oyl`.

| File | What it holds |
|---|---|
| [release.md](release.md) | 1.0 status, the clean-fuzz rule, and the tagging checklist |
| [fuzzing.md](fuzzing.md) | ClusterFuzzLite, the "fuzzies parsed" tally, finding and fixing crashes, CI habits |
| [performance.md](performance.md) | How to measure, where Oyl stands, and the post-1.0 performance plan |
| [naming.md](naming.md) | The yam → Oyl rename and the move to openbohemians |
| [ideas.md](ideas.md) | Larger features agreed in principle but not started: tree API, 1.1 → 1.2 converter |
| [outreach.md](outreach.md) | GitHub Sponsors, and the plan to reach heavy YAML users after the parallel release |
| [perdoc-fallback-prototype.patch](perdoc-fallback-prototype.patch) | Prototype of per-document eager fallback (release.md issue 3), against `d6b8afb` |

Keep each note short and dated. Check the code or `git log` before acting on
anything here; notes go stale.

## Handling inbox notes

Other agents leave notes in `.ai/inbox/`. A note in the inbox has not been
dealt with yet, so the inbox holds only unprocessed notes. To process one:

1. Commit the note as it arrived, before anything else, so the original is
   kept in history.
2. Put what matters into the memory notes here, in your own words.
3. Delete the note in a following commit, and cite the commit that still
   has it (`git show <commit>:.ai/inbox/<file>`).

If a note is reference material that will be consulted again, such as a
spec or a design, move it here as its own file instead of summarizing it.
The repo is public: a note with private details should be summarized
without committing the original.

## Intake

The user shares files for the work at hand in `.ai/intake/`. Look there
when starting a session. The files are the user's: don't commit, move or
delete them unless asked.

## Arcana

Outbound Arcana messages need the user's go-ahead, message by message:
draft and offer, then wait. Reading and receiving mail is fine.
