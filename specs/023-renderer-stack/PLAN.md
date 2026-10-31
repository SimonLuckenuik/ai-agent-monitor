# Plan

1. Replace the recursive descent parser (`Expr` in `src/faces.cpp`) with operator
   precedence over two fixed stacks of 12 (values and operators), same precedence, same
   functions, same handling of malformed input; 12 deep or more gives 0.
2. `renderItems`: its per-item hashes static (it never runs twice at once); `repaintBox`
   not inlined, its sprite static.
3. Measure with `-fstack-usage` (`PLATFORMIO_BUILD_FLAGS=-fstack-usage
   PLATFORMIO_BUILD_DIR=.pio/su pio run`) and, if a path is still near the limit, split
   `drawItem` (448 B) per item type.
4. Unit checks for nesting; the integration case with an empty known list.
