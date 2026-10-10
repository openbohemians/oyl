# Project memory

Notes that AI agents working on Oyl keep for each other: status, plans,
decisions in progress and lessons that are not obvious from the code or the
git history. Read these before starting work; update them when something
here changes. `../config.yml` names this project's agent on the Arcana bus:
`@oyl`.

`.ai/` is a git worktree of the repository's orphan `ai` branch, ignored by
the code checkout (`git worktree add .ai ai` checks it out). Run git against
it (`git -C .ai ...`), stage by path, never `git add -A`, and push it by
name (`git -C .ai push origin ai`). The repository is public, so the branch
is too. Settled knowledge that anyone working on the code needs belongs in
the project's own docs, not here.

Until 2026-10-10 the notes lived on `main` under `.ai/`, and this branch
was split from that history. Older citations such as
`git show <commit>:.ai/inbox/<file>` name commits on `main`.

| File | What it holds |
|---|---|
| [release.md](release.md) | 1.0 status, the clean-fuzz rule, and the tagging checklist |
| [fuzzing.md](fuzzing.md) | ClusterFuzzLite, the "fuzzies parsed" tally, finding and fixing crashes, CI habits |
| [performance.md](performance.md) | How to measure, where Oyl stands, and the post-1.0 performance plan |
| [naming.md](naming.md) | The yam → Oyl rename and the move to openbohemians |
| [ideas.md](ideas.md) | Larger features agreed in principle but not started: tree API, 1.1 → 1.2 converter |
| [outreach.md](outreach.md) | GitHub Sponsors, and the plan to reach heavy YAML users after the parallel release |
| [perdoc-fallback-prototype.patch](perdoc-fallback-prototype.patch) | Prototype of per-document eager fallback (release.md issue 3), against `d6b8afb` |

Keep each note short and dated (`*Updated YYYY-MM-DD*`), with absolute
dates and commit hashes. Check the code or `git log` before acting on
anything here; notes go stale. Machine-specific facts don't belong here.
Never commit credentials or keys.

## Handling inbox notes

Messages from other agents land in `.ai/mail/inbox/`. A note in the inbox
has not been dealt with yet, so the inbox holds only unprocessed notes. To
process one:

1. Commit the note as it arrived, before anything else, so the original is
   kept in history.
2. Put what matters into the memory notes here, in your own words.
3. Delete the note in a following commit, and cite the commit that still
   has it (`git -C .ai show <commit>:mail/inbox/<file>`).

If a note is reference material that will be consulted again, such as a
spec or a design, move it here as its own file instead of summarizing it.
The branch is public: a note with private details should be summarized
without committing the original.

## Outbound mail

The lead agent (the session holding the Arcana handle) writes mail on the
user's behalf, one file per message named by its address:
`<handle>--<YYYY-MM-DD>-<topic>.md` (`@datadungeon--2026-10-10-schema.md`).

- Routine mail (status, notices of things done, answers from settled facts,
  requests for information) goes straight to `.ai/mail/outbox/`. The
  delivery process sends it and moves it to `mail/sent/`, the committed
  record of what was said.
- Anything that commits the user or the project, makes a decision the user
  hasn't made, pushes back, shares something private, or that the user
  asked to see goes to `mail/drafts/` with a `Held: <why>` line, and waits
  for the user's approval. When unsure, hold. `mail.review: all` in
  `../config.yml` holds everything.
- Helpers (subagents, other sessions) never send: they put messages in
  `mail/drafts/` with a `By: <who>` line, and the lead reviews them like
  its own.
- Drafts and the outbox are machine-local and never committed. Never write
  into another project's `.ai/`.

## Transcripts

`.ai/transcript/` holds session transcripts written by the agent runtime,
one tagged entry per event (`<E1043 …>…</E1043>`), always redacted before
they're committed, and encrypted with git-crypt if `transcript.encrypt` is
set. Read them, never edit them; find an entry with
`grep -rn '^<E1043 ' .ai/transcript`. Files that look like binary mean the
worktree is locked (`cd .ai && git-crypt unlock`).

## Intake

The user shares files for the work at hand in `.ai/intake/`. Look there
when starting a session. The files are the user's: don't commit, move or
delete them unless asked. `.ai/.gitignore` keeps them out of commits.

## Arcana

Outbound Arcana messages need the user's go-ahead, message by message:
draft and offer, then wait. Reading and receiving mail is fine.
