# Quanticade

## Overview

Quanticade is an decent uci command-line chess engine written in C in development since late 2023.

## Playing Strength

Currently Quanticade isnt rated on any website (We are looking to get it rated on CCRL and others).

Estimated elo is currently 2800-2900.

## History

Quanticade is based on BitBoard Chess engine by Maksim Korzh

Version 0.04 was very weak at aprox 1600 elo. There were only the very basic things implemented to get a match running.

Version 0.1 then bumped this to 1900 elo with some basic pruning and etc. Things started to get serious with version 0.2 and 0.3 which
were 2000 and 2500 respectively. 0.3 also introduced Stockfish 12 NNUE network which helped a ton.

0.4 was a test version or beta release for 0.5 Dev which is the latest as of 8.1.2024 clocking at 2900 elo aproximately.

### Supported UCI Commands

* **Hash** (int) Sets the size of hash table in MB
* **Use NNUE** (bool) Enables or disables the usage of NNUE
* **EvalFile** (string) Path to the NNUE network
* **ClearHash** (button) Clears the hash table

## Credits

- Maksim Korzh for his BitBoard Chess youtube series
- BlueFeverSoft for his VICE chess series
- Daniel Shawul for his nnue probing library
- Stockfish for creating a clearly and easily readable codebase with many great ideas



