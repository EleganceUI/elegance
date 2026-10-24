// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Generates the API reference (src/content/docs/reference/) from the public headers in
// include/elg/. Each header becomes a page: its top comment as the introduction, then each
// declaration with the comment above it. Implementation details are left out: `detail`
// namespaces, private and protected members, the bodies of function templates, and internal
// macros. Run by `npm run dev` and `npm run build`; the output isn't checked in.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const includeDir = path.resolve(here, '../../include/elg');
const outDir = path.resolve(here, '../src/content/docs/reference');

// One line per header for the index, in reading order.
const headers = [
	['elegance.h', 'The umbrella header: includes everything below.'],
	['app.h', 'App, windows, and the event loop.'],
	['window.h', 'The window a view is in; custom title bars and window controls.'],
	['view.h', 'View, the modifier methods, and the layout value types.'],
	['views.h', 'Stacks, controls, If, ForEach, scroll views, lists, and menus.'],
	['text.h', 'Text.'],
	['font.h', 'Fonts, weights, text styles, and custom fonts.'],
	['color.h', 'Color.'],
	['theme.h', 'Themes and semantic colors.'],
	['reactive.h', 'State, Binding, Computed, effects, and batching.'],
	['prop.h', 'Reactive arguments.'],
	['environment.h', 'Environment values and keys.'],
	['layout.h', 'The layout protocol, cached layouts, and the stack layouts.'],
	['grid.h', 'Grids: VGrid and HGrid tracks, and Grid tables of GridRows.'],
	['modifier.h', 'The modifier protocols, for custom modifiers.'],
	['modifiers/layout.h', 'Built-in layout modifiers.'],
	['modifiers/decoration.h', 'Built-in decoration modifiers.'],
	['modifiers/effects.h', 'Built-in effect modifiers.'],
	['modifiers/environment.h', 'Built-in environment modifiers.'],
	['modifiers/events.h', 'Built-in event modifiers.'],
	['shapes.h', 'Shapes: rectangles, circles, capsules, paths, and custom shapes.'],
	['path.h', 'Vector paths and stroke styles.'],
	['gradient.h', 'Linear and radial gradients, and ShapeStyle.'],
	['graphics_context.h', 'Immediate-mode drawing in a Canvas.'],
	['geometry.h', 'Point, Size, Rect, and corner radii.'],
	['styles.h', 'Button, toggle, and text field styles.'],
	['animation.h', 'Animations and transitions.'],
	['events.h', 'Pointer, keyboard, and text input events; custom event handlers.'],
	['image.h', 'Images, icons, icon sets, and AsyncImage.'],
	['audio.h', 'Sound effects and audio playback.'],
	['video.h', 'Video playback.'],
	['capture.h', 'Recording from the microphone and the camera.'],
	['http.h', 'HTTP requests.'],
	['desktop.h', 'File dialogs, notifications, and the tray icon.'],
	['hot_reload.h', 'Hot reload and previews.'],
	['error.h', 'The exceptions Elegance throws.'],
];

