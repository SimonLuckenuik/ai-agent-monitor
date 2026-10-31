# Progress

- 2026-10-31: proposed and approved in one step (the owner asked to fix the stack
  overflow found with spec 022). Iterative parser, `renderItems` statics, `repaintBox`
  not inlined; 286 unit checks (12 new). Chip frames in RESEARCH.
- 2026-10-31: `example_faces_memory` passes with no known crashes: all 39 example faces
  run under the clock's heap and stack (deepest 2,752 B counted on the host). Lowest free
  heap in that run: 2.5 KB, the next limit to watch.
