# Code quality report

Date: 2026-07-13

## Scope

Analyzed project-owned C++ under `app`, `engine/include`, `engine/public`, and `engine/src`.
Generated files and `engine/externals` are excluded from actionable results.

## Tools and quality gate

- Lizard 1.23.0
- Cppcheck 2.21.0
- Lizard hard gate: cyclomatic complexity > 20, function length > 120 lines,
  or more than 8 parameters
- Cppcheck: `--enable=all --inconclusive --check-level=exhaustive`, C++20, Win64

The first exploratory Lizard pass intentionally used stricter advisory thresholds
(CCN 15, 80 lines, 6 parameters). Those findings are retained in `initial` and
`iteration-2`; the hard gate uses limits that identify high-risk functions without
forcing related renderer arguments into artificial parameter objects.

## Results

| Pass | Cppcheck actionable | Lizard advisory/hard-gate | Build |
|---|---:|---:|---|
| Initial | 35 project findings | 50 advisory findings | Not run in this pass |
| Iteration 1 | 4 | 49 advisory findings | Failed: one incomplete parameter rename |
| Iteration 2 | 0 | 49 advisory findings | Debug/x64 passed |
| Final hard gate | 0 | 0 | Debug/x64 passed |

The raw initial Cppcheck XML contains 57 entries: 35 project findings, 21 findings
from DirectXTex headers pulled in during preprocessing, and one checker-information
entry. Later passes explicitly exclude external code.

## Corrections made

- Replaced access to a moved-from rollback vector with a swap-based drain.
- Removed redundant quaternion initialization and simplified integer conversion.
- Corrected declaration/definition parameter-name mismatches and const contracts.
- Applied const/static qualifications reported by Cppcheck where semantically valid.
- Replaced raw search loops with standard range algorithms.
- Split particle input and impact-timeline processing out of the scene update.
- Isolated timeline threshold crossing to remove an analyzer false inference while
  preserving frame-crossing behavior.
- Split bind-pose and animated-bone overlays out of the CCN-27 debug-overlay function.

## Artifacts

- `initial/cppcheck.xml`: original Cppcheck output
- `initial/lizard-warnings.txt`: strict exploratory Lizard findings
- `initial/lizard-full.csv`: original Lizard metrics
- `iteration-1/cppcheck.xml`: first correction pass
- `iteration-2/cppcheck.xml`: zero-actionable Cppcheck pass
- `iteration-2/lizard-warnings.txt`: remaining strict advisory findings
- `final/cppcheck.xml`: final Cppcheck output
- `final/lizard-warnings.txt`: final hard-gate output (empty)
- `final/lizard-full.csv`: final metrics

## Verification

`CG4.slnx` was built with Visual Studio MSBuild 18.7.8 using Debug/x64. The final
executable is `generated/outputs/x64/Debug/CG4/CG4.exe`.
