# tiny-renderer current status

This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains older detailed milestone history and is not authoritative when it lags this file. A capability is considered integrated only when its exact `main` commit has passed Linux, macOS, and ASan/UBSan CI; milestone-numbered branches by themselves are not completion evidence.

## Integrated architecture through Milestone 72

Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, explicit depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity and alpha-to-coverage, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–47 extend the same execution path with tangent-space normal mapping, Blinn-Phong specular lighting, point/spot/multi-light accumulation, point/spot/directional shadowing, RGB light color, per-record shadow bindings, deterministic PCF policy, cascaded directional shadows, owned mip chains, nearest-level/trilinear filtering, and raster-derived perspective-correct UV gradients.

Milestones 48–52 make color/HDR boundaries explicit: opt-in sRGB source decoding happens before mip generation, output sRGB encoding happens only at the 8-bit boundary, deterministic RGB PFM preserves resolved linear floats, PFM import enters the same linear texture/mipmap domain, and bounded Reinhard display mapping is a read-only export view.

Milestones 53–59 promote linear HDR environment textures into the existing renderer rather than creating a second rendering path: deterministic equirectangular camera backgrounds, headless environment rendering, camera-ray-footprint mip selection, bounded diffuse environment lighting, environment-lit headless rendering, view-dependent perfect-mirror environment reflection, and headless reflection with authoritative camera/viewer binding.

Milestone 61 adds bounded OBJ per-vertex RGB as canonical mesh semantics while preserving UV/normal channel layouts and prepared-model ownership. Milestone 63 adds bounded MTL `Ke` self-emission, fail-closed runtime/prepared validation, direct/prepared/list propagation, fixed-shading integration before the fragment program, and fingerprint semantics that preserve historical zero-emission byte sequences. Milestone 64 promotes the shared gradient sampler to bounded deterministic anisotropic filtering with exactly 1x/2x/4x policy, principal-axis footprint reduction, existing mip/address/filter reuse, and prepared-model validation. Milestone 65 exposes the same material-texture mip/anisotropy sampler through the headless render CLI. Milestones 66–68 add bounded glossy environment-reflection mip policies and expose them through the same library/tooling path. Milestones 69–71 complete the current bounded MTL material-texture data plane with owned `map_Ks`, `map_Ke`, and `map_Ns`: shared decoded ownership, UV/sampler/gradient reuse, fail-closed direct/prepared/list validation, deterministic fingerprints/inspection, and shared direct/environment specular semantics.

Milestone 71 maps linear `map_Ns` RGB by arithmetic mean into the bounded `[1,1000]` exponent domain as `1 + mean(rgb) * 999`. A mapped exponent replaces the uniform `Ns` fallback for that fragment and is resolved once for both direct Blinn-Phong specular and `EnvironmentReflectionMipPolicy::MaterialShininess`. The milestone also closes the emissive-only direct-mesh UV preflight gap discovered during integration review.

Milestone 72 adds deterministic entry-level painter ordering for caller-selected transparent prepared lists. `draw_prepared_model_list_back_to_front` computes one finite/projectable mean view-space Z key per non-empty prepared entry, uses stable far-to-near ordering, rejects vertex-program entries whose post-program positions are not represented by the canonical key, and delegates execution once to the existing prepared-list path. It does not classify opacity, split mixed material draws, sort triangles, or claim order-independent transparency.

The exact integrated `main` commit is `f4d63c1c4fbbf285875a54e5fb2da84eb8980bc1`; its Linux, macOS, and ASan/UBSan post-merge CI is green.

The repository also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, UV-optional position/normal face layouts, bounded PPM/TGA/PFM texture import through the shared image dispatcher, and bounded headless OBJ preview/render tooling.

### Milestone-number note

Milestone numbers describe work streams, not an assertion that every lower-numbered branch has been integrated. Stale milestone-numbered branches are not completion evidence. The branch `milestone-73-prepared-spatial-metadata` currently exists at the exact M72 main commit but has no implementation commit yet; it is treated as a reserved concurrent surface and must pass normal PR/exact-head/post-merge gates before this status can call it integrated.

