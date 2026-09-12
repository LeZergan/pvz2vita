# RC12 texture failures — September 12, 2026

The supplied loader.log is RC11 (September 10, 20:08:13), reaches frame 189600,
and contains no STALL snapshots. Four TEXTURE records report GL_OUT_OF_MEMORY
for 2048x2048 and 1024x2048 images near frames 187800–189000. Frames continue
after those records; the last report is 58.95 FPS. These allocation errors
are confirmed, but the log alone does not locate the reported crash.

Both supplied dumps are exact copies of previously analyzed RC6 date crashes:

| Dump | SHA-256 |
| --- | --- |
| 1788920884 | `259a29cc5276bc3fda45eb7e36c658109bcb60bab6555e106222721a804fd494` |
| 1788921085 | `9a4961fb028e68f14f608d9dca46e97a3d51252fbe5f8d080b5d690a0e07c1c0` |

The existing time bridge passes the original failing game instruction with
UTC-3 and timestamp 6. No fresh RC11 crash stack was supplied. The current
log hash is `0e4240142bb6ccd9c14f676f260d54d7b9289593245205f7d10d48958e536712`.

## Reproduced defects and changes

- The imported image-upload wrapper lacked the mip guard present in another,
  unused wrapper. It now rejects higher mip uploads for resized textures and
  placeholders, retaining normal mip uploads for unchanged textures.
- Packed 4444/565 uploads with pixels were expanded to RGBA8, but NULL storage
  and later packed subimages followed different paths. Storage and fills now
  consistently use RGBA8 and CPU conversion. The shipped VitaGL's native
  packed-storage/byte-fill path was independently shown to produce incorrect
  pixel bytes; this is not evidence of a heap overwrite or the latest crash.
- A failed CPU downsample after GPU allocation failure previously uploaded
  NULL and marked that empty image as recovered. It now uses a checked 1x1
  placeholder and blocks incompatible subsequent fills. Failed alpha uploads
  also establish checked fallback state. A failed smaller upload no longer
  retries a larger full-resolution allocation.
- The old NULL-storage OOM fallback silently halved dimensions, potentially
  breaking render-target viewport/depth attachment agreement. It now stops
  with an explicit graphics-memory error. A failed 1x1 fallback also stops;
  neither case returns to rendering with known failed storage. This is safe
  failure handling, not a claim that every memory-starved scene can continue.

## Validation

`check-texture-units.py` has six regressions (`mips`, `packed`, `oom-conversion`,
`oom-alpha`, `oom-target`, `oom-placeholder`). Each fails on the preserved RC11
source and passes on RC12. Normal texture bindings, fills, deletion/name reuse,
thin images and conversion-failure guards also pass. Fatal-path tests assert
the upload count and terminate in a host stub rather than showing a dialog.

`check-texture-storage-arm.py` executes the linked VitaGL and production loader
with memory allocation, system/GPU queries, and file/logging calls mocked. It
checks actual pixel bytes for empty packed storage, RGBA8 fills, and packed
fills after normalization. It does not boot the game or render a frame.
The existing compiled timezone, shader lifetime and pixel-worker checks pass.

No Vita3K was used. RC12 hardware confirmation is pending. A new dump from the
failing installed build, with its log and triggering action, is still needed
to identify the reported crash conclusively. Placeholder recovery can omit
artwork; unrecoverable graphics allocation requires a restart.
