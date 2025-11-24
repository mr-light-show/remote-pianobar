/*
Copyright (c) 2025

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <check.h>
#include <string.h>
#include "../../src/websocket/http/http_server.h"

/* Test: HTML files should have text/html MIME type */
START_TEST(test_mime_type_html) {
	const char *mime = BarHttpGetMimeType("index.html");
	ck_assert_str_eq(mime, "text/html");
}
END_TEST

/* Test: CSS files should have text/css MIME type */
START_TEST(test_mime_type_css) {
	const char *mime = BarHttpGetMimeType("style.css");
	ck_assert_str_eq(mime, "text/css");
}
END_TEST

/* Test: JavaScript files should have application/javascript MIME type */
START_TEST(test_mime_type_js) {
	const char *mime = BarHttpGetMimeType("app.js");
	ck_assert_str_eq(mime, "application/javascript");
}
END_TEST

/* Test: JSON files should have application/json MIME type */
START_TEST(test_mime_type_json) {
	const char *mime = BarHttpGetMimeType("data.json");
	ck_assert_str_eq(mime, "application/json");
}
END_TEST

/* Test: PNG files should have image/png MIME type */
START_TEST(test_mime_type_png) {
	const char *mime = BarHttpGetMimeType("image.png");
	ck_assert_str_eq(mime, "image/png");
}
END_TEST

/* Test: SVG files should have image/svg+xml MIME type */
START_TEST(test_mime_type_svg) {
	const char *mime = BarHttpGetMimeType("icon.svg");
	ck_assert_str_eq(mime, "image/svg+xml");
}
END_TEST

/* Test: Unknown extension should return default MIME type */
START_TEST(test_mime_type_unknown) {
	const char *mime = BarHttpGetMimeType("file.unknown");
	ck_assert_str_eq(mime, "application/octet-stream");
}
END_TEST

/* Test: NULL path should return default MIME type */
START_TEST(test_mime_type_null) {
	const char *mime = BarHttpGetMimeType(NULL);
	ck_assert_str_eq(mime, "application/octet-stream");
}
END_TEST

/* Test: File without extension should return default MIME type */
START_TEST(test_mime_type_no_extension) {
	const char *mime = BarHttpGetMimeType("README");
	ck_assert_str_eq(mime, "application/octet-stream");
}
END_TEST

/* Create test suite */
Suite *http_server_suite(void) {
	Suite *s;
	TCase *tc_mime;
	
	s = suite_create("HTTP Server");
	tc_mime = tcase_create("MIME Types");
	
	tcase_add_test(tc_mime, test_mime_type_html);
	tcase_add_test(tc_mime, test_mime_type_css);
	tcase_add_test(tc_mime, test_mime_type_js);
	tcase_add_test(tc_mime, test_mime_type_json);
	tcase_add_test(tc_mime, test_mime_type_png);
	tcase_add_test(tc_mime, test_mime_type_svg);
	tcase_add_test(tc_mime, test_mime_type_unknown);
	tcase_add_test(tc_mime, test_mime_type_null);
	tcase_add_test(tc_mime, test_mime_type_no_extension);
	
	suite_add_tcase(s, tc_mime);
	
	return s;
}

