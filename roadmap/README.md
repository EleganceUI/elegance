# Elegance roadmap

A checklist site for building Elegance by hand: every subsystem in build order, what to read
first, how to know each part works, and the traps the current code already hit. Built with
[Astro Starlight](https://starlight.astro.build), like `docs/`.

```sh
npm ci          # once
npm run dev     # http://localhost:4322, reloads as you edit
npm run build   # static site in dist/
```

Needs Node 22.12 or later.

- Checklists live in `src/content/docs/<part>/`, and specs ("How it works") in
  `src/content/docs/how/<part>/` under the same file name; each links to its twin automatically.
  The sidebar is generated from these folders (`src/sidebar.mjs`): each topic's spec, then its
  checklist. Cross-cutting explanations live in `src/content/docs/how/concepts/`.
  The parts and their order are in `src/sections.mjs`; pages within a part are ordered by
  `sidebar.order`.
- A spec lists what it builds on in its frontmatter (`depends: [framework/reactive, ...]`, ids
  without `how/`). The site shows "Depends on" / "Needed by" on each spec and generates the
  dependency map from them (`src/graph.ts`). An unknown id or a cycle fails the build.
- Any Markdown task item (`- [ ] ...`) becomes a clickable checkbox. Ticks are kept in the
  browser's local storage, keyed by a hash of the item's text: reordering keeps them, rewording
  an item clears it. The overview page can export and import them as JSON.
- To publish under a sub-path, set `ROADMAP_SITE` and `ROADMAP_BASE` (see `astro.config.mjs`).
  Links between pages are relative, so they work under any base.
