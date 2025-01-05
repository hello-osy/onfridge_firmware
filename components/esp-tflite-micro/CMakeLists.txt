# Minimum CMake version required
cmake_minimum_required(VERSION 3.16)

# Define the project name
project(esp_tflite_micro)

# Register the component
idf_component_register(SRCS "src/main.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer nvs_flash)
