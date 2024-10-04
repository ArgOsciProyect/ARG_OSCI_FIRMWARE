idf_component_register(SRCS
                    INCLUDE_DIRS ".")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS components/arduino)