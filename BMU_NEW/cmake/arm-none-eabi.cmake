find_program(ARM_GCC arm-none-eabi-gcc REQUIRED)
find_program(ARM_GDB arm-none-eabi-gdb REQUIRED)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(ARM_SIZE arm-none-eabi-size REQUIRED)

message(STATUS "Arm GCC: ${ARM_GCC}")
message(STATUS "Arm GDB: ${ARM_GDB}")

