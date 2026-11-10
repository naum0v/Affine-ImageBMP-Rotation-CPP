cmake_minimum_required(VERSION 3.12)
project(AffineImageBMPRotation)

# Добавляем исходники из папки src
add_executable(code "src/code.cpp")

# Устанавливаем стандарт C++
set_property(TARGET code PROPERTY CXX_STANDARD 20)
set_property(TARGET code PROPERTY CXX_STANDARD_REQUIRED ON)

# Необходимо для работы std::thread (многопоточность)
find_package(Threads REQUIRED)
target_link_libraries(code PRIVATE Threads::Threads)