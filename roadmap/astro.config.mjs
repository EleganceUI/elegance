// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

import { sections } from './src/sections.mjs';
import { conceptItems, partItems } from './src/sidebar.mjs';

// Where the site is hosted; see docs/astro.config.mjs.
const site = process.env.ROADMAP_SITE || undefined;
const base = process.env.ROADMAP_BASE || '/';

export default defineConfig({
	site,
	base,
	integrations: [
		starlight({
			title: 'Elegance Roadmap',
			description: 'Everything it takes to build Elegance by hand, as a checklist.',
			logo: { src: './src/assets/logo.svg', replacesTitle: false },
			favicon: '/favicon.svg',
			customCss: ['./src/styles/custom.css'],
			lastUpdated: false,
			pagefind: true,
			tableOfContents: { minHeadingLevel: 2, maxHeadingLevel: 3 },
			expressiveCode: {
				themes: ['github-dark-default', 'github-light-default'],
				styleOverrides: { borderRadius: '0.5rem' },
				defaultProps: { wrap: true, preserveIndent: true },
			},
			components: {
				// Adds the page's progress bar and checklist controls under its title.
				PageTitle: './src/components/PageTitle.astro',
			},
			sidebar: [
				{ label: 'Overview', slug: 'index' },
				{ label: 'Dependency map', slug: 'dependencies' },
				{ label: 'Lessons learned', slug: 'lessons' },
				{ label: 'Concepts', items: conceptItems() },
				// Each part lists its topics: the spec ("how it works"), then its checklist.
				...sections.map(({ dir, label }) => ({ label, items: partItems(dir) })),
			],
		}),
	],
});
