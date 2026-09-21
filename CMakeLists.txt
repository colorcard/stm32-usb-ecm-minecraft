cmake_minimum_required(VERSION 3.22)

#
# STM32G474 —— USB CDC-ECM 以太网 + lwIP + Minecraft 服务端工程
#
# 目录结构：
#   Drivers/            STM32 HAL / CMSIS（厂商底层）
#   libraries/sdk/      system、HAL 配置、newlib 桩
#   libraries/rp_common 公共层（时钟、延时、BSP 入口、中断）
#   libraries/rp_driver 芯片外设驱动层
#   libraries/rp_device 外接设备驱动层（含 USB ECM 设备层）
#   libraries/usb       USB Device 栈配置、描述符与 ECM 类
#   libraries/net       lwIP 移植、ECM 网卡驱动与 DHCP 服务器
#   libraries/third_party/lwip  lwIP 源码
#   project/user        框架入口（main / isr）
#   project/code        Minecraft 协议服务端
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Rapfi_Embedded)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# ---------------------------------------------------------------------------
# 厂商 HAL / CMSIS 源码
# ---------------------------------------------------------------------------
set(HAL_DIR ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src)

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # 启动文件与厂商底层
    ${CMAKE_SOURCE_DIR}/startup_stm32g474xx.s
    ${CMAKE_SOURCE_DIR}/libraries/sdk/system_stm32g4xx.c
    ${CMAKE_SOURCE_DIR}/libraries/sdk/syscalls.c
    ${CMAKE_SOURCE_DIR}/libraries/sdk/sysmem.c

    # HAL 驱动
    ${HAL_DIR}/stm32g4xx_hal.c
    ${HAL_DIR}/stm32g4xx_hal_cortex.c
    ${HAL_DIR}/stm32g4xx_hal_dma.c
    ${HAL_DIR}/stm32g4xx_hal_dma_ex.c
    ${HAL_DIR}/stm32g4xx_hal_exti.c
    ${HAL_DIR}/stm32g4xx_hal_flash.c
    ${HAL_DIR}/stm32g4xx_hal_flash_ex.c
    ${HAL_DIR}/stm32g4xx_hal_flash_ramfunc.c
    ${HAL_DIR}/stm32g4xx_hal_gpio.c
    ${HAL_DIR}/stm32g4xx_hal_pwr.c
    ${HAL_DIR}/stm32g4xx_hal_pwr_ex.c
    ${HAL_DIR}/stm32g4xx_hal_rcc.c
    ${HAL_DIR}/stm32g4xx_hal_rcc_ex.c
    ${HAL_DIR}/stm32g4xx_hal_iwdg.c
    ${HAL_DIR}/stm32g4xx_hal_spi.c
    ${HAL_DIR}/stm32g4xx_hal_spi_ex.c
    ${HAL_DIR}/stm32g4xx_hal_tim.c
    ${HAL_DIR}/stm32g4xx_hal_tim_ex.c
    ${HAL_DIR}/stm32g4xx_hal_uart.c
    ${HAL_DIR}/stm32g4xx_hal_uart_ex.c
    ${HAL_DIR}/stm32g4xx_hal_pcd.c
    ${HAL_DIR}/stm32g4xx_hal_pcd_ex.c
    ${HAL_DIR}/stm32g4xx_ll_usb.c

    # 公共层
    ${CMAKE_SOURCE_DIR}/libraries/rp_common/rp_common_bsp.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_common/rp_common_clock.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_common/rp_common_debug.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_common/rp_common_delay.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_common/rp_common_fault.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_common/rp_common_fifo.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_common/rp_common_interrupt.c

    # 芯片外设驱动层
    ${CMAKE_SOURCE_DIR}/libraries/rp_driver/rp_driver_gpio.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_driver/rp_driver_pit.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_driver/rp_driver_spi.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_driver/rp_driver_timer.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_driver/rp_driver_uart.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_driver/rp_driver_watchdog.c

    # 外接设备驱动层
    ${CMAKE_SOURCE_DIR}/libraries/rp_device/rp_device_key.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_device/rp_device_lcd_hw.c

    # 框架入口
    ${CMAKE_SOURCE_DIR}/project/user/src/main.c
    ${CMAKE_SOURCE_DIR}/project/user/src/isr.c
)

