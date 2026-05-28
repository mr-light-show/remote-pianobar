/*
 * Smoke tests for src/libpiano/response.c — JSON parsing and error mapping.
 * These tests exercise PianoResponse with pre-built responseData strings so
 * they do not require a live Pandora connection.
 */

#include <check.h>
#include <string.h>

#include <piano.h>

/* Helper: set up a zeroed PianoHandle + PianoRequest, call PianoResponse,
 * return the result.  Caller must call PianoDestroy(&ph) afterwards. */
static PianoReturn_t callResponse (PianoHandle_t *ph, PianoRequest_t *req,
                                   PianoRequestType_t type,
                                   const char *responseJson) {
	memset (ph,  0, sizeof (*ph));
	memset (req, 0, sizeof (*req));
	req->type         = type;
	req->responseData = (char *) responseJson; /* response is read-only; cast safe */
	return PianoResponse (ph, req);
}

/* A Pandora "fail" response maps code → PIANO_RET_OFFSET + code */
START_TEST (test_response_fail_maps_error_code)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	/* code 1001 → PIANO_RET_P_INVALID_AUTH_TOKEN (PIANO_RET_OFFSET + 1001) */
	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_GENRE_STATIONS,
	        "{\"stat\":\"fail\",\"code\":1001,\"message\":\"AUTH_INVALID_TOKEN\"}");
	ck_assert_int_eq (ret, PIANO_RET_P_INVALID_AUTH_TOKEN);
	PianoDestroy (&ph);
}
END_TEST

/* Missing "stat" field → PIANO_RET_INVALID_RESPONSE */
START_TEST (test_response_missing_stat_field)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_GENRE_STATIONS,
	        "{\"result\":{}}");
	ck_assert_int_eq (ret, PIANO_RET_INVALID_RESPONSE);
	PianoDestroy (&ph);
}
END_TEST

/* Completely invalid JSON → PianoResponse gracefully returns an error */
START_TEST (test_response_invalid_json_does_not_crash)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_GENRE_STATIONS,
	        "not-json");
	ck_assert_int_ne (ret, PIANO_RET_OK);
	PianoDestroy (&ph);
}
END_TEST

/* "fail" response with missing "code" field → PIANO_RET_INVALID_RESPONSE */
START_TEST (test_response_fail_missing_code_field)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_GENRE_STATIONS,
	        "{\"stat\":\"fail\",\"message\":\"no code\"}");
	ck_assert_int_eq (ret, PIANO_RET_INVALID_RESPONSE);
	PianoDestroy (&ph);
}
END_TEST

/* "ok" response for GET_GENRE_STATIONS with an empty result array — must not crash */
START_TEST (test_response_ok_genre_stations_empty_does_not_crash)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_GENRE_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"categories\":[]}}");
	/* May return OK or INVALID_RESPONSE depending on implementation — just no crash */
	(void) ret;
	PianoDestroy (&ph);
}
END_TEST

Suite *libpiano_response_suite (void) {
	Suite *s = suite_create ("libpiano_response");
	TCase *tc = tcase_create ("JSON parsing");
	tcase_add_test (tc, test_response_fail_maps_error_code);
	tcase_add_test (tc, test_response_missing_stat_field);
	tcase_add_test (tc, test_response_invalid_json_does_not_crash);
	tcase_add_test (tc, test_response_fail_missing_code_field);
	tcase_add_test (tc, test_response_ok_genre_stations_empty_does_not_crash);
	suite_add_tcase (s, tc);
	return s;
}
