# Elegence documentation

The documentation site, built with [Astro Starlight](https://starlight.astro.build). Guides live in
`src/content/docs/`; the API reference is generated from `include/elg/` by
`scripts/gen-reference.mjs` on every build, and the guides show the sources and screenshots of
`examples/`.

```sh
npm ci          # once
npm run dev     # http://localhost:4321, reloads as you edit
npm run build   # static site in dist/
```

Needs Node 22.12 or later. To publish under a sub-path (GitHub Pages for a repository), set
`DOCS_SITE` and `DOCS_BASE`; `.github/workflows/docs.yml` does this.

Screenshots in `src/assets/examples/` are captures of the example apps; retake them when an
example's look changes.
