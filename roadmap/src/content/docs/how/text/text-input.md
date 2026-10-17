---
title: TextField and SecureField
description: "The editor model, shown vs real text, carets and selection, IME, undo, multi-line, and how the field is split into elements."
sidebar:
  order: 5
depends: [text/fonts-and-layout, text/styled-text, framework/input-and-focus]
---

## What it's for

A text field must feel native: typing, IME composition, selection by keyboard and pointer, the
clipboard, undo, scrolling to the caret, and a blinking caret that costs nothing while idle. It
must also let styles decide what the field looks like, and let apps show text differently from
what's stored (passwords, mention chips, markdown).

## How it works

### Three pieces

| Piece | Job |
|---|---|
| `TextEditor` | The model: text, caret and selection, composition, undo, shaping and layout, painting the text |
| `TextEditorElement` | Where the text sits inside the style's body. Measures, arranges and paints by asking the editor |
| `TextFieldElement` | The control around the style's whole body. Focusable; takes keys, text input and pointer events and passes them to the editor |

`TextFieldStyle` builds the body from a configuration that includes the `field` view (the
editor element) plus focus and hover states. The default draws a rounded border; `Plain` draws
nothing. Because the editor element is placed by the style, the text takes fonts and colors from
**where the style puts it**.

### Text, shown text, and segments

The **text** is what the binding holds. What's **shown** can differ:

- a **SecureField** shows one bullet per character;
- a **styler** (`TextFieldOptions::styler`) returns `TextSpan`s (styled byte ranges), and spans
  with a `label` show the label instead of their text (a mention `<@123>` shown as "@Ana").

The editor keeps a list of **segments** `{text_begin, text_end, shown_begin, shown_end, atomic}`.
The caret, selection and undo work in **text** bytes; shaping, layout, drawing and hit testing
work in **shown** bytes. `to_shown` and `to_text` map between them. Atomic segments (labels,
bullets) can't be entered: a position inside one snaps to its nearer end, the caret jumps over
it, and Backspace deletes it whole.

### Shaping and layout

On any text or style change, `reshape()` rebuilds segments and the shown text, runs the styler,
and shapes the shown text as styled runs (the [StyledText layout](../styled-text/)). On width
change, `relayout(width)` breaks lines and computes **caret stops**: every valid caret position
with its x, line and height. Only cluster boundaries are stops.

### Editing

- **Caret and anchor** are text byte offsets; the selection is between them. Shift extends by
  moving the caret and keeping the anchor.
- **Movement**: by caret stop (arrows), by word (Ctrl/Alt+arrows), to line or text ends
  (Home/End), between lines at the same x (Up/Down in multi-line).
- **Replacing**: typing or pasting replaces the selection. `clean()` removes newlines unless the
  field is multi-line. The binding is written after every change.
- **Undo**: before a change, a snapshot (text, caret, anchor) is pushed. Consecutive **typing**
  shares one snapshot, so a typed word undoes at once; any other edit starts a new one. Redo is
  the mirror stack, cleared by new edits.
- **External changes**: when the binding changes from outside (a completion, a reset),
  `set_text` replaces the text, moves the caret to the end (where typing carries on), clears any
  composition and doesn't record undo. The field's own writes come back through the binding too;
  an unchanged value is ignored.

### IME

While the field has focus, the platform's text input is on. Composition text arrives as
"editing" events with a caret; the editor keeps it separately (`composition_`), shows it
**inline at the caret, underlined**, and doesn't write the binding until text is committed. After
each layout, the editor reports where the caret is (`set_text_input_area`), so the OS places its
candidate window there. Secure fields don't allow composition.

### Pointer

A click places the caret at the nearest stop (Shift+click extends), drag selects,
double-click selects a word, and triple-click selects all. The I-beam shows on hover with no
delay. Tabbing in selects everything; clicking in doesn't.

### Scrolling and the caret

A single-line field scrolls horizontally, and a multi-line field (`max_lines > 1`) grows up to
that many lines and then scrolls vertically, keeping the caret in view. The editor element
`always_arranges`, so the scroll follows the caret. The caret **blinks** with timed wakes
(`WindowServices::wake_at`) at each on/off edge, restarting at each edit, so an idle focused
field wakes about twice a second and does nothing in between.

### Keys and submit

The field consumes printable keys, so an outer `.on_key_press` never sees typing.
`options.on_key` sees each key **first** and may take it (an autocomplete list using Up/Down/Enter).
Enter runs `on_submit`; in multi-line fields, Shift+Enter inserts a newline.

## Rules

- Caret, selection and undo in text bytes; drawing in shown bytes; segments translate.
- The caret only stops on cluster boundaries and never inside an atomic segment.
- Composition never reaches the binding until committed.
- A secure field never exposes its text: no copy, no cut, no accessibility value.

## Edge cases

- **Word movement in a secure field** goes to the ends, since words would leak structure.
- **A styler's spans** must be sorted and disjoint; that's the contract, and worth an assertion.
- **Grapheme accuracy**: cluster boundaries approximate graphemes; some combining sequences may
  allow a caret stop inside them.
