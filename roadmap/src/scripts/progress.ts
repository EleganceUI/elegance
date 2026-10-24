// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Checklist progress, kept in this browser's localStorage. Items are identified by a hash of
// their text, so reordering a page keeps its ticks; rewording an item clears that one.

const STORAGE_KEY = 'elegance-roadmap';

export interface Progress {
	version: 1;
	// page id -> item key -> when it was ticked (ISO date)
	pages: Record<string, Record<string, string>>;
	hideDone: boolean;
}

function empty(): Progress {
	return { version: 1, pages: {}, hideDone: false };
}

export function isProgress(value: unknown): value is Progress {
	const v = value as Progress;
	return !!v && v.version === 1 && typeof v.pages === 'object' && v.pages !== null;
}

export function load(): Progress {
	try {
		const raw = localStorage.getItem(STORAGE_KEY);
		if (raw) {
			const parsed: unknown = JSON.parse(raw);
			if (isProgress(parsed)) {
				return { ...empty(), ...parsed };
			}
		}
	} catch {
		// Storage blocked or corrupt: start empty.
	}
	return empty();
}

export function save(progress: Progress): void {
	try {
		localStorage.setItem(STORAGE_KEY, JSON.stringify(progress));
	} catch {
		// Storage blocked: ticks last until the page closes.
	}
}

// FNV-1a, 32-bit, in base 36: short and stable.
function hash(text: string): string {
	let h = 0x811c9dc5;
	for (let i = 0; i < text.length; ++i) {
		h ^= text.charCodeAt(i);
		h = Math.imul(h, 0x01000193);
	}
	return (h >>> 0).toString(36);
}

// The item's own text, without nested lists.
function itemText(item: HTMLElement): string {
	const copy = item.cloneNode(true) as HTMLElement;
	copy.querySelectorAll('ul, ol').forEach((list) => list.remove());
	return (copy.textContent ?? '').replace(/\s+/g, ' ').trim();
}

export interface Item {
	key: string;
	checkbox: HTMLInputElement;
	element: HTMLElement;
}

// Every task-list checkbox in the page's content, with a key unique within the page.
export function collectItems(root: ParentNode): Item[] {
	const seen = new Map<string, number>();
	const items: Item[] = [];
	root.querySelectorAll<HTMLInputElement>('input[type="checkbox"]').forEach((checkbox) => {
		const element = checkbox.closest('li');
		if (!element) {
			return;
		}
		const base = hash(itemText(element));
		const count = seen.get(base) ?? 0;
		seen.set(base, count + 1);
		items.push({ key: count === 0 ? base : `${base}-${count}`, checkbox, element });
	});
	return items;
}

export function doneCount(progress: Progress, page: string): number {
	return Object.keys(progress.pages[page] ?? {}).length;
}
