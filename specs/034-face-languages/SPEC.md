# 034 Custom faces in the clock's language

Status: Proposed (2026-11-01)

## Problem

The clock speaks English or French (`lang`), and its own placeholders follow
(`{date.wday}`, `{now.text}`). A custom face's own words do not: "TASKS", "NEXT STOP",
"up to 1.2 mm in 15 min" are written in one language. Today a face can switch with a map
per word (`"maps": {"hello": {"en": "Hello", "fr": "Bonjour"}}` and `"{sys.lang:hello}"`),
which works but takes a map and a lookup for every word.

## Goals

1. **Text in several languages, in place:** any text field may be an object of languages,
   `"text": {"en": "RAIN IN {now.rain_in} MIN", "fr": "PLUIE DANS {now.rain_in} MIN"}`.
   The clock draws its language's entry, else `en`, else the first one. (smalltv-pomodoro's
   faces do this, with `pt`, `en`, `es`.)
2. **A face's word list:** `"words": {"en": {"tasks": "TASKS"}, "fr": {"tasks": "TÂCHES"}}`
   and `{w.tasks}` anywhere a placeholder goes, for words used several times or inside
   longer text; a word missing in the clock's language falls back to `en`.
3. **Within the limits:** both count toward the 4 KB a face; accents are folded as all
   text is (`é` → `e`).
4. **Documented and checked:** the face format (integrations API and the skill's
   reference and schema), an example face in both languages, unit checks, and the
   example faces' French screenshots in the integration suite.

## Non-goals

- More clock languages (the clock's own text is English and French).
- Translating the example faces wholesale; those that gain French are chosen one by one.
