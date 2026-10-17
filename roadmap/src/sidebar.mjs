// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Builds the sidebar from the content folders: for each part, every topic's spec ("How it works",
// how/<part>/<page>.md) followed by its checklist (<part>/<page>.md), in `sidebar.order`.
// Read straight from the files, since astro.config runs before content collections exist.
import { readdirSync, readFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const docs = fileURLToPath(new URL('./content/docs/', import.meta.url));

function frontmatter(file) {
	const text = readFileSync(file, 'utf8');
	const block = text.match(/^---\r?\n([\s\S]*?)\r?\n---/)?.[1] ?? '';
	const title = block.match(/^title:\s*(.+)$/m)?.[1]?.trim().replace(/^["']|["']$/g, '') ?? '';
	const order = Number(block.match(/^\s+order:\s*(\d+)/m)?.[1] ?? 999);
	return { title, order };
}

function pages(dir) {
	const path = join(docs, dir);
	if (!existsSync(path)) {
		return [];
	}
	return readdirSync(path)
		.filter((name) => /\.mdx?$/.test(name))
		.map((name) => {
			const slug = name.replace(/\.mdx?$/, '');
			return { slug, ...frontmatter(join(path, name)) };
		});
}

// One part: topics in order, each its spec then its checklist (either may be missing).
export function partItems(dir) {
	const specs = new Map(pages(`how/${dir}`).map((page) => [page.slug, page]));
	const checklists = new Map(pages(dir).map((page) => [page.slug, page]));
	const slugs = [...new Set([...specs.keys(), ...checklists.keys()])];
	const order = (slug) => (specs.get(slug) ?? checklists.get(slug)).order;
	slugs.sort((a, b) => order(a) - order(b));
	return slugs.flatMap((slug) => {
		const spec = specs.get(slug);
		const checklist = checklists.get(slug);
		const items = [];
		if (spec) {
			items.push({ label: spec.title, slug: `how/${dir}/${slug}` });
		}
		if (checklist) {
			items.push({
				label: spec ? 'Checklist' : checklist.title,
				slug: `${dir}/${slug}`,
				attrs: spec ? { class: 'roadmap-sidebar-checklist' } : {},
			});
		}
		return items;
	});
}

// Concept pages (how/concepts), in order.
export function conceptItems() {
	return pages('how/concepts')
		.sort((a, b) => a.order - b.order)
		.map((page) => ({ label: page.title, slug: `how/concepts/${page.slug}` }));
}
