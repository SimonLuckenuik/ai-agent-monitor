# Research

- A gz image is about 70% of the plain one. gz size crossing a 4 KB sector boundary
  lowers the limit by 4 KB.
- The stock page refuses names not starting with `SDP` but does not check contents; the
  stock firmware cannot unpack `.gz`.
- The USB port is power only, so "flash over USB" is not an install path; the plain full
  image (over the limit) is UART-only and not released.
