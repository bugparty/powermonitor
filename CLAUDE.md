# CLAUDE.md

## Step 1

Read the first word of the request, and read an additional instruction file when it is:
- "design": REPO-ROOT/.github/prompts/design.prompt.md
- "verify": REPO-ROOT/.github/prompts/verify.prompt.md
- "ask": REPO-ROOT/.github/prompts/ask.prompt.md
- "investigate": REPO-ROOT/.github/prompts/investigate.prompt.md
- "code": REPO-ROOT/.github/prompts/code.prompt.md
- "review": REPO-ROOT/.github/prompts/review.prompt.md

### Exceptions

- If the first word is not in the list:
  - Follow REPO-ROOT/.github/prompts/code.prompt.md
  - Skip Step 2

## Step 2

Only applies when the first word is "design" or "investigate".
Read the second word if it exists, convert it to a title `# THE-WORD`.

## Step 3

Keep the remaining as is.
Treat the processed request as "the LATEST chat message" in the additional instruction file.
Follow the additional instruction file and start working immediately, there will be no more input.

## Examples

When the request is `ask how does the parser resync on noise`, follow `ask.prompt.md` and "the LATEST chat message" becomes:
```
how does the parser resync on noise
```

When the request is `design problem add PING message`, follow `design.prompt.md` and "the LATEST chat message" becomes:
```
# Problem
add PING message
```

When the request is `fix the CRC bug in parser`, since the first word is not in the list, follow `code.prompt.md` and "the LATEST chat message" becomes:
```
fix the CRC bug in parser
```

## Web Viewer Data Samples

- Agents generating JSON data for the web viewer must follow the reference format in `web_viewer/src/dataset/agent_format_sample.json`.
- When designing or reviewing web viewer UI, load `web_viewer/src/dataset/full_capability_sample.json` as the reference dataset.
