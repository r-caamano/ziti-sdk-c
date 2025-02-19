/*
Copyright (c) 2021 NetFoundry, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "catch2/catch.hpp"

#include <buffer.h>
#include <iostream>

TEST_CASE("fixed buffer overflow", "[util]") {
    char b[10];
    auto buf = new_fixed_string_buf(b, sizeof(b));

    REQUIRE(string_buf_fmt(buf, "This will not fit!") == -1);
    REQUIRE(string_buf_size(buf) == 0);
    delete_string_buf(buf);
}

TEST_CASE("buffer appendn", "[util]") {
    auto buf = new_string_buf();

    std::string test_str;

    std::string str("this is a string\n");
    for (int i = 0; i < 10; i++) {
        string_buf_appendn(buf, str.c_str(), str.size());
        test_str += str;
    }

    CHECK(string_buf_size(buf) == test_str.size());

    size_t len;
    char *result = string_buf_to_string(buf, &len);

    CHECK_THAT(result, Catch::Equals(test_str));
    CHECK(len == test_str.size());

    delete_string_buf(buf);
    free(result);
}

TEST_CASE("buffer append", "[util]") {
    string_buf_t json_buf;
    string_buf_init(&json_buf);

    std::string test_str;

    for (int i = 0; i < 10; i++) {
        string_buf_append(&json_buf, "this is a string\n");
        test_str += "this is a string\n";
    }

    size_t len;
    char *result = string_buf_to_string(&json_buf, &len);

    CHECK_THAT(result, Catch::Equals(test_str));
    CHECK(len == test_str.size());

    string_buf_free(&json_buf);
    free(result);
}

TEST_CASE("buffer fmt", "[util]") {
    string_buf_t fmt_buf;
    string_buf_init(&fmt_buf);

    fmt_buf.chunk_size = 160;

    std::string test_str;

    for (int i = 0; i < 1000; i++) {
        string_buf_fmt(&fmt_buf, "%04d\n", i);
        char num[16];
        snprintf(num, 16, "%04d\n", i);
        test_str += num;
    }

    size_t size = string_buf_size(&fmt_buf);
    CHECK(size == test_str.size());
    CHECK(size == 1000 * 5);

    size_t len;
    char *result = string_buf_to_string(&fmt_buf, &len);
    CHECK(len == test_str.size());
    CHECK(string_buf_size(&fmt_buf) == 0);
    CHECK_THAT(result, Catch::Equals(test_str));

    free(result);
    string_buf_free(&fmt_buf);
}



