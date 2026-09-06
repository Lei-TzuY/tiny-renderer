# tiny-renderer current status

          This file is the compact live capability/status layer for the repository. `ROADMAP.md` retains the detailed milestone record. A milestone is closed only after its exact `main` commit passes Linux, macOS, and ASan/UBSan CI.

          ## Integrated architecture through Milestone 42

          Milestones 1–35 establish the deterministic CPU raster pipeline, indexed meshes and generalized varyings, fixed-point coverage/interpolation, depth/stencil/blend ownership, viewport/scissor, 4x MSAA, material/texture import, opacity/A2C, directional shadows, alpha-tested cutouts, and bounded fragment/vertex programs. Milestones 36–42 extend the same path with tangent-space normal mapping, Blinn-Phong specular lighting, point lights, caller-ordered fixed multi-light accumulation, point-light cubemap shadows, spotlight shading, and bounded spotlight shadow mapping.

          The current main line also integrates bounded OBJ relative indices, polygon triangulation, smoothing/generated normals, deterministic model inspection/fingerprints, multiple sibling MTL libraries, and bounded 24-bit TGA texture import through the shared image dispatch path.

          ## Milestone 43 — deterministic RGB light color

          The current integration candidate extends the existing directional, point, spotlight, shadow, normal-map, specular, fragment-program, and prepared-model pipeline with bounded per-light RGB color rather than a parallel shading path.

          Implemented acceptance surface:

          - directional, point, and spotlight records own finite `[0,1]` RGB color with white defaults that preserve established output when unused;
          - one common component-wise rule tints ambient, Lambert diffuse, and Blinn-Phong specular contributions; attenuation, cone falloff, and shadow visibility remain scalar geometric/visibility factors;
          - colored ambient remains unshadowed exactly as before, while directional/point/spot shadows continue to suppress only the associated direct contribution;
          - legacy singular directional/point submission and `FixedLightCollection` share the same shading and validation semantics, including caller-order accumulation;
          - invalid color rejects during shared/static preflight before framebuffer mutation, and prepared plans retain the validated color state through list execution;
          - regression coverage locks white compatibility, analytic directional tinting, point/spot reuse, mixed RGB accumulation, tinted specular, shadow interaction, fragment-program ordering, fail-closed invalid state, and prepared-list byte/hash equivalence;
          - no spectral, color-temperature, sRGB, exposure/tonemapping, HDR photometry, PBR, GPU, or performance claim is made.

          ## Next frontier — Milestone 44

          Promote the fixed-light/shadow architecture from one singleton association per light type to **bounded per-record typed shadow bindings**. The executable slice should allow multiple caller-ordered fixed lights of the same type to retain independent validated shadow resources while preserving whole-list preflight, resource lifetime, selective visibility, and the existing single raster/shading path. It must not silently broaden into soft-shadow filtering, cascades, automatic allocation, or unbounded scene-graph state.
