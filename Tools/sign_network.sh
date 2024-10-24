#!/bin/bash

echo "This script will take a file with name nn.nnue and will sign it as prefix"

touch temp.nnue
echo -n "4275636B657432303438" > temp.nnue
cat nn.nnue >> temp.nnue
mv nn.nnue nn.nnue.bak
mv temp.nnue nn.nnue

echo "Your signed NNUE network is in nn.nnue file"

