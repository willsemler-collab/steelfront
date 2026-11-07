# Steel Front

A top-down tank battle for the **TI-84 Plus CE**, written in C (no TI-BASIC).

Fight waves of enemy tanks. Armor works by angle: shots into the front plate do
little damage (heavy tanks bounce them completely), side hits do more, and rear hits
do the most. Air raids start at wave 2, so watch for the red target circles. Green
crates repair you.

## Controls
| Key | Action |
|---|---|
| Up / Down | drive forward / reverse |
| Left / Right | turn hull |
| `alpha` / `X,T,θ,n` | turn turret left / right |
| `2nd` | fire |
| `clear` | quit |

## Building
1. Install the CE C toolchain (CEdev): https://ce-programming.github.io/toolchain/
2. In this folder run `make`. You get `bin/STLFRONT.8xp`.

Or push this folder to a GitHub repo. The included workflow builds the `.8xp`
for you (it shows up under the Actions tab as a downloadable artifact).

## Putting it on the calculator
1. Send `STLFRONT.8xp` with TI Connect CE.
2. Also send the **CE C libraries** (`clibs.8xg`, from https://tiny.cc/clibs).
   The game needs them.
3. On OS 5.5 or newer, TI blocks assembly/C programs. You need the
   arTIfiCE jailbreak (https://yvantt.github.io/arTIfiCE/) to run them.
   On older OS versions, run it with `Asm(prgmSTLFRONT)`.

The `test/` folder holds a PC test harness. It stubs the calculator libraries so the
game logic can be run under sanitizers. It isn't needed to build the game.
