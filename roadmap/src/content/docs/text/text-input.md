---
title: TextField and SecureField
description: An editor model, carets and selection, IME, clipboard, undo, multi-line, and styled spans.
sidebar:
  order: 5
---

The biggest single control. Keep the editing model (text, caret, selection, undo) separate from
drawing and from the focusable element that takes input.

:::note[Depends on]
[Fonts, shaping, and the Text view](../fonts-and-layout/), [StyledText](../styled-text/) (styled runs, for the styler and multi-line), [Hit testing, events, and focus](../../framework/input-and-focus/),
and [Environment, themes, and styles](../../framework/environment-and-styles/) (`TextFieldStyle`).
:::

## Read first

- `design.md` §11 (Text input, M4 details), M4 `SecureField` and fixes
- `src/view/text_field.*`, `TextFieldOptions` in `include/elg/views.h`, `TextSpan` in `include/elg/text.h`

## Tasks

### Structure
- [ ] `TextEditor` model: text synced with the `Binding<std::string>`; caret and selection as byte offsets on HarfBuzz cluster boundaries; IME composition; undo stack
- [ ] `TextEditorElement` draws the text where the `TextFieldStyle` places it
- [ ] A focusable `TextFieldElement` around the style's body takes the input
- [ ] `WindowServices`: text input on/off, the IME input area, clipboard, cursor shape, `wake_at(time)`
- [ ] `FocusManager` turns SDL text input on while a field has focus

### Keyboard editing
- [ ] Arrows; Ctrl+arrows (Alt on macOS) by word; Home/End
- [ ] Shift extends the selection; Backspace/Delete, with word variants
- [ ] Ctrl/Cmd + A, C, X, V
- [ ] Undo (Ctrl/Cmd+Z) with consecutive typing grouped; redo with Ctrl+Y or Shift+Ctrl/Cmd+Z
- [ ] Enter runs `on_submit`
- [ ] Consume printable keys so outer `on_key_press` handlers don't see typing
- [ ] `options.on_key` sees keys first (autocomplete lists taking Up/Down/Enter)

### Pointer
- [ ] Click places the caret, Shift+click extends, drag selects
- [ ] Double-click selects a word, triple-click selects everything
- [ ] I-beam cursor on hover
- [ ] Tabbing in selects all; clicking in doesn't

### Display
- [ ] Horizontal scroll to keep the caret in view
- [ ] Caret blink via timed wakes (`wake_at`), not continuous rendering
- [ ] IME: composition drawn inline and underlined at the caret; candidate window placed with `SDL_SetTextInputArea`
- [ ] Text takes its font and colors from where the style places the field, so a style's `.font()` and `.foreground_style()` apply

### Styles
- [ ] `TextFieldStyle` protocol; `RoundedBorderTextFieldStyle` (default) and `PlainTextFieldStyle`

### SecureField
- [ ] One bullet per character; caret stops map back to the text's bytes
- [ ] No copy or cut; word movement and double-click take the whole field; no IME composition
- [ ] Never expose its text to accessibility

### Multi-line and styled spans (newest work)
- [ ] `max_lines > 1`: wraps and grows up to that many lines, then scrolls vertically; Enter submits, Shift+Enter inserts a newline
- [ ] `styler(text) -> vector<TextSpan>`: sorted, non-overlapping styled byte ranges, re-run after each change
- [ ] Spans with a `label` show the label and edit as one unit (the caret goes around them, Backspace deletes them whole), e.g. mentions as chips while the text keeps `<@123>`
- [ ] Map between shown text and real text through segments; atomic segments (labels, bullets) can't be entered

## Done when

- [ ] Typing Japanese through the Windows IME shows the composition inline and the candidate list at the caret
- [ ] Undo after typing a word and then pasting undoes the paste, then the word
- [ ] A markdown styler bolds `**text**` live while typing, and a mention chip deletes as one unit
- [ ] An idle focused field wakes about twice a second for the caret and is otherwise at 0% CPU

## Gotchas

- HarfBuzz clusters only approximate grapheme boundaries. ICU or UAX #29 is the proper fix.
- Not built yet: selectable `Text`, a cut/copy/paste context menu in fields, and caret/selection
  exposure to screen readers.
