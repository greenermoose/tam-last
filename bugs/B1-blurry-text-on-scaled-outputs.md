# B1: Blurry text on scaled outputs

- **Status:** Fix available
- **Severity:** high
- **Affected versions:** 0.0.1 (first build)
- **Fixed in:** 0.0.1 (second build, before Fred accepted 0.0.1)
- **Reported:** 2026-11-17, by Fred, with a screenshot beside two foot
  windows

## What happens

On an output with a scale of 1.25, tam-last's text is blurry; foot's,
beside it on the same output, is crisp.

## What should happen

Text as crisp as foot's at any scale.

## Steps to reproduce

Run tam-last on an output whose scale is not 1 (Hyprland `monitor`
scale 1.25), next to foot.

## Cause

tam-last drew at scale 1 and the compositor stretched the buffer by 1.25.
foot draws at the output's real resolution through fractional-scale-v1
and viewporter. The font rasterizer was not the cause: it was not
consulted on the size it draws at.

## Fix and verification

tam-last binds `wp_fractional_scale_manager_v1` and `wp_viewporter`. On
`preferred_scale` it reopens the font at 96 dpi times the scale, draws
the buffer at the logical size times the scale (rounded as the protocol
asks), and sets the viewport destination to the logical size. The tests
check the new opcodes against the XML, and `tests/test_render.c` writes
a sample at 120 dpi (scale 1.25): cells of 9 x 20 device pixels.
Waiting for Fred to check it on DP-2.
