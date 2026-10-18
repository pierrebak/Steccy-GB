#!/bin/sh
#-------------------------------------------------------------------------------------------------------------------------------------------
# run-tests.sh - telecharge des ROMs de test publiques et les execute avec gb-headless
#   ./tools/run-tests.sh          (depuis la racine du projet, apres "make -C gb-lx gb-headless")
#-------------------------------------------------------------------------------------------------------------------------------------------
set -e
cd "$(dirname "$0")/.."
mkdir -p testroms
cd testroms
B=https://raw.githubusercontent.com/retrio/gb-test-roms/master
[ -f cpu_instrs.gb ]   || curl -sL -o cpu_instrs.gb   $B/cpu_instrs/cpu_instrs.gb
[ -f instr_timing.gb ] || curl -sL -o instr_timing.gb $B/instr_timing/instr_timing.gb
[ -f halt_bug.gb ]     || curl -sL -o halt_bug.gb     $B/halt_bug.gb
[ -f dmg_sound.gb ]    || curl -sL -o dmg_sound.gb    $B/dmg_sound/dmg_sound.gb
[ -f dmg-acid2.gb ]    || curl -sL -o dmg-acid2.gb    https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb
H=../gb-lx/gb-headless

echo "== cpu_instrs (attendu : Passed all tests)";  $H cpu_instrs.gb 4000 2>/dev/null | tail -2
echo "== instr_timing (attendu : Passed)";          $H instr_timing.gb 600 2>/dev/null | tail -2
echo "== halt_bug : resultat affiche a l'ecran -> halt_bug.pgm"; $H halt_bug.gb 1500 halt_bug.pgm >/dev/null 2>&1
echo "== dmg-acid2 : image -> dmg-acid2.pgm (a comparer avec l'image de reference du projet dmg-acid2)"
$H dmg-acid2.gb 120 dmg-acid2.pgm >/dev/null 2>&1
echo "== dmg_sound : resultat a l'ecran -> dmg_sound.pgm (attendu : 8 tests ok sur 12)"
$H dmg_sound.gb 2400 dmg_sound.pgm son.wav >/dev/null 2>&1
echo "termine."
