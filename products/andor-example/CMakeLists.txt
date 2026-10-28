# ANDOR Example: an ANDOR instrument built from the ANDOR Instrument Shell (shared/andor-shell).
# Its identifiers are its entry in andor-design-system/product-registry.json; never change them
# once it has shipped (hosts recall sessions and automation by them).
include(${PROJECT_SOURCE_DIR}/shared/andor-shell/AndorInstrument.cmake)

andor_add_instrument(ANDOR_Example
    PRODUCT_NAME "ANDOR Example"
    SLUG example
    PLUGIN_CODE Aexm
    BUNDLE_ID com.osp.andorexample
    VERSION 0.1.0
    SOURCES Source/Product.cpp)

andor_add_instrument_tests(ANDOR_Example andor_example_tests)
