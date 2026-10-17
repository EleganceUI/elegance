// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Where the site is hosted. For GitHub Pages under a repository, set DOCS_SITE to
// https://<user>.github.io and DOCS_BASE to /<repository> (the workflow in .github does).
const site = process.env.DOCS_SITE || undefined;
const base = process.env.DOCS_BASE || '/';

export default defineConfig({
	site,
	base,
	integrations: [
		starlight({
			title: 'Elegence',
			description: 'Declarative, reactive UI for modern C++, drawn with Vulkan.',
			logo: { src: './src/assets/logo.svg', replacesTitle: false },
			favicon: '/favicon.svg',
			customCss: ['./src/styles/custom.css'],
			lastUpdated: false,
			tableOfContents: { minHeadingLevel: 2, maxHeadingLevel: 3 },
			expressiveCode: {
				themes: ['github-dark-default', 'github-light-default'],
				styleOverrides: { borderRadius: '0.5rem' },
				defaultProps: { wrap: true, preserveIndent: true },
			},
			sidebar: [
				{
					label: 'Start here',
					items: [
						{ label: 'Introduction', slug: 'start/introduction' },
						{ label: 'Installation', slug: 'start/installation' },
						{ label: 'Your first app', slug: 'start/first-app' },
						{ label: 'Thinking in Elegence', slug: 'start/concepts' },
					],
				},
				{
					label: 'Essentials',
					items: [
						{ label: 'Views and modifiers', slug: 'guides/views-and-modifiers' },
						{ label: 'Layout', slug: 'guides/layout' },
						{ label: 'State and data flow', slug: 'guides/state' },
						{ label: 'Lists and dynamic content', slug: 'guides/lists' },
						{ label: 'Text, fonts, and icons', slug: 'guides/text-and-icons' },
						{ label: 'Controls', slug: 'guides/controls' },
						{ label: 'Colors and themes', slug: 'guides/themes' },
						{ label: 'Shapes and drawing', slug: 'guides/drawing' },
						{ label: 'Animation', slug: 'guides/animation' },
						{ label: 'Input, focus, and gestures', slug: 'guides/input' },
						{ label: 'Menus and popups', slug: 'guides/menus' },
						{ label: 'Windows', slug: 'guides/windows' },
					],
				},
				{
					label: 'Media and the system',
					items: [
						{ label: 'Images', slug: 'guides/images' },
						{ label: 'Audio', slug: 'guides/audio' },
						{ label: 'Video', slug: 'guides/video' },
						{ label: 'Microphone and camera', slug: 'guides/capture' },
						{ label: 'Networking', slug: 'guides/networking' },
						{ label: 'Desktop integration', slug: 'guides/desktop' },
					],
				},
				{
					label: 'Going further',
					items: [
						{ label: 'Components and modifiers', slug: 'guides/custom-views' },
						{ label: 'Custom layouts', slug: 'guides/custom-layouts' },
						{ label: 'Control styles', slug: 'guides/styles' },
						{ label: 'The environment', slug: 'guides/environment' },
						{ label: 'Accessibility', slug: 'guides/accessibility' },
						{ label: 'Hot reload and previews', slug: 'guides/hot-reload' },
						{ label: 'Debugging and errors', slug: 'guides/debugging' },
					],
				},
				{ label: 'Examples', slug: 'examples' },
				{ label: 'API reference', collapsed: true, items: [{ autogenerate: { directory: 'reference' } }] },
			],
		}),
	],
	vite: {
		// The guides show the examples' sources from ../examples.
		server: { fs: { allow: ['..'] } },
	},
});
