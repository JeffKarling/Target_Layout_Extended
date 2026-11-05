# Documentation and natural writing style

## Core mandate

Agents must write all documentation and markdown files in natural English. Automated prose must avoid formulaic patterns, robotic title casing and exaggerated punctuation.

Clear technical prose relies on concise sentences separated by periods rather than nested clauses joined by punctuation.

## Sentence case and natural capitalization

Avoid title casing common nouns, concepts and technical phrases. Write "logical design" or "physical design" rather than capitalizing terms. Similarly, write "precompiled headers" and "link time optimization".

Use sentence case for all markdown headings, table headers and bold list labels. For example, write `## Compiler enforced architecture` and `**Private implementation.**`.

Reserve capital letters strictly for true proper nouns, acronyms and the first word of a sentence. Proper nouns include CMake, Ninja and Cargo. Acronyms include DAG, PFL, LTO and ABI.

## Elimination of parenthetical asides

Never place parentheses inside titles or markdown headings. Headings must be concise, direct and assertive without hedging.

Do not use parentheses to wedge afterthoughts, clarifications or lists of examples into sentences. Discard superfluous qualifications or integrate essential details directly into the sentence using natural phrasing. Reserve parentheses strictly for language syntax such as function calls and genuine mathematical expressions.

## Comma limits per sentence

A complete sentence should preferably use at most one comma. A sentence must never exceed two commas.

Split multi-clause sentences into shorter sentences instead of chaining clauses with commas.

## Punctuation tells and list formatting

Do not put colons in titles or markdown headings. Avoid formulaic colon lead-ins like `Crucially:` or introductory sentences ending in a colon before every block or list.

Avoid repetitive bold-colon bullet lists like `- **Label:** text`. Favor coherent prose paragraphs or clean bold labels punctuated with a period (`**Label.** Text`).

Do not use em dashes or hyphens to splice sentences or insert dramatic pauses. Write clean, complete sentences separated by periods.

Minimize compound hyphenated words. Prefer "one to one" or "colocation" over hyphenated forms. Prefer "self contained", "zero friction" and "fine grained" unless required by code identifiers.
