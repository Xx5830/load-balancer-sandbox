set shell := ["sh", "-c"]
set windows-shell := ["cmd.exe", "/c"]

#Help
default:
    @just --list --unsorted

#Очистка build
clear:
    cmake -E rm -rf build

#Конфигурация cmake
setup preset = "release":
    cmake --preset {{preset}}

#Сборка проекта
build preset = "release":
    cmake --build --preset {{preset}}

#Запуск тестов
test preset = "debug": (build preset)
    ctest --preset {{preset}}

#Группируем проект в файл
token :
    npx repomix

# Запуск бенчмарка с пресетом и сохранением результата
run preset_input output preset="release": build
    cmake -E make_directory {{parent_directory(output)}}
    ./build/{{preset}}/bin/main {{preset_input}} {{output}}

# Запустить веб-интерфейс (http://127.0.0.1:8000)
web:
    @if [ ! -f web/.venv/bin/python ]; then echo "Сначала создайте venv: cd web && python -m venv .venv && pip install -e ."; exit 1; fi
    cd web && .venv/bin/python -m lbweb.app