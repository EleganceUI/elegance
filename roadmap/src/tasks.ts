// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The number of task-list items (`- [ ] ...`) in a page's Markdown, at build time.
export function countTasks(markdown: string): number {
	return markdown.match(/^\s*[-*+] \[[ xX]\] /gm)?.length ?? 0;
}
