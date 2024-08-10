cmake_minimum_required(VERSION 3.28)
project(Fp_test5 C)

set(CMAKE_C_STANDARD 11)

add_executable(Fp_test5 server.c
        tokenize.c
        tokenize.h
        tcp-utils.c
        tcp-utils.h
        config.c
        config.h
        fserv.c
        fserv.h
        threadpool.c
        threadpool.h
)
