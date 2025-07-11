idf_component_register(
SRCS
    "adc.c"
    "DS4432U.c"
    "EMC2101.c"
    "i2c_bitaxe.c"
    "INA260.c"
    "main.c"
    "nvs_config.c"
    "display.c"
    "screen.c"
    "input.c"
    "lvglDisplay.c"
    "system.c"
    "TPS546.c"
    "vcore.c"
    "work_queue.c"
    "nvs_device.c"
    "lv_font_portfolio-6x8.c"
    "logo.c"
    "mempoolAPI.c"
    "lvglDisplayBAP.c"
    "./http_server/http_server.c"
    "./http_server/theme_api.c"
    "./database/dataBase.c"
    "./self_test/self_test.c"
    "./tasks/stratum_task.c"
    "./tasks/create_jobs_task.c"
    "./tasks/asic_task.c"
    "./tasks/asic_result_task.c"
    "./tasks/power_management_task.c"

INCLUDE_DIRS
    "."
    "tasks"
    "http_server"
    "database"
    "self_test"
    "../components/asic/include"
    "../components/connect/include"
    "../components/dns_server/include"
    "../components/stratum/include"

PRIV_REQUIRES
    "app_update"
    "driver"
    "esp_adc"
    "esp_app_format"
    "esp_event"
    "esp_http_server"
    "esp_netif"
    "esp_psram"
    "esp_timer"
    "esp_wifi"
    "json"
    "nvs_flash"
    "spiffs"
    "vfs"
    "esp_driver_i2c"
    "esp_http_client"
    "mbedtls"
    
)

idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_INCLUDE_SIMPLE=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_PATH= ${CMAKE_SOURCE_DIR}/main/lv_conf.h" APPEND)

set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/http_server/acs-os")
set(DATA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/database/data")

if("$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    message(STATUS "Running on GitHub Actions. Web ui will be prebuilt.")

    spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist/acs-os FLASH_IN_PROJECT)
    
    # Create data partition image if data directory exists
    if(EXISTS "${DATA_SRC_DIR}")
        spiffs_create_partition_image(data ${DATA_SRC_DIR} FLASH_IN_PROJECT)
    endif()
else()
    find_program(NPM_EXECUTABLE npm)
    if(NOT NPM_EXECUTABLE AND NOT EXISTS ${WEB_SRC_DIR}/dist)
        message(FATAL_ERROR "npm is not found! Please install it to proceed.")
    endif()

    ExternalProject_Add(
        web_ui_dist
        PREFIX ${CMAKE_BINARY_DIR}/web_ui_dist
        SOURCE_DIR ${WEB_SRC_DIR}
        DOWNLOAD_COMMAND ""
        CONFIGURE_COMMAND
            ${NPM_EXECUTABLE}  i
        USES_TERMINAL_BUILD true
        BUILD_COMMAND
            ${NPM_EXECUTABLE} run build
        INSTALL_COMMAND ""
        BUILD_ALWAYS OFF
        BUILD_IN_SOURCE TRUE
        BUILD_BYPRODUCTS
            "${WEB_SRC_DIR}/dist/acs-os/index.html"
    )

    add_dependencies(${COMPONENT_LIB} web_ui_dist)

    spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist/acs-os FLASH_IN_PROJECT DEPENDS web_ui_dist)
    
    # Create data partition image if data directory exists
    if(EXISTS "${DATA_SRC_DIR}")
        spiffs_create_partition_image(data ${DATA_SRC_DIR} FLASH_IN_PROJECT)
    endif()
endif()
