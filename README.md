## <p align="center"><img src=Quanticade.png alt="Quanticade Chess Engine" width="25%" height="25%"/></p>

## Overview

Quanticade is a strong uci command-line chess engine written in C in development since late 2023 with NNUE trained on Lc0 Data.
Quanticade usually ranks around the 15th place in the world.

## Playing Strength

|         | CCRL Blitz    | CCRL 40/15 | CEGT 40/20 |
|---------|---------------|------------|------------|
| Orion   | 3685 (Top 15) | 3557       | Untested   |
| Fenrir  | 3669 (Top 18) | 3561       | Untested   |
| Chimera | 3619          | 3515       | Untested   |
| Electra | 3552          | 3487       | 3410       |
| Aurora  | 3465          | 3406       | Untested   |
| 0.7     | Untested      | Untested   | Untested   |

## History

Quanticade is based on BitBoard Chess engine by Maksim Korzh
While some of the code still remains, nearly all parts of original engine have been touched or rewritten completely

### Supported UCI Commands

* **Hash** (int) Sets the size of hash table in MB
* **Threads** (int) Sets the number of threads to search with
* **MoveOverhead** (int) Milliseconds to account for UCI->GUI->UCI communication overhead
* **EvalFile** (string) Path to the NNUE network
* **ClearHash** (button) Clears the hash table

## Credits

- Maksim Korzh for his BitBoard Chess youtube series
- BlueFeverSoft for his VICE chess series
- TerjeKir for Weiss
- Stockfish for creating a clearly and easily readable codebase with many great ideas
- JW for bullet NNUE trainer
- All of the people on fury bench for putting up with me
- SF Discord for helping me and giving me good ideas
- NotBaltic for creating the Quanticade logo
