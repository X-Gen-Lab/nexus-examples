# Application-private single-LUN profile over the selected official ST class.
# Author: Nexus Team
nexus_add_st_usb_device()
set(msc_supplier "${NEXUS_SOURCE}/vendors/st/usb_device")
add_library(
    example_st_msc_vendor
    STATIC
    EXCLUDE_FROM_ALL
    "${msc_supplier}/Class/MSC/Src/usbd_msc.c"
    "${msc_supplier}/Class/MSC/Src/usbd_msc_bot.c"
    "${msc_supplier}/Class/MSC/Src/usbd_msc_scsi.c"
    "${msc_supplier}/Class/MSC/Src/usbd_msc_data.c"
)
target_include_directories(
    example_st_msc_vendor
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/port"
)
target_include_directories(
    example_st_msc_vendor
    SYSTEM
    PUBLIC "${msc_supplier}/Core/Inc" "${msc_supplier}/Class/MSC/Inc"
)
target_include_directories(
    example_st_msc_vendor
    INTERFACE "${CMAKE_CURRENT_LIST_DIR}/port"
)
target_link_libraries(
    example_st_msc_vendor
    PUBLIC Nexus::ABI
    PRIVATE Nexus::STUSBDevice nexus_build_options
)
nexus_apply_target_policy(example_st_msc_vendor)
add_library(example_st_msc_reference STATIC EXCLUDE_FROM_ALL st_msc_reference.c)
target_include_directories(
    example_st_msc_reference
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(
    example_st_msc_reference
    PUBLIC Nexus::IOBlock example_st_msc_vendor
    PRIVATE nexus_owned_warnings nexus_build_options
)

nexus_apply_target_policy(example_st_msc_reference)
