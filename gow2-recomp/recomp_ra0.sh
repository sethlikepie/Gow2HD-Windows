set +e
cd /c/Users/softlive/Documents/self-projects/gow2_work/recomp_mid
t0=$(date +%s)
N=$(wc -l < ra0_changed.txt)
echo "=== recompilando $N chunks RA=0-fixed (-P 3) $(date +%H:%M:%S) ==="
cat ra0_changed.txt | xargs -P 3 -I {} sh -c 'g++ -std=c++20 -O0 -c -I . "$1" -o "$1.o" 2>"$1.ra0cc"' _ {}
dur=$(( $(date +%s)-t0 ))
ERR=0; FAILED=""
while read f; do
  e=$(grep -c 'error:' "$f.ra0cc" 2>/dev/null)
  if [ "$e" -gt 0 ]; then ERR=$((ERR+e)); FAILED="$FAILED $f"; fi
done < ra0_changed.txt
echo "=== recompile FIM dur=${dur}s | erros=$ERR ==="
[ -n "$FAILED" ] && { echo "CHUNKS COM ERRO:$FAILED"; for f in $FAILED; do echo "--- $f ---"; grep -m2 'error:' "$f.ra0cc"; done; }
echo "DONE_RECOMP_RA0"
