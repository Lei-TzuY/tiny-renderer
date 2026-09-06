# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

## Integrated architecture through Milestone 46

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–43 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading/shadows, and bounded per-light RGB color.

Milestones 44–46 make shadow ownership and sampling composable: every bounded light record can own a typed shadow resource, Hard/3x3-PCF is explicit per binding, and directional records can own bounded ordered cascade sets selected deterministically from perspective-correct world position and camera-view depth.

The current main line also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, and bounded PPM/TGA texture import through the shared image dispatch path.

## Milestone 46 — bounded cascaded directional shadows

Implemented acceptance surface:

- 1–4 immutable directional cascades with finite strictly increasing view-depth splits, per-cascade light transform, bias, depth texture, and Hard/PCF policy;
- half-open cascade ownership with exact boundary behavior and deterministic unshadowed handling beyond the final split;
- camera shading selects one cascade from the established perspective-correct world position and reuses the existing projected shadow sampler;
- cascade capture reuses prepared geometry, vertex programs, alpha-tested casters, clipping, culling, depth ownership, and depth capture;
- legacy single-map directional records remain compatible and can coexist with cascaded records on different lights;
- direct/model/prepared/list validation remains fail closed, and prepared plans retain cascade resources across source lifetime;
- deterministic regressions cover resource/split validation, near/far visibility, single-map compatibility, capture parity, mixed bindings, prepared ownership, and whole-list no-write failure semantics.

## Next frontier — Milestone 47

Promote texture sampling to **deterministic mipmapped minification with raster-derived LOD**. Preserve the existing level-zero nearest/bilinear path exactly by default, then add a validated owned mip chain, bounded nearest-level/trilinear policy, and perspective-correct UV-gradient footprint calculation integrated into the existing diffuse/opacity/normal texture path. No anisotropic, sRGB, shader-derivative, GPU, performance, or image-quality claim belongs in the first slice.
