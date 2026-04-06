# CLAUDE.md

- Read through `REPO-ROOT/AGENTS.md` before performing any work.
  - `AGENTS.md` is the guideline you should follow.
  - Following `Finding Documents` in `AGENTS.md`, find relevant documents on demand when you need specific knowledge.
  - Following `Coding Guidelines` in `AGENTS.md`, read all **(MUST READ)** documents before touching the source code.
- Interpret the request (in the latest chat message, not including conversation history) following the steps:

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
