# Top-level build for the PROTECT C++ command-line ports.
#
# Each tool also builds standalone from its own cpp/ directory; this file
# builds the whole suite at once:
#
#   cmake -B build && cmake --build build
#
# Binaries land under build/<tool>/cpp/. Dependencies:
#   - A C++17 compiler
#   - SQLite3 development headers        (all DB-backed tools)
#   - libpng, libjpeg, ZBar              (imagebarcodesearch only)
#   - the `curl` binary at runtime       (the scraping/cloning tools)
cmake_minimum_required(VERSION 3.10)
project(PROTECT CXX)

add_subdirectory(ChatLogsParser/cpp)
add_subdirectory(acronym2Text/cpp)
add_subdirectory(linkScraper/cpp)
add_subdirectory(websiteCloner/cpp)
add_subdirectory(ImageMetadataScraper/cpp)
add_subdirectory(imageBarcodeSearch/cpp)
