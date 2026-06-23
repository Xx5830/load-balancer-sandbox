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
test preset = "debug": build  
    ctest --preset {{preset}}

#Группируем проект в файл
token :
    npx repomix

# Запуск бенчмарка с пресетом и сохранением результата
run preset_input output preset="release": build
    @mkdir -p $(dirname {{output}})
    ./build/{{preset}}/bin/main {{preset_input}} {{output}}
