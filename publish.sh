#!/usr/bin/env bash
# Створює репозиторій на GitHub і завантажує туди проєкт.
# Потрібен GitHub CLI (https://cli.github.com), один раз: gh auth login
set -e
cd "$(dirname "$0")"
NAME=${1:-c2py-yacc}

if [ ! -d .git ]; then
    git init -q -b main
    git add .
    git commit -q -m "Транслятор підмножини C на flex/bison: AST, інтерпретатор, генерація Python, візуалізація"
fi

if command -v gh >/dev/null 2>&1; then
    gh repo create "$NAME" --public --source=. --remote=origin --push \
        --description "LALR(1) translator for a C subset (flex + bison): AST, interpreter, C->Python, d3.js AST viewer"
    echo
    echo "Готово: $(gh repo view --json url -q .url)"
else
    echo "GitHub CLI не знайдено. Створіть порожній репозиторій $NAME на github.com/new і виконайте:"
    echo "  git remote add origin https://github.com/<ваш-логін>/$NAME.git"
    echo "  git push -u origin main"
fi
