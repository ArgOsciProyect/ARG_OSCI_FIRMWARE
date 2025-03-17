 . $HOME/esp/v5.3.1/esp-idf/export.sh
 sudo chmod 666 /dev/ttyUSB0
 find . \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -exec clang-format -i -style=file {} \;