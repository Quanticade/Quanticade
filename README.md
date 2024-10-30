## <p align="center"><img src=Quanticade.png alt="Quanticade Chess Engine" width="25%" height="25%"/></p>

## Overview

Quanticade is a strong uci command-line chess engine written in C in development since late 2023.

## Playing Strength

Quanticade Aurora is currently ~3461 CCRL Blitz.

Electra version estimated to be ~3500-3550

## History

Quanticade is based on BitBoard Chess engine by Maksim Korzh
While some of the code still remains, nearly all parts of original engine have been touched or rewritten completely

### Supported UCI Commands

* **Hash** (int) Sets the size of hash table in MB
* **Threads** (int) Sets the number of threads to search with
* **EvalFile** (string) Path to the NNUE network
* **ClearHash** (button) Clears the hash table

## Credits

- Maksim Korzh for his BitBoard Chess youtube series
- BlueFeverSoft for his VICE chess series
- TerjeKir for Weiss
- Stockfish for creating a clearly and easily readable codebase with many great ideas
- All of the people on fury bench for putting up with me
- SF Discord for helping me and giving me good ideas
- NotBaltic for creating the Quanticade logo