## Milestone 74 candidate — bounded flat-scene headless rendering

M74 is intentionally independent from the reserved M73 prepared-spatial-metadata surface. It promotes the already-integrated heterogeneous prepared-list architecture into a real multi-model offline transaction instead of introducing a persistent scene graph or a second raster path.

Acceptance surface:

- `OfflineSceneEntry` borrows one `ModelAsset`, caller model transform, and `ModelRenderOptions`;
- `render_scene_preview` validates settings and all entries, snapshots each asset/options pair into `PreparedModelSubmission`, and computes one combined finite world-space bound from caller-transformed canonical vertices before a framebuffer can be returned;
- one global auto-fit transform frames the complete flat scene while preserving every relative entry transform;
- `OfflineSceneOrdering::InputOrder` delegates to the canonical `draw_prepared_model_list` executor;
- `OfflineSceneOrdering::BackToFront` delegates to the M72 painter-order helper and therefore inherits its stable entry-level ordering and bounded vertex-program restriction instead of duplicating sorting/raster logic;
- optional environment background remains a scene-global pre-geometry pass, while optional environment diffuse/reflection settings are injected into each entry through the existing fixed-light path with authoritative preview-camera viewer binding and conflict rejection;
- empty scenes are deterministic clear/environment-only renders;
- null assets, unsupported ordering, invalid model/options state, non-finite or unprojectable transformed positions, and unrepresentable combined bounds reject deterministically;
- regressions lock deterministic heterogeneous 4x output, combined-fit invariance under global uniform scale/translation, back-to-front byte/hash equivalence to explicit far-then-near source-alpha submission, observable difference from near-first composition, empty/environment-only behavior, and invalid later-entry rejection;
- this slice does not add hierarchy, parent-child transforms, animation, persistent scene storage, automatic opaque/transparent classification, draw/triangle sorting, OIT, GUI rendering, GPU execution, or performance/image-quality claims.

PR candidate: `#89`, initial exact implementation head `91a72f6c3dd638cf94f7c2429f06d2056b6ce756` passed Ubuntu, macOS, and ASan/UBSan build/test/sample gates before this status synchronization. Because this status update changes the exact candidate head, the final head must pass the same three gates again before integration.

## Architectural invariants

- One CPU raster path owns clipping, culling, fixed-point top-left coverage, interpolation, shading/program execution, sample coverage, stencil/depth, blending, and color writes.
- `Framebuffer` remains the authoritative per-sample ownership primitive; higher layers validate and submit rather than duplicating ownership semantics.
- Model/prepared/list submission validates complete state before writes when later invalid state could otherwise partially commit earlier work.
- Submission-order helpers may reorder already-prepared work, but they must delegate execution to the canonical prepared-list path rather than creating a second raster path.
- Offline scene orchestration owns only bounded preparation, combined framing, environment injection, and executor selection; it does not own raster/material/depth/blend semantics.
- Texture roles and environment lookups reuse `Texture2D`, sampler validation, mip generation, transfer semantics, and gradient sampling rather than maintaining role-specific filters.
- Imported asset textures use shared ownership; prepared submissions retain resource lifetime independently from source-object lifetime.
- Headless/tooling controls configure existing library state rather than creating CLI-only rendering behavior.
- Default/trailing state additions preserve historical behavior unless the caller explicitly opts into the new capability.
- Performance claims require controlled measurements; CI duration is never treated as a benchmark.

## Promotion after Milestone 74

Do not race the reserved M73 prepared-spatial-metadata surface. If M73 converges and integrates, rebase/re-audit against that exact main and use first-class prepared spatial metadata to promote flat-scene ordering from whole-entry painter keys toward verified draw-granularity ordering and post-vertex-program geometry where the metadata contract can actually support it. If that transparency path reaches diminishing returns, the next architectural promotion should be an explicit material/BRDF model boundary with deterministic semantics and evidence rather than additional MTL map-name farming or rebranding the current bounded Phong/Ns path as PBR.
