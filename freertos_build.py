Import("env")
from os.path import join

cube = env.PioPlatform().get_package_dir("framework-stm32cubef4")
rtos = join(cube, "Middlewares", "Third_Party", "FreeRTOS", "Source")
env.Append(CPPPATH=[join(rtos, "include"), join(rtos, "portable", "GCC", "ARM_CM4F")])
env.BuildSources(join("$BUILD_DIR", "freertos"), rtos, src_filter=["+<tasks.c>", "+<queue.c>", "+<list.c>", "+<portable/GCC/ARM_CM4F/port.c>", "+<portable/MemMang/heap_4.c>"])
