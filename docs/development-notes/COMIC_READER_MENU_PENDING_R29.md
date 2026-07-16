# R29 Comic reader fixes

## Fixed r28 stuck-on-open

R28 could start background next-page preload before the current visible page had completed rendering. Logs looked like this:

```text
Extracting page 38/211
Extracting page 39/211
```

R29 prevents preload until:

- at least one visible CBZ page display has completed,
- the render task is not busy,
- no page-turn input is pending,
- at least 1500 ms has passed since the last display.

## Footer button fix

Footer screens emit fixed raw button zones:

```text
Back / Select / Previous / Next
```

R29 bypasses reader front-button remapping while footer mode is active so the visible footer buttons match the touch zones.

## Comic menu

Center tap in CBZ reader opens a comic-specific menu:

1. Return Home
2. Gray enhancement, -50%..+50%
3. 4-level / 16-level grayscale preference

The values are saved to settings JSON and the comic page is forced through a full refresh after changes.
