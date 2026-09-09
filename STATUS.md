# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed historical milestone record. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 71

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI. Milestones 66–68 add bounded glossy environment-reflection mip policies and expose them through the same library/tooling path. Milestones 69–71 complete the current bounded MTL material-texture data plane with owned `map_Ks`, `map_Ke`, and `map_Ns`: shared decoded ownership, UV/sampler/gradient reuse, fail-closed direct/prepared/list validation, deterministic fingerprints/inspection, and shared direct/environment specular semantics.

Milestone 71 specifically maps linear `map_Ns` RGB by arithmetic mean into the bounded `[1,1000]` exponent domain as `1 + mean(rgb) * 999`. A mapped exponent replaces the uniform `Ns` fallback for that fragment and is resolved once for both direct Blinn-Phong specular and `EnvironmentReflectionMipPolicy::MaterialShininess`. The milestone also closes the emissive-only direct-mesh UV preflight gap discovered during integration review.

The exact integrated `main` commit is `29e9f1b1b43ee623bdf6ba7584b61a325d3b690c`; its Linux, macOS, and ASan/UBSan post-merge CI is green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. In the current lineage, milestone-60 and milestone-62 branch names exist outside the exact integrated `main` history, and stale milestone-numbered branches are not completion evidence. Any future integration must pass the same PR/exact-head/post-merge gates as other milestones.

## Milestone 72 candidate — deterministic back-to-front prepared-list submission

The post-M71 architecture audit intentionally stops farming additional MTL map names. The next executable gap is submission ordering for the already-integrated transparency data path: the renderer has material opacity/`map_d`, source-alpha blending, explicit depth-write control, MSAA, alpha-to-coverage, and heterogeneous prepared lists, but the list API only preserves caller order.

This candidate adds a bounded painter-order submission layer above the existing prepared-list executor rather than duplicating raster, shading, depth, blend, or framebuffer ownership.

Acceptance surface:

- `draw_prepared_model_list_back_to_front` accepts the same heterogeneous `PreparedModelListEntry` records plus shared view/projection transforms;
- non-empty entries receive one deterministic entry-level sort key from the mean view-space Z of canonical mesh vertices after the entry model transform; the renderer's right-handed camera convention therefore submits more-negative/farther depths first;
- stable sorting preserves exact caller order when two entries have equal sort depth;
- all sort inputs must be finite and projectable in homogeneous view space; a malformed later entry rejects before the existing prepared-list executor can mutate the framebuffer;
- after ordering, execution delegates once to the existing `draw_prepared_model_list`, retaining its whole-list vertex-program preparation, dynamic material/raster preflight, and single raster/Framebuffer ownership path;
- the first slice rejects prepared entries with vertex programs because a program may move positions after the canonical sort key was computed; supporting them requires first-class post-program bounds/sort metadata rather than silently sorting stale geometry;
- empty prepared plans are ignored for sorting and empty input is a deterministic no-op;
- deterministic regressions compare a near-first source-alpha list against explicit far-then-near manual submission, prove byte/hash equivalence after sorting, prove non-commutative difference from unsorted caller order, lock stable equal-depth behavior, and verify non-finite later sort state fails before writes;
- this slice does not automatically classify opaque versus transparent draws, split mixed-material models, sort individual triangles, solve intersecting transparent geometry, implement order-independent transparency, or claim hardware/API parity.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Submission-order helpers may reorder already-prepared work, but they must delegate execution to the canonical prepared-list path rather than creating a second raster path.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 72

If M72 passes exact-head and post-merge CI, do not treat entry-level painter sorting as complete transparency. Re-audit the submission architecture. The most likely next promotion is first-class prepared draw/bounds metadata that can support draw-granularity ordering and post-vertex-program geometry without stale sort keys; alternatively, if transparency has reached diminishing returns, promote to an explicit material/BRDF architecture rather than adding more MTL names. Any PBR/microfacet slice must define deterministic BRDF/material semantics and evidence instead of rebranding the bounded Phong/Ns path.
