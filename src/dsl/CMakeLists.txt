add_library(lumen-dsl STATIC
    dsl.cpp
    design_document.cpp
    design_editor.cpp
    design_preview.cpp
    design_resources.cpp
    design_mapping.cpp
    design_schema.cpp
    design_preview_frame.cpp
    document_store.cpp
    text_dsl.cpp
)

target_include_directories(lumen-dsl PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-dsl PUBLIC lumen-core)
target_compile_features(lumen-dsl PUBLIC cxx_std_20)