# ---------------------------------------------------------------------------
# USB Device（Type-C 原生 USB，CDC-ECM 以太网卡）
# ---------------------------------------------------------------------------
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/libraries/third_party/STM32_USB_Device_Library/Core/Src/usbd_core.c
    ${CMAKE_SOURCE_DIR}/libraries/third_party/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    ${CMAKE_SOURCE_DIR}/libraries/third_party/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    ${CMAKE_SOURCE_DIR}/libraries/usb/usbd_conf.c
    ${CMAKE_SOURCE_DIR}/libraries/usb/usbd_desc.c
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/libraries/usb
    ${CMAKE_SOURCE_DIR}/libraries/third_party/STM32_USB_Device_Library/Core/Inc
)

# ---------------------------------------------------------------------------
# 网络：lwIP（裸机） + USB CDC-ECM 网卡 + Minecraft 服务器
# ---------------------------------------------------------------------------
set(LWIP_DIR ${CMAKE_SOURCE_DIR}/libraries/third_party/lwip)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # ECM 设备类与设备层
    ${CMAKE_SOURCE_DIR}/libraries/usb/usbd_ecm.c
    ${CMAKE_SOURCE_DIR}/libraries/usb/usbd_ecm_if.c
    ${CMAKE_SOURCE_DIR}/libraries/rp_device/rp_device_usb_ecm.c

    # lwIP 核心（NO_SYS，IPv4，TCP/UDP）
    ${LWIP_DIR}/src/core/init.c
    ${LWIP_DIR}/src/core/def.c
    ${LWIP_DIR}/src/core/inet_chksum.c
    ${LWIP_DIR}/src/core/ip.c
    ${LWIP_DIR}/src/core/mem.c
    ${LWIP_DIR}/src/core/memp.c
    ${LWIP_DIR}/src/core/netif.c
    ${LWIP_DIR}/src/core/pbuf.c
    ${LWIP_DIR}/src/core/raw.c
    ${LWIP_DIR}/src/core/stats.c
    ${LWIP_DIR}/src/core/sys.c
    ${LWIP_DIR}/src/core/tcp.c
    ${LWIP_DIR}/src/core/tcp_in.c
    ${LWIP_DIR}/src/core/tcp_out.c
    ${LWIP_DIR}/src/core/timeouts.c
    ${LWIP_DIR}/src/core/udp.c
    ${LWIP_DIR}/src/core/ipv4/etharp.c
    ${LWIP_DIR}/src/core/ipv4/icmp.c
    ${LWIP_DIR}/src/core/ipv4/dhcp.c
    ${LWIP_DIR}/src/core/ipv4/ip4.c
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/src/core/ipv4/ip4_frag.c
    ${LWIP_DIR}/src/netif/ethernet.c

    # 移植与网卡驱动
    ${CMAKE_SOURCE_DIR}/libraries/net/rp_lwip.c
    ${CMAKE_SOURCE_DIR}/libraries/net/rp_netif_ecm.c
    ${CMAKE_SOURCE_DIR}/libraries/net/rp_dhcpd.c

    # Minecraft Java 协议服务器
    ${CMAKE_SOURCE_DIR}/project/code/mc_server.c
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/libraries/net
    ${LWIP_DIR}/src/include
)

# ---------------------------------------------------------------------------
# 头文件搜索路径
# ---------------------------------------------------------------------------
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32G4xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
    ${CMAKE_SOURCE_DIR}/libraries/sdk
    ${CMAKE_SOURCE_DIR}/libraries/rp_common
    ${CMAKE_SOURCE_DIR}/libraries/rp_driver
    ${CMAKE_SOURCE_DIR}/libraries/rp_device
    ${CMAKE_SOURCE_DIR}/project/code
)

# ---------------------------------------------------------------------------
# 宏定义
# ---------------------------------------------------------------------------
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32G474xx
    USBD_SUPPORT_USER_STRING_DESC=1
    $<$<CONFIG:Debug>:DEBUG>
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    m
)

# Generate .hex/.bin next to the .elf for flashing tools
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary -S
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.hex and .bin"
)
