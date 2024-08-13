# Quanticade

## Overview

Quanticade is a decent uci command-line chess engine written in C in development since late 2023.

## Playing Strength

Currently Quanticade isnt rated on any website (Testing in progress).

Estimated elo is currently ~3400.

## History

Quanticade is based on BitBoard Chess engine by Maksim Korzh
While some of the code still remains, nearly all parts of original engine have been touched or rewritten completely

### Supported UCI Commands

* **Hash** (int) Sets the size of hash table in MB
* **Threads** (int) Sets the number of threads to search with
* **Use NNUE** (bool) Enables or disables the usage of NNUE
* **EvalFile** (string) Path to the NNUE network
* **ClearHash** (button) Clears the hash table

## Credits

- Maksim Korzh for his BitBoard Chess youtube series
- BlueFeverSoft for his VICE chess series
- TerjeKir for Weiss
- Stockfish for creating a clearly and easily readable codebase with many great ideas
- All of the people on fury bench for putting up with me
- SF Discord for helping me and giving me good ideas
