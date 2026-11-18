# Headless renderer: renders a project to an audio file without the GUI.
# Stage S0.1 only reports its version; rendering arrives in stage S7.
add_executable(af-render main.cpp)
target_link_libraries(af-render PRIVATE AudioForge::app)
af_set_warnings(af-render)
set_target_properties(af-render PROPERTIES FOLDER "Apps")
