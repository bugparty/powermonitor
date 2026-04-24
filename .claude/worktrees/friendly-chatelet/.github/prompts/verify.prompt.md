# Verifying

- Check out `Coding Guidelines` in `REPO-ROOT/AGENTS.md` for build and test instructions.
- All code changes should already be applied. Your goal is to verify they are correct.

## Step 1. Build

- Run `pwsh workflow.ps1` from `REPO-ROOT`.
- If there are compile errors:
  - Identify whether the issue is on the callee or caller side before changing anything.
  - Only fix warnings introduced by your change; do not touch pre-existing warnings.
- Re-run until the build is clean.

## Step 2. Test

- Run the full test suite via `pwsh workflow.ps1`.
- If a test fails:
  - Read the failing `ASSERT` output carefully.
  - To iterate on a single failing test: `./build_linux/bin/pc_sim_test --gtest_filter=SuiteName.TestName`
  - If the cause is unclear, add a temporary `std::cerr` to print variable state, then remove it after fixing.
  - Do not delete any existing test case.
- Re-run until all tests pass.

## Step 3. Check It Again

- Go back to Step 1 and follow all steps once more to confirm nothing was missed.

## Step 4. Confirm

- When the build is clean and all tests pass, report the result.
