# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 43

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

The current main line also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, and bounded 24-bit TGA texture import through the shared image dispatch path.

## Milestone 44 — bounded per-record typed shadow bindings

The current integration candidate removes the one-shadow-per-light-type collection bottleneck without creating a second shading path.

Implemented acceptance surface:

- every bounded `FixedLight` record owns optional directional, point, and spotlight shadow state, with validation permitting only the binding matching that record's active type;
- multiple same-type records can own independent validated maps and biases while preserving exact caller-order light accumulation;
- directional per-record shadows project the existing perspective-correct world position per sample, avoiding new light-clip varying arrays; point and spotlight records reuse their existing world-position samplers;
- legacy singleton directional/point/spot associations remain supported and are byte/hash-equivalent to the matching per-record form, while double-binding one record is rejected deterministically;
- capture identity, resource presence, bias, cross-type state, and malformed direct submissions fail closed before framebuffer mutation;
- prepared plans retain per-record shared resource lifetime and heterogeneous list execution remains byte/hash-equivalent to prepared single submission;
- ambient remains unshadowed and only each bound record's colored direct contribution receives its own visibility scalar;
- no PCF/soft shadows, cascades, cookies/IES, automatic allocation, unbounded light state, GPU API, or performance claim is introduced.

## Next frontier — Milestone 45

Promote hard binary visibility into an **explicit bounded shadow sampling policy**. The next slice should retain `Hard` as the exact default and add one deterministic fixed-kernel PCF mode across the existing typed per-record resources, with clearly documented projected-map and cubemap-face edge behavior, whole-list validation, prepared ownership, and regression evidence. It must not claim physically based penumbrae, cross-face cubemap seam filtering, cascades, stochastic sampling, or performance parity.
