# Changes from upstream RNode Firmware

This repository is **Stolo Node Firmware**, a fork of
[markqvist/RNode_Firmware](https://github.com/markqvist/RNode_Firmware),
licensed under the GNU General Public License v3.0 like its upstream. This
file is the running record of every deliberate difference from the pinned
upstream commit, kept so that a recipient of a Stolo Node can see exactly
what was changed and why (GPL-3.0 §5(a)), and so a rebase onto a newer
upstream is a merge of hooks rather than a merge of logic.

**Upstream pin:** `markqvist/RNode_Firmware` master
`d39339f8ecd5145b248c18bac7b6ea0f82faf85a` (reports firmware version 1.86;
identical to the `1.86` tag except for one README line). The `master` branch
of this repository is that commit, untouched. Stolo work lands on `main`.

Every modified upstream file carries a line of the form
`// Modified by Stolo Systems Inc., <date>` near its licence header; new
files carry a Stolo copyright header under the same licence.

## Changes

| Date | Area | Change | Why |
|---|---|---|---|
| 2026-09-14 | Repo | Added `CHANGES-from-upstream.md`, `STOLO.md`, and a CI workflow that builds the T-Beam Supreme target from the pinned source and publishes the binary with its SHA-256. | GPL kit §4 (reproducible build + release manifest) and the F0 gate: the fork must build the unchanged target before any Stolo change lands. No firmware source is modified by this entry. |
