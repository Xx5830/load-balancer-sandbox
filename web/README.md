# lbweb

Локальный сайт: ввод пресета, запуск C++-бенчмарка и анализ результатов балансировки.

Пресет и результат описаны схемами в `schems/` (`preset-schema.json`, `result-schema.json`).
Запуск гоняет C++-бинарь из `build/` по одному прогону на выбранный алгоритм.

## Запуск

```bash
just build              # собрать C++-бинарь (из корня репозитория)
cd web
python -m venv .venv && source .venv/bin/activate
pip install -e .

lbweb-demo  # записать пример пресета в data/presets/example.json
lbweb       # http://127.0.0.1:8000
```

## Разработка

```bash
pip install -e ".[dev]"
ruff check lbweb/
mypy lbweb/
```
