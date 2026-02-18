## 2024-05-22 - [Optimizing small protocol functions]
**Learning:** Inlining small accessor functions (unpack_*) and moving them to headers provided a significant performance boost (~58% for signed 20-bit unpack) in Release builds, even when adding extra masking instructions for safety.
**Action:** Always prefer inlining very small, frequently called helper functions (like bit manipulation/unpacking) in headers, but ensure to benchmark in Release mode as Debug mode can mislead about overhead.
