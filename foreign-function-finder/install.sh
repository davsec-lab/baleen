#!/bin/sh

cargo install --path binary --force
cargo install --path driver --force

rm -rf .log
mkdir .log