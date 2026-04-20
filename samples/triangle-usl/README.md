# triangle-usl

This sample is reserved for the real USL artifact path.

Status:
- `triangle-manual` is the working GPU runtime baseline.
- `triangle-usl` will consume a `.us` / `.uslib` artifact from the USL compiler.

Planned load flow:
1. Load `triangle_tint.us` or `triangle_tint.uslib`
2. Read required runtime metadata from the artifact
   - entry points
   - resource reflection
3. Use embedded Metal backend payload if present
4. Otherwise compile from the artifact and optionally populate backend cache
5. Create shader library, bind-group layout, bind group, pipeline, then draw

Expected artifact policy:
- Required metadata lives in the artifact
- Optional backend payloads may live in the artifact
- Manual sample and USL sample stay separate so GPU/runtime regressions and compiler regressions are easy to isolate

Related USL-side docs:
- `/Users/recp/Projects/recp/UniversalShading/us/DESIGN.md`
- `/Users/recp/Projects/recp/UniversalShading/us/BYTECODE_SPEC.md`
- `/Users/recp/Projects/recp/UniversalShading/us/SYNTAX_STDLIB_STATUS.md`
