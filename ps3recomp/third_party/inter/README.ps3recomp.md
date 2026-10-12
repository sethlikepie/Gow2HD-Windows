# Inter font pin

Fallback typeface of the runtime overlay shell (used when SF Pro is not
available, and always on Windows/Linux).

- Upstream: <https://github.com/rsms/inter>, release `v4.1` (`Inter-4.1.zip`,
  SHA-256 `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e`).
- Files: `extras/ttf/Inter-Regular.ttf`, `extras/ttf/Inter-SemiBold.ttf`, `LICENSE.txt`.
- License: SIL Open Font License 1.1 (`LICENSE.txt`, unmodified).
- Embedded at build time by `cmake/embed_file.cmake` into the
  `ps3recomp_overlay_fonts` object library; nothing is modified.
