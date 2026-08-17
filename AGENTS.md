# AI Agent Guidelines

This file is to be read by all AI agents operating in this repository.

1. **ABSOLUTE HARDWARE GUARDRAILS (NO HARDWARE MODIFICATIONS)**:
   - **STRICTLY FORBIDDEN**: Never suggest, propose, instruct, or assume resistors, diodes, capacitors, level shifters, trace cuts, or rewiring. The hardware is fixed with DIRECT wiring (3-wire SPI with SDIO connected directly to P0.17).
   - All solutions MUST be 100% pure firmware, Devicetree, driver code (`drivers/input/pmw3610.c`), or configuration (`.conf` / `.keymap`).
2. **Always Update Diagnostics**: Whenever you formulate a new hypothesis, make a breakthrough, or change the testing approach, you MUST explicitly document it in `diagnostic_memory.md`. Do not wait for the user to ask you to update it.
3. **Be Objective, Not Overconfident**: When debugging low-level hardware or firmware issues, present theories as hypotheses, not absolute facts. Wait for log verification before celebrating.
4. **Check Your History**: Always review previous changes before making large architectural shifts to avoid regressions or re-introducing deleted code. Consult `diagnostic_memory.md` thoroughly before proposing changes.
5. **No Regressions on Stable Baselines**: If the trackball is communicating and moving the cursor, do not destroy the baseline driver architecture. Optimize iteratively.
