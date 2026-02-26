## 2024-05-22 - [Optimizing small protocol functions]
**Learning:** Inlining small accessor functions (unpack_*) and moving them to headers provided a significant performance boost (~58% for signed 20-bit unpack) in Release builds, even when adding extra masking instructions for safety.
**Action:** Always prefer inlining very small, frequently called helper functions (like bit manipulation/unpacking) in headers, but ensure to benchmark in Release mode as Debug mode can mislead about overhead.

## 2024-05-23 - [Optimizing TimelineChart Range Filtering]
**Learning:** Replaced O(N) array filtering with O(log N) binary search for visible points in a timeline chart. This is critical for performance when dealing with large datasets (millions of points). The `useMemo` hook was re-calculating on every `range` update (pan/zoom), causing significant lag.
**Action:** Always check for linear scans on large datasets in hot paths (like render loops or frequent effect dependencies) and replace with binary search if the data is sorted.
