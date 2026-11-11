# Project report (LaTeX)

`nova_report.pdf` is the full report (author: Jaswinder Singh, ID 1234090, Compiler Design project).

Rebuild from scratch (re-measures every number on your machine; takes a few minutes because it runs the fuzzers and benchmarks):

```bash
python3 report/tools/gen_data.py          # runs compiler, tests, fuzzers, benchmarks -> report/gen/*.tex
cd report && pdflatex nova_report && pdflatex nova_report && pdflatex nova_report
```
`img/` holds the studio screenshots (taken with a headless browser driving the real UI). Needs `pdflatex` with TikZ, pgfplots, tcolorbox, listings (TeX Live).
