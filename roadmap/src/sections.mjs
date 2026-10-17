// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The roadmap's parts, in build order. Each is a folder of src/content/docs/; pages inside are
// ordered by their `sidebar.order`. Shared by the sidebar (astro.config.mjs) and the progress
// overview (components/Overview.astro).
export const sections = [
	{ dir: 'foundations', label: '1. Foundations' },
	{ dir: 'renderer', label: '2. Renderer' },
	{ dir: 'framework', label: '3. Framework core' },
	{ dir: 'text', label: '4. Text' },
	{ dir: 'controls', label: '5. Controls and windows' },
	{ dir: 'graphics', label: '6. Paths, icons, and images' },
	{ dir: 'media', label: '7. Media and networking' },
	{ dir: 'system', label: '8. System integration' },
	{ dir: 'tools', label: '9. Developer tools and quality' },
	{ dir: 'beyond', label: '10. Beyond the current code' },
];
