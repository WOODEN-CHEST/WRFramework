gcc ../source/*.c -I"../include" -I"../include/libs" -std=c2x -Werror -Wall -Wextra -Wpedantic -O3 -c -L"../lib" -l:libraylib.a -lGL -lm -lpthread -ldl -lrt -lX11
ar rcs libwr.a *.o