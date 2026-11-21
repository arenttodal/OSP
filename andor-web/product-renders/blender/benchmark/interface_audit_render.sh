#!/bin/bash
# Each campaign still's camera rendered twice at 25 % with few samples: 17 Nov interface vs the frozen beta.
cd /home/user/OSP/andor-web
PY=/tmp/claude-0/-home-user/7d8f5fab-1322-5a3a-abe0-75a07a55398b/scratchpad/bpyenv/bin/python; SC=product-renders/blender/scripts/andor_scene.py
O=/tmp/claude-0/-home-user/7d8f5fab-1322-5a3a-abe0-75a07a55398b/scratchpad/iaudit
# scale: each shot at about the size a website shows it (1920 wide landscape, about 1080-1170 wide portrait)
for js in "ANDOR_GALLERY A1_R1 0.75" "ANDOR_GALLERY A2 0.8" "ANDOR_MATERIAL M01_sharp 0.8" "ANDOR_MATERIAL M02_sharp 0.64" "ANDOR_FLOATING B1_R4 0.6" \
          "ANDOR_GALLERY A1M_R1 1.0" "ANDOR_MATERIAL M03_sharp 0.64" "ANDOR_MATERIAL M04_sharp 0.64" "ANDOR_FLOATING B2_115_R4 0.75" \
          "ANDOR_MATERIAL M05_sharp 0.64" "ANDOR_MATERIAL M06_R2_sharp 0.64" "ANDOR_GALLERY A1_R1_W 0.75"; do
  set -- $js
  for t in current 0.9.0-beta.1; do
    out=$O/${2}__$t.png; [ -f $out ] && continue
    $PY $SC -- --setup $1 --shot $2 --scale $3 --samples 16 --noise 0.05 --textures product-renders/blender/textures_$t --out $out > $O/${2}__$t.log 2>&1 || echo "FAIL $2 $t"
  done
  echo "$(date +%T) $2"
done
echo AUDIT_DONE
