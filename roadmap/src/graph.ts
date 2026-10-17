// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The dependency graph between subsystems, from the spec pages' `depends` frontmatter. Spec pages
// live at how/<part>/<page>; their checklist twin, when there is one, at <part>/<page>.
import { getCollection, type CollectionEntry } from 'astro:content';
import { sections } from './sections.mjs';

export const SPEC_PREFIX = 'how/';

export interface Node {
	key: string; // "<part>/<page>"
	title: string;
	order: number;
	part: string;
	spec: CollectionEntry<'docs'>;
	checklist?: CollectionEntry<'docs'>;
	depends: string[];
	neededBy: string[];
}

let cached: Map<string, Node> | undefined;

export async function graph(): Promise<Map<string, Node>> {
	if (cached) {
		return cached;
	}
	const entries = await getCollection('docs');
	const byId = new Map(entries.map((entry) => [entry.id, entry]));
	const nodes = new Map<string, Node>();
	for (const entry of entries) {
		// Concept pages explain ideas across parts; they aren't build steps.
		if (!entry.id.startsWith(SPEC_PREFIX) || entry.id.startsWith(`${SPEC_PREFIX}concepts/`)) {
			continue;
		}
		const key = entry.id.slice(SPEC_PREFIX.length);
		nodes.set(key, {
			key,
			title: entry.data.title,
			order: entry.data.sidebar?.order ?? 999,
			part: key.split('/')[0],
			spec: entry,
			checklist: byId.get(key),
			depends: entry.data.depends ?? [],
			neededBy: [],
		});
	}
	for (const node of nodes.values()) {
		for (const dependency of node.depends) {
			const target = nodes.get(dependency);
			if (!target) {
				throw new Error(`${SPEC_PREFIX}${node.key}: depends on "${dependency}", which has no spec page`);
			}
			target.neededBy.push(node.key);
		}
	}
	// A cycle would make the build order meaningless.
	const state = new Map<string, 'visiting' | 'done'>();
	const visit = (key: string, path: string[]) => {
		if (state.get(key) === 'done') return;
		if (state.get(key) === 'visiting') {
			throw new Error(`dependency cycle: ${[...path, key].join(' -> ')}`);
		}
		state.set(key, 'visiting');
		for (const dependency of nodes.get(key)!.depends) visit(dependency, [...path, key]);
		state.set(key, 'done');
	};
	for (const key of nodes.keys()) visit(key, []);
	cached = nodes;
	return nodes;
}

// Nodes sorted by part (in roadmap order), then by sidebar order.
export function ordered(nodes: Map<string, Node>): Node[] {
	const partIndex = new Map(sections.map(({ dir }, i) => [dir, i]));
	return [...nodes.values()].sort(
		(a, b) => (partIndex.get(a.part) ?? 99) - (partIndex.get(b.part) ?? 99) || a.order - b.order,
	);
}

// Every node `key` needs, directly or not.
export function allDependencies(nodes: Map<string, Node>, key: string): Set<string> {
	const result = new Set<string>();
	const walk = (k: string) => {
		for (const dependency of nodes.get(k)?.depends ?? []) {
			if (!result.has(dependency)) {
				result.add(dependency);
				walk(dependency);
			}
		}
	};
	walk(key);
	return result;
}

export function href(id: string): string {
	return `${import.meta.env.BASE_URL.replace(/\/$/, '')}/${id}/`;
}
