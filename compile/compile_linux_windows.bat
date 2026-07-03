gcc ../source/*.c -I"../include" -I"../include/libs" -std=c2x -Werror -Wall -Wextra -Wpedantic -O3 -c -L"../lib" -l:libraylib.a
ar rcs libwr.a *.o