# Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
vespa_add_executable(searchlib_sourceblender_test_app TEST
    SOURCES
    sourceblender_test.cpp
    DEPENDS
    vespa_searchlib
    searchlib_test
)
vespa_add_test(NAME searchlib_sourceblender_test_app COMMAND searchlib_sourceblender_test_app)
vespa_add_executable(searchlib_sourceblender_benchmark_app TEST
    SOURCES
    sourceblender_benchmark.cpp
    DEPENDS
    vespa_searchlib
    searchlib_test
)
# Note: the benchmark is not run as a unit test, so vespa_add_test() is not specified for it.
