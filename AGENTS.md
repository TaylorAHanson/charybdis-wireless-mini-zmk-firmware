# AI Agent Guidelines

This file is to be read by all AI agents operating in this repository.

1. **Always Update Diagnostics**: Whenever you formulate a new hypothesis, make a breakthrough, or change the testing approach, you MUST explicitly document it in the `diagnostic_memory.md` artifact (or similar tracking document). Do not wait for the user to ask you to update it.
2. **Be Objective, Not Overconfident**: When debugging low-level hardware or firmware issues, present theories as hypotheses, not absolute facts. Wait for log verification before celebrating.
3. **Check Your History**: Always review previous changes before making large architectural shifts to avoid regressions or re-introducing deleted code.
