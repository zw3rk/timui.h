# Image limits and lifecycle

Terminal images are optional. The API always falls back to the `[img]`
placeholder when a protocol, payload, or limit is unsupported.

## Public limits

| Macro | Default | Meaning |
|---|---:|---|
| `TIMUI_IMAGE_MAX_DIMENSION` | `4096` | Maximum decoded image width or height in pixels. |
| `TIMUI_IMAGE_MAX_PIXELS` | `16777216` | Maximum decoded RGBA pixels. |
| `TIMUI_IMAGE_PNG_MAX_BYTES` | `16777216` | Maximum PNG byte payload accepted by PNG constructors and lazy decode. |
| `TIMUI_IMAGE_PNG_MAX_DIMENSION` | `TIMUI_IMAGE_MAX_DIMENSION` | PNG-specific dimension cap. |
| `TIMUI_IMAGE_PNG_MAX_PIXELS` | `TIMUI_IMAGE_MAX_PIXELS` | PNG-specific decoded-pixel cap. |
| `TIMUI_IMAGE_PLACEMENT_CAP` | `8` | Maximum terminal image placements recorded in one frame. Extra placements draw placeholders. |

Applications can override these macros before including `timui.h`.

## Ownership

`timui_image_from_png`, `timui_image_from_rgba`, and
`timui_image_from_png_rgba` return an owning `TimuiImage *`. Free it with
`timui_image_free`. The caller may free or mutate its original source buffers
after construction; timui copies accepted PNG bytes and RGBA rows.

Plain PNG images are lazily decoded only when a Sixel draw needs RGBA pixels.
The decoded RGBA cache lives in the `TimuiImage` until `timui_image_free`.

## Protocol lifecycle

- Kitty: timui transmits PNG bytes once per `TimuiImage` id and emits placements
  each frame. Stale Kitty placements from the previous frame are deleted before
  the next image flush.
- iTerm2: timui emits inline OSC 1337 image payloads for unclipped PNG-backed
  draws. There is no retained placement id to delete.
- Sixel: timui emits DCS Sixel payloads from RGBA pixels, PNG+RGBA sidecars, or
  a bounded lazy PNG decode.
- `TIMUI_NO_IMAGES`: image APIs remain available, image caps are stripped, and
  every draw uses the text placeholder.

Malformed, oversized, clipped-unsupported, or protocol-incompatible image draws
must not emit partial terminal image escapes.
