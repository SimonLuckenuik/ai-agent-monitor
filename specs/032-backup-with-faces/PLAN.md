# 032 Plan

1. Settings page: `backup()` collects the settings, faces and variables (the variables'
   `$persist` flag comes from `GET /api/faces`, which lists them; if it does not say which
   persist, add that field), and saves one JSON file.
2. `restore()`: by type; settings first, then faces one by one, then variables with
   `$persist`; a summary toast and a list of refusals.
3. Integration case: push faces and variables, back up through the page's code path
   (Python doing the same calls), reset, restore, compare.
4. Docs: settings.md (Backup), CHANGELOG.
