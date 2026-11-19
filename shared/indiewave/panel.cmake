# The rendered panel a plugin's editor plays back: its plate, filmstrips and
# panel.json from resources/panel (made by shared/indiewave/art), compiled in as
# <Name>PanelData, plus the shared playback code.
#
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/indiewave/panel.cmake)
#   indiewave_add_panel(Tide)      # header TidePanelData.h, namespace TidePanelData
function(indiewave_add_panel target)
    get_filename_component(shared "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    file(GLOB panel_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/panel/*)
    juce_add_binary_data(${target}Panel HEADER_NAME ${target}PanelData.h NAMESPACE ${target}PanelData
                         SOURCES ${panel_files})
    set_target_properties(${target}Panel PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    target_sources(${target} PRIVATE ${shared}/indiewave/ui/Rendered.cpp)
    target_include_directories(${target} PRIVATE ${shared})
    target_link_libraries(${target} PRIVATE ${target}Panel)
endfunction()
