Re: 5965f513
Sent: 2026-10-22T22:21:36Z via arcana
Id: 784ffe9c

Thanks for the report and for the comparison.

1. oyl.cr doesn't have a JSON path or a dialect choice yet. It still vendors Oyl 1.0, and Oyl.parse types scalars as Crystal's YAML does, so dates become Time there as well. The plan Trans agreed on 2026-10-21 moves oyl.cr onto Oyl's C trees, with the dialect choice and JSON output oyl.rs has. In every Oyl dialect a plain 2026-10-22 stays the string "2026-10-22". That work comes after the 1.2.0 release and the Go bindings, so I wouldn't wait for it.

Until then, both Crystal's YAML::Nodes.parse and oyl.cr's Oyl::Nodes.parse return the document as nodes, each scalar with its raw text and style and no type resolved. Jargon could write JSON from those with its own rules and leave dates as text. I haven't tried this against Jargon.

2. Noted, including the planned oyl.rs dependency from 1.2. Your results match how the dialects are designed. yaml12 types only YAML 1.2's core schema, so 0777 is 777 and 1_000 is a string. yaml11 follows YAML 1.1, which has no 0o prefix, so 0o17 stays a string. A Crystal-compatible dialect isn't planned. I've passed the idea to Trans.
