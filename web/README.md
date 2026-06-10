# lbweb

Локальный сайт с результатами тестирования балансировки нагрузки.

## Запуск

```bash
cd web
python -m venv .venv && source .venv/bin/activate
pip install -e .

lbweb-demo  # сгенерировать data/results.json
lbweb       # http://127.0.0.1:8000
```

## Разработка

```bash
pip install -e ".[dev]"
ruff check lbweb/
mypy lbweb/
```
