# Note we are using INTERFACE so that the library can be configured per-app
# with compile-time defines

add_library(libdvi INTERFACE)

target_sources(libdvi INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/dvi.c
	${CMAKE_CURRENT_LIST_DIR}/dvi.h
	${CMAKE_CURRENT_LIST_DIR}/dvi_config_defs.h
	${CMAKE_CURRENT_LIST_DIR}/dvi_serialiser.c
	${CMAKE_CURRENT_LIST_DIR}/dvi_serialiser.h
	${CMAKE_CURRENT_LIST_DIR}/dvi_timing.c
	${CMAKE_CURRENT_LIST_DIR}/dvi_timing.h
	${CMAKE_CURRENT_LIST_DIR}/tmds_encode.S
	${CMAKE_CURRENT_LIST_DIR}/tmds_encode.c
	${CMAKE_CURRENT_LIST_DIR}/tmds_encode.h
	${CMAKE_CURRENT_LIST_DIR}/tmds_table.h
	${CMAKE_CURRENT_LIST_DIR}/tmds_table_fullres.h
	${CMAKE_CURRENT_LIST_DIR}/util_queue_u32_inline.h
    ${CMAKE_CURRENT_LIST_DIR}/data_packet.c
    ${CMAKE_CURRENT_LIST_DIR}/data_packet.h
    ${CMAKE_CURRENT_LIST_DIR}/audio_ring.c
    ${CMAKE_CURRENT_LIST_DIR}/audio_ring.h
    ${CMAKE_CURRENT_LIST_DIR}/hdmia.c
)
# Enable for Olimex board with stereo sound
#    target_compile_definitions(libdvi INTERFACE
#                               -DDVI_DEFAULT_SERIAL_CONFIG=Olimex_RP2040_PICO_PC_cfg
#                               -DDVI_USE_PIO_CLOCK)

target_include_directories(libdvi INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(libdvi INTERFACE
	pico_base_headers
	pico_util
	hardware_dma
	hardware_interp
	hardware_pio
	hardware_pwm
)

pico_generate_pio_header(libdvi ${CMAKE_CURRENT_LIST_DIR}/dvi_serialiser.pio)
pico_generate_pio_header(libdvi ${CMAKE_CURRENT_LIST_DIR}/tmds_encode_1bpp.pio)
pico_generate_pio_header(libdvi ${CMAKE_CURRENT_LIST_DIR}/dvi_clock.pio)
