# One target definition is shared by the existing source and installed SDK
# entry points. The application owns main; no second source graph is authored.
nexus_add_firmware(industrial_control SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/main.c"
    "${CMAKE_CURRENT_LIST_DIR}/industrial_application.c"
    "${CMAKE_CURRENT_LIST_DIR}/industrial_spi.c"
    "${CMAKE_CURRENT_LIST_DIR}/native/model.c"
    LIBRARIES Nexus::SpiOwner Nexus::Settings Nexus::ModbusRTU
        Nexus::BMP280 Nexus::FileFlash Nexus::OSNative Nexus::Arch)

# Native controls belong only to this explicit software reference.
target_link_libraries(industrial_control PRIVATE Nexus::NativeModel)
