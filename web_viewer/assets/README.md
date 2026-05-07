# Assets

## Logos / marks

**⚠️ Flagged substitutions.** The `openpowermonitor` repo ships no logo or wordmark. The files in this folder are **originals created for this design system** to give the brand a visual anchor. Replace them if a real mark is produced.

- `mark.svg` — square icon. A stylized INA228-sampled sine burst on a dark panel. 32 / 64 / 128 px friendly.
- `wordmark.svg` — horizontal "Power Monitor" wordmark with the mark.
- `mark-on-light.svg` — light-theme variant of the mark.

## Icons

Icons are not bundled. This system uses **Lucide** (https://lucide.dev) from CDN:

```html
<script src="https://unpkg.com/lucide@0.447.0/dist/umd/lucide.min.js"></script>
<script>lucide.createIcons();</script>
<i data-lucide="activity"></i>
```

Approved Lucide glyphs for Power Monitor UI:
- `activity` — generic signal / timeline
- `upload` — load JSON
- `maximize-2` — fit all
- `layers` — tracks
- `grip-vertical` — drag handle
- `plug-zap`, `battery` — product/hardware
- `circle` — legend dots (already handled with colored divs)
