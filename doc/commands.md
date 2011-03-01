# Speculation on the commands

## Endpoints

### 1

* input

### 2

* output
* raw data
* pixel data

### 3

* commands

## Speculation on the structure

### Bits 0-6

* seem to be 0 for each command

### Bit 7

* seems to be the command type
* seems to determine the size

## Guessed Commands

### Turn the LED on

    [0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 252, 108, 48]

