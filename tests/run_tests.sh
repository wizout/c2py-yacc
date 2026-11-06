#!/usr/bin/env bash
# Кожну програму запускаємо трьома способами і порівнюємо вивід:
#   1) скомпільовану gcc (еталон),
#   2) нашим інтерпретатором AST (c2py --run),
#   3) згенерований Python (c2py --py | python3).
cd "$(dirname "$0")/.."
BIN=./build/c2py
TMP=$(mktemp -d)
pass=0; fail=0

for src in examples/*.c tests/cases/*.c; do
    name=$(basename "$src" .c)
    gcc -w -o "$TMP/$name" "$src" -lm 2>/dev/null || { echo "SKIP $src (gcc failed)"; continue; }
    "$TMP/$name" > "$TMP/gcc.out"
    $BIN --run "$src" > "$TMP/run.out" 2>&1
    $BIN --py "$src" -o "$TMP/$name.py" && python3 "$TMP/$name.py" > "$TMP/py.out" 2>&1

    if cmp -s "$TMP/gcc.out" "$TMP/run.out" && cmp -s "$TMP/gcc.out" "$TMP/py.out"; then
        printf "  ok    %s\n" "$src"
        pass=$((pass + 1))
    else
        printf "  FAIL  %s\n" "$src"
        diff "$TMP/gcc.out" "$TMP/run.out" | sed 's/^/        run: /' | head -5
        diff "$TMP/gcc.out" "$TMP/py.out"  | sed 's/^/        py:  /' | head -5
        fail=$((fail + 1))
    fi
done

# програми з помилками мають завершуватись з ненульовим кодом
for src in examples/errors/*.c; do
    if $BIN --run "$src" > /dev/null 2>&1; then
        printf "  FAIL  %s (error not detected)\n" "$src"; fail=$((fail + 1))
    else
        printf "  ok    %s (error detected)\n" "$src"; pass=$((pass + 1))
    fi
done

rm -rf "$TMP"
echo "passed: $pass, failed: $fail"
[ $fail -eq 0 ]
