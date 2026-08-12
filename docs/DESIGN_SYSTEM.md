# TrinityCore Studio design system

## Intent

The Studio interface is a high-density professional workbench, not a themed game
menu. It has to keep a large number of world-data editors, a live 3D map, schema
warnings, and destructive database actions legible at the same time. The redesign
uses a single visual language across the project launcher, shell, database forms,
client-data inspectors, validation, and logs.

## Layout hierarchy

```text
Menu / product identity
└─ Workspace bar: project, active editor, record context, connection state, commands
   ├─ Navigation rail: grouped tools with compact identity cards
   ├─ Docked document workspace: browser / canvas / inspector / tools
   └─ Status bar: write mode, active document, core flavor, contextual feedback
```

- **Project context is always visible.** The workspace bar identifies the project,
  current editor, record, target write mode, and command palette.
- **Navigation is semantic.** Content, Logic & Scripts, and World & Data are
  distinct groups; every module has an icon tile, readable name, and stable ID.
- **Panels explain themselves.** Shared `StudioPanelHeader` cards label browser,
  inspector, data source, and validation surfaces before users encounter fields.
- **Empty states include the next action.** A disconnected DB, missing client data,
  and no selected record are deliberate workflows rather than blank panels.

## Visual tokens

| Token | Meaning |
| --- | --- |
| Gold | Primary action, selected studio identity, Blizzard-compatible emphasis |
| Blue | Dark-theme navigation, selected tabs, informational context |
| Green | Live connection / safe success state |
| Violet | SQL-export / reviewable-output state |
| Red | Destructive action / blocking error |
| Slate | Surface hierarchy, secondary controls, quiet actions |

The dark palette uses an obsidian-blue surface stack; Blizzard preserves warm
parchment-compatible charcoal and gold. Both retain strong text contrast, visible
field boundaries, tab hierarchy, and table row separation.

## Reusable components

`Editor/ui/Widgets` supplies the shared presentation layer:

- `StudioPanelHeader` — eyebrow, title, description, status badge
- `StudioEmptyState` — clear no-data/no-selection/connection guidance
- `StudioButton` — primary, secondary, quiet, and danger action hierarchy
- `StudioPill` — compact state badges for live/export/offline and project metadata
- `BeginFieldTable` / `FieldRow` — consistent label/value grids with readable rows

Editor modules remain responsible for document state and persistence; components
only render. This keeps the C++ model/repository layer independent of ImGui.

## Interaction and accessibility rules

1. **Never encode a destructive state only by color.** Buttons include clear labels;
   validation carries text severities.
2. **Never leave an empty surface unexplained.** Use an empty state with a next step.
3. **Retain keyboard-first workflows.** Menu shortcuts, Ctrl+P, Ctrl+S, Ctrl+Z/Y,
   and module-specific tools remain available after visual changes.
4. **Keep project/core/write context visible.** A user must know whether a change is
   targeting Live DB or SQL Export before saving.
5. **Use review gates for structural operations.** SQL conversion preview and world
   validation are surfaced as cards/notes before transactional application.
