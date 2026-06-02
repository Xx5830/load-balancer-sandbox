#Help
default:
    @just --list --unsorted

#Очистка build
clear:
    cmake -E rm -rf build

#Конфигурация cmake
setup preset = "debug":
    cmake --preset {{preset}}

#Сборка проекта
build preset = "debug":
    cmake --build --preset {{preset}}

#Запуск тестов
test preset = "debug": build  
    ctest --preset {{preset}}

#Группируем проект в файл
token :
    npx repomix