const slugOf = (header) => header.replace(/\.h$/, '').replace(/\//g, '-');

// --- Small C++ scanning helpers ---

// The line without its // comment and string or character literals (for counting braces).
function codeOnly(line) {
	let out = '';
	for (let i = 0; i < line.length; i++) {
		const c = line[i];
		if (c === '/' && line[i + 1] === '/') break;
		if (c === '"' || c === "'") {
			// R"x(...)x" raw strings don't occur in the public headers.
			const quote = c;
			i++;
			while (i < line.length && line[i] !== quote) {
				if (line[i] === '\\') i++;
				i++;
			}
			continue;
		}
		out += c;
	}
	return out;
}

const braceDelta = (line) => {
	const code = codeOnly(line);
	return (code.match(/\{/g) || []).length - (code.match(/\}/g) || []).length;
};

const isComment = (line) => line.trim().startsWith('//');

// --- Cleaning a header ---

function clean(text) {
	let lines = text.replace(/\r\n/g, '\n').split('\n');

	// The license header, include guard, includes, and pragmas.
	while (lines.length && lines[0].startsWith('//')) lines.shift();
	lines = lines.filter(
		(l) =>
			!/^#(ifndef|define) ELG_\w+_H\s*$/.test(l) &&
			!/^#endif \/\/ ELG_\w+_H/.test(l) &&
			!/^#include /.test(l) &&
			!/^#pragma /.test(l),
	);

	const out = [];
	for (let i = 0; i < lines.length; i++) {
		const line = lines[i];
		const trimmed = line.trim();

		// Blocks that are implementation: `namespace detail { ... }`.
		if (/^namespace detail \{/.test(trimmed)) {
			let depth = braceDelta(line);
			while (depth > 0 && ++i < lines.length) depth += braceDelta(lines[i]);
			continue;
		}
		// Platform and compiler conditionals (export macros, warning pragmas).
		if (/^#if(def)? (_MSC_VER|defined\(_WIN32\)|ELG_VIEW_EXTENSIONS)/.test(trimmed)) {
			let nesting = 1;
			while (nesting > 0 && ++i < lines.length) {
				if (/^#if/.test(lines[i].trim())) nesting++;
				if (/^#endif/.test(lines[i].trim())) nesting--;
			}
			continue;
		}
		// Macros: public ones by their first line only; internal ones (ending in _ or
		// CONCAT/EXPORT) not at all.
		if (/^#define /.test(trimmed)) {
			const name = trimmed.match(/^#define (\w+)/)[1];
			const internal = /_$|CONCAT|EXPORT/.test(name);
			let first = line.replace(/\s*\\$/, '');
			while (lines[i].trimEnd().endsWith('\\') && i + 1 < lines.length) i++;
			if (!internal) out.push(first.replace(/^(#define \w+(\([^)]*\))?).*$/, '$1'));
			continue;
		}
		// Namespace lines themselves (the content stays).
		if (/^namespace elg(::\w+)? \{$/.test(trimmed) || /^\} \/\/ namespace elg/.test(trimmed)) continue;
		if (/ELG_PRIVATE_MEMBERS_(BEGIN|END)/.test(trimmed)) continue;
		if (/^static_assert\(/.test(trimmed)) {
			while (!lines[i].trimEnd().endsWith(';') && i + 1 < lines.length) i++;
			continue;
		}
		out.push(line.replace(/\bELG_API /g, ''));
	}
	return stripPrivate(out);
}

// Removes private and protected sections and friend declarations from class bodies.
function stripPrivate(lines) {
	const out = [];
	let depth = 0;
	let skipping = -1; // the depth whose private section is being skipped
	for (const line of lines) {
		const trimmed = line.trim();
		if (skipping >= 0) {
			if (depth === skipping && trimmed === 'public:') {
				skipping = -1;
				out.push(line);
			} else if (depth + braceDelta(line) < skipping) {
				skipping = -1;
				out.push(line); // the class's closing brace
			}
			depth += braceDelta(line);
			continue;
		}
		if (depth > 0 && (trimmed === 'private:' || trimmed === 'protected:')) {
			skipping = depth;
			// Drop comments that introduced the section.
			while (out.length && isComment(out[out.length - 1])) out.pop();
			continue;
		}
		if (/^friend (struct|class) /.test(trimmed) && trimmed.endsWith(';')) continue;
		out.push(line);
		depth += braceDelta(line);
	}
	// Empty `public:` right before a closing brace, and blank lines before closing braces.
	return out.filter((line, i) => !(line.trim() === '' && out[i + 1] && /^\s*\};?/.test(out[i + 1])));
}

// --- Splitting into documented items ---

// Blocks separated by blank lines at namespace level.
function blocks(lines) {
	const result = [];
	let current = [];
	let depth = 0;
	for (const line of lines) {
		if (line.trim() === '' && depth === 0) {
			if (current.length) result.push(current);
			current = [];
			continue;
		}
		current.push(line);
		depth += braceDelta(line);
	}
	if (current.length) result.push(current);
	return result;
}

// Function templates defined in the header: just the declaration.
function withoutBodies(code) {
	const out = [];
	let depth = 0;
	for (let i = 0; i < code.length; i++) {
		const line = code[i];
		const trimmed = line.trim();
		const starts = depth === 0 && trimmed.endsWith('{') && line.includes('(') &&
			!/^(class|struct|enum|union)\b/.test(trimmed) && !/=\s*\{$/.test(trimmed);
		if (starts) {
			out.push(line.replace(/\s*\{$/, ';'));
			let inner = braceDelta(line);
			while (inner > 0 && ++i < code.length) inner += braceDelta(code[i]);
			continue;
		}
		out.push(line);
		depth += braceDelta(line);
	}
	return out;
}

function headingOf(code) {
	for (const raw of code) {
		const line = raw.trim();
		if (line === '' || line.startsWith('template') || line.startsWith('requires')) continue;
		let m;
		if ((m = line.match(/^#define (\w+)/))) return m[1];
		if ((m = line.match(/^(?:enum class|enum|class|struct)\s+(\w+)/))) return m[1];
		if ((m = line.match(/^using (\w+) =/))) return m[1];
		if ((m = line.match(/^inline (?:constexpr |const )+\w+ (\w+)::\w+/))) return `${m[1]} constants`;
		const paren = line.indexOf('(');
		const equals = line.indexOf('=');
		if (equals >= 0 && (paren < 0 || equals < paren)) {
			if ((m = line.slice(0, equals).match(/(\w+)\s*$/))) return m[1];
		}
		if ((m = line.match(/(operator\S+?|~?\w+)\s*\(/))) return m[1];
		return line.replace(/[;{].*$/, '').trim();
	}
	return null;
}

// --- Markdown ---

// Pointers into design.md (the design document in the repository) mean nothing on the web.
const withoutDesignRefs = (text) =>
	text
		.replace(/\s*\((?:see )?(?:design\.md )?§[\d.]+(?:, rule \d+)?\)/g, '') // "(design.md §8)", "(§13.2)"
		.replace(/,?\s*(?:see )?design\.md §[\d.]+(?:, rule \d+)?/g, ''); // "..., design.md §9.5)"
const escapeProse = (text) =>
	withoutDesignRefs(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

// A comment block as Markdown: paragraphs, lists, and indented lines as C++ examples.
function commentToMarkdown(comments) {
	const out = [];
	let paragraph = [];
	let code = [];
	const flushParagraph = () => {
		if (paragraph.length) out.push(escapeProse(paragraph.join(' ')), '');
		paragraph = [];
	};
	const flushCode = () => {
		if (code.length) {
			const indent = Math.min(...code.filter((l) => l.trim()).map((l) => l.match(/^ */)[0].length));
			out.push('```cpp', ...code.map((l) => l.slice(indent)), '```', '');
		}
		code = [];
	};
	for (const raw of comments) {
		const text = raw.trim().replace(/^\/\/ ?/, '');
		if (/^ {2,}\S/.test(text)) {
			flushParagraph();
			code.push(text);
		} else if (text.trim() === '') {
			flushParagraph();
			flushCode();
		} else if (/^(\s*)- /.test(text)) {
			flushParagraph();
			flushCode();
			paragraph.push(text.trim());
			flushParagraph();
			out.pop(); // list items stay together
		} else {
			flushCode();
			paragraph.push(text.trim());
		}
	}
	flushParagraph();
	flushCode();
	return out.join('\n').replace(/\n{3,}/g, '\n\n').trim();
}

function render(header, description) {
	const source = fs.readFileSync(path.join(includeDir, header), 'utf8');
	const items = blocks(clean(source));
	const out = [
		'---',
		`title: "<elg/${header}>"`,
		`description: ${JSON.stringify(description)}`,
		'editUrl: false',
		'---',
		'',
		`<!-- Generated by docs/scripts/gen-reference.mjs from include/elg/${header}. Edit the header, not this page. -->`,
		'',
		'```cpp',
		`#include <elg/${header}>`,
		'```',
		'',
	];
	const seen = new Map();
	items.forEach((block, index) => {
		const firstCode = block.findIndex((l) => !isComment(l));
		const comments = firstCode < 0 ? block : block.slice(0, firstCode);
		const code = firstCode < 0 ? [] : withoutBodies(block.slice(firstCode));
		if (!code.length) {
			const section = comments.length === 1 && comments[0].match(/^\/\/ --- (.+) ---$/);
			if (section) {
				out.push(`## ${escapeProse(section[1])}`, '');
			} else {
				out.push(commentToMarkdown(comments), '');
				if (index === 0) out.push('');
			}
			return;
		}
		const heading = headingOf(code);
		// Overloads and repeated names share one heading.
		if (heading && !seen.has(heading)) {
			out.push(`### ${escapeProse(heading)}`, '');
			seen.set(heading, true);
		}
		if (comments.length) out.push(commentToMarkdown(comments), '');
		const tidy = (line) => {
			const comment = line.indexOf('//');
			return comment < 0 ? line : line.slice(0, comment) + withoutDesignRefs(line.slice(comment));
		};
		out.push('```cpp', ...code.map(tidy), '```', '');
	});
	return out.join('\n').replace(/\n{3,}/g, '\n\n');
}

fs.rmSync(outDir, { recursive: true, force: true });
fs.mkdirSync(outDir, { recursive: true });

const index = [
	'---',
	'title: API reference',
	'description: Every public header of Elegance, generated from the headers themselves.',
	'editUrl: false',
	'sidebar:',
	'  order: 0',
	'  label: Overview',
	'---',
	'',
	'These pages are generated from the public headers in `include/elg/`, so they always match the',
	'code. Each shows the header\'s declarations with the comments written above them; private',
	'members and implementation details are left out. Include `<elg/elegance.h>` to get everything.',
	'',
	'| Header | Contents |',
	'| --- | --- |',
];
let order = 1;
for (const [header, description] of headers) {
	if (!fs.existsSync(path.join(includeDir, header))) throw new Error(`missing header ${header}`);
	const page = render(header, description).replace('editUrl: false\n', `editUrl: false\nsidebar:\n  order: ${order++}\n  label: "${header}"\n`);
	fs.writeFileSync(path.join(outDir, `${slugOf(header)}.md`), page);
	index.push(`| [\`<elg/${header}>\`](./${slugOf(header)}/) | ${description} |`);
}

// Every header should be listed above.
const listed = new Set(headers.map(([h]) => h));
for (const entry of fs.readdirSync(includeDir, { recursive: true })) {
	const header = entry.replace(/\\/g, '/');
	if (header.endsWith('.h') && header !== 'export.h' && !listed.has(header)) {
		console.warn(`gen-reference: ${header} isn't in the list; add it to scripts/gen-reference.mjs`);
	}
}

fs.writeFileSync(path.join(outDir, 'index.md'), index.join('\n') + '\n');
console.log(`gen-reference: ${headers.length} headers -> ${path.relative(process.cwd(), outDir)}`);
