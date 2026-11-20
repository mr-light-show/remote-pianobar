/*
 * Smoke tests for src/libpiano/response.c — JSON parsing and error mapping.
 * These tests exercise PianoResponse with pre-built responseData strings so
 * they do not require a live Pandora connection.
 */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <piano.h>

#include "crypt.h"
#include "piano_private.h"

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

/* "stat":null is not a string: must be rejected, not passed to strcmp */
START_TEST (test_response_null_stat_is_invalid)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_STATIONS,
	        "{\"stat\":null,\"result\":{}}");
	ck_assert_int_eq (ret, PIANO_RET_INVALID_RESPONSE);
	PianoDestroy (&ph);
}
END_TEST

/* A station whose name is JSON null must be skipped, not strdup(NULL)'d */
START_TEST (test_response_station_null_name_skipped)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"stations\":["
	        "{\"stationName\":null,\"stationToken\":\"bad\"},"
	        "{\"stationName\":\"Good\",\"stationToken\":\"good\"}]}}");
	ck_assert_int_eq (ret, PIANO_RET_OK);
	ck_assert_ptr_nonnull (ph.stations);
	ck_assert_str_eq (ph.stations->name, "Good");
	ck_assert_str_eq (ph.stations->id, "good");
	ck_assert_ptr_null (ph.stations->head.next);
	PianoDestroy (&ph);
}
END_TEST

/* A quickmix station without stationToken must not reach strcmp(.., NULL) */
START_TEST (test_response_quickmix_missing_station_token)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"stations\":["
	        "{\"stationName\":\"QuickMix\",\"isQuickMix\":true,"
	        "\"quickMixStationIds\":[\"a\"]},"
	        "{\"stationName\":\"A\",\"stationToken\":\"a\"}]}}");
	ck_assert_int_eq (ret, PIANO_RET_OK);
	PianoDestroy (&ph);
}
END_TEST

/* null entries in quickMixStationIds are ignored; real ids still match */
START_TEST (test_response_quickmix_null_id_entry)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"stations\":["
	        "{\"stationName\":\"QuickMix\",\"stationToken\":\"q\","
	        "\"isQuickMix\":true,\"quickMixStationIds\":[null,\"a\"]},"
	        "{\"stationName\":\"A\",\"stationToken\":\"a\"},"
	        "{\"stationName\":\"B\",\"stationToken\":\"b\"}]}}");
	ck_assert_int_eq (ret, PIANO_RET_OK);
	PianoStation_t *q = ph.stations;
	ck_assert_ptr_nonnull (q);
	PianoStation_t *a = (PianoStation_t *) q->head.next;
	ck_assert_ptr_nonnull (a);
	PianoStation_t *b = (PianoStation_t *) a->head.next;
	ck_assert_ptr_nonnull (b);
	ck_assert (!q->useQuickMix);
	ck_assert (a->useQuickMix);
	ck_assert (!b->useQuickMix);
	PianoDestroy (&ph);
}
END_TEST

/* stations that is not an array must be ignored */
START_TEST (test_response_stations_not_array)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"stations\":\"oops\"}}");
	ck_assert_int_eq (ret, PIANO_RET_OK);
	ck_assert_ptr_null (ph.stations);
	PianoDestroy (&ph);
}
END_TEST

/* focusTraitName:null must be skipped instead of reaching PianoStrpcat */
START_TEST (test_response_explain_null_focus_trait)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataExplain_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	req.type = PIANO_REQUEST_EXPLAIN;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"explanations\":["
	        "{\"focusTraitName\":null},{\"focusTraitName\":\"jazz\"}]}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_ptr_nonnull (data.retExplain);
	ck_assert_ptr_nonnull (strstr (data.retExplain, "jazz"));
	free (data.retExplain);
	PianoDestroy (&ph);
}
END_TEST

static void initCryptoHandle (PianoHandle_t *ph, PianoRequest_t *req,
		PianoRequestDataLogin_t *login, int step) {
	ck_assert_int_eq (PianoInit (ph, "u", "p", "d", "inkeyinkey", "inkeyinkey"),
			PIANO_RET_OK);
	memset (req, 0, sizeof (*req));
	memset (login, 0, sizeof (*login));
	login->step = step;
	req->type = PIANO_REQUEST_LOGIN;
	req->data = login;
}

/* Missing partnerAuthToken in the partnerLogin response is a bad response,
 * not a login that proceeds with a NULL token. */
START_TEST (test_response_login_step0_missing_auth_token)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;
	char json[256];

	initCryptoHandle (&ph, &req, &login, 0);
	char *enc = PianoEncryptString (ph.partner.out, "xxxx1234567890");
	ck_assert_ptr_nonnull (enc);
	snprintf (json, sizeof (json), "{\"stat\":\"ok\",\"result\":{"
	        "\"syncTime\":\"%s\",\"partnerId\":\"42\"}}", enc);
	free (enc);
	req.responseData = json;
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_INVALID_RESPONSE);
	ck_assert_ptr_null (ph.partner.authToken);
	PianoDestroy (&ph);
}
END_TEST

/* syncTime:null must not be passed to the decryptor */
START_TEST (test_response_login_step0_null_sync_time)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;

	initCryptoHandle (&ph, &req, &login, 0);
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"syncTime\":null,"
	        "\"partnerAuthToken\":\"t\",\"partnerId\":\"42\"}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_INVALID_RESPONSE);
	PianoDestroy (&ph);
}
END_TEST

/* A well-formed partnerLogin response still continues the login */
START_TEST (test_response_login_step0_ok)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;
	char json[256];

	initCryptoHandle (&ph, &req, &login, 0);
	char *enc = PianoEncryptString (ph.partner.out, "xxxx1234567890");
	ck_assert_ptr_nonnull (enc);
	snprintf (json, sizeof (json), "{\"stat\":\"ok\",\"result\":{"
	        "\"syncTime\":\"%s\",\"partnerAuthToken\":\"tok\","
	        "\"partnerId\":\"42\"}}", enc);
	free (enc);
	req.responseData = json;
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_CONTINUE_REQUEST);
	ck_assert_str_eq (ph.partner.authToken, "tok");
	ck_assert_int_eq (login.step, 1);
	PianoDestroy (&ph);
}
END_TEST

/* userLogin response missing userAuthToken must fail, leaving no half-set
 * user info behind. */
START_TEST (test_response_login_step1_missing_token)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;

	initCryptoHandle (&ph, &req, &login, 1);
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"userId\":\"7\","
	        "\"userAuthToken\":null}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_INVALID_RESPONSE);
	ck_assert_ptr_null (ph.user.listenerId);
	ck_assert_ptr_null (ph.user.authToken);
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_response_login_step1_ok)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;

	initCryptoHandle (&ph, &req, &login, 1);
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"userId\":\"7\","
	        "\"userAuthToken\":\"tok\"}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_str_eq (ph.user.listenerId, "7");
	ck_assert_str_eq (ph.user.authToken, "tok");
	PianoDestroy (&ph);
}
END_TEST

/* PianoRequest must refuse to escape a NULL partner/user auth token */
START_TEST (test_request_login_step1_null_partner_token)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;

	initCryptoHandle (&ph, &req, &login, 1);
	login.user = "u";
	login.password = "p";
	ph.partner.authToken = NULL;
	ck_assert_int_eq (PianoRequest (&ph, &req, PIANO_REQUEST_LOGIN),
	        PIANO_RET_INVALID_RESPONSE);
	free (req.postData);
	PianoDestroy (&ph);
}
END_TEST

/* With a partner token, the userLogin request is built (token URL-encoded) */
START_TEST (test_request_login_step1_with_partner_token)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;

	initCryptoHandle (&ph, &req, &login, 1);
	login.user = "u";
	login.password = "p";
	ph.partner.authToken = strdup ("tok/+=");
	ck_assert_int_eq (PianoRequest (&ph, &req, PIANO_REQUEST_LOGIN), PIANO_RET_OK);
	ck_assert_ptr_nonnull (strstr (req.urlPath, "auth_token=tok%2F%2B%3D"));
	free (req.postData);
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_request_standard_null_user_token)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	ck_assert_int_eq (PianoInit (&ph, "u", "p", "d", "inkeyinkey", "inkeyinkey"),
	        PIANO_RET_OK);
	memset (&req, 0, sizeof (req));
	ph.user.listenerId = strdup ("7");
	ph.user.authToken = NULL;
	ck_assert_int_eq (PianoRequest (&ph, &req, PIANO_REQUEST_GET_STATIONS),
	        PIANO_RET_INVALID_RESPONSE);
	free (req.postData);

	/* ... and a missing user id */
	free (ph.user.listenerId);
	ph.user.listenerId = NULL;
	ph.user.authToken = strdup ("tok");
	ck_assert_int_eq (PianoRequest (&ph, &req, PIANO_REQUEST_GET_GENRE_STATIONS),
	        PIANO_RET_INVALID_RESPONSE);
	free (req.postData);
	PianoDestroy (&ph);
}
END_TEST

/* Non-array containers in the other list parsers are ignored, not walked */
START_TEST (test_response_lists_not_arrays)
{
	static const struct { PianoRequestType_t type; const char *json; } cases[] = {
		{PIANO_REQUEST_GET_GENRE_STATIONS,
		 "{\"stat\":\"ok\",\"result\":{\"categories\":7}}"},
		{PIANO_REQUEST_GET_GENRE_STATIONS,
		 "{\"stat\":\"ok\",\"result\":{\"categories\":[{\"categoryName\":\"c\","
		 "\"stations\":{}}]}}"},
		{PIANO_REQUEST_EXPLAIN,
		 "{\"stat\":\"ok\",\"result\":{\"explanations\":\"x\"}}"},
		{PIANO_REQUEST_SEARCH,
		 "{\"stat\":\"ok\",\"result\":{\"artists\":1,\"songs\":2}}"},
		{PIANO_REQUEST_GET_STATION_MODES,
		 "{\"stat\":\"ok\",\"result\":{\"availableModes\":true}}"},
	};
	for (size_t i = 0; i < sizeof (cases)/sizeof (*cases); i++) {
		PianoHandle_t ph;
		PianoRequest_t req;
		union {
			PianoRequestDataExplain_t explain;
			PianoRequestDataSearch_t search;
			PianoRequestDataGetStationModes_t modes;
		} data;
		memset (&data, 0, sizeof (data));
		memset (&ph, 0, sizeof (ph));
		memset (&req, 0, sizeof (req));
		req.type = cases[i].type;
		req.data = &data;
		req.responseData = (char *) cases[i].json;
		(void) PianoResponse (&ph, &req);
		PianoDestroy (&ph);
	}
}
END_TEST

/* createStation answer without name/token must not be added or escape-matched */
START_TEST (test_response_create_station_null_token)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_CREATE_STATION,
	        "{\"stat\":\"ok\",\"result\":{\"stationName\":\"X\","
	        "\"stationToken\":null}}");
	ck_assert_int_eq (ret, PIANO_RET_INVALID_RESPONSE);
	ck_assert_ptr_null (ph.stations);
	PianoDestroy (&ph);
}
END_TEST

/* An empty station list (or a NULL id) finds nothing; it must not assert:
 * createStation searches the list before appending the first station. */
START_TEST (test_find_station_by_id_empty_list_or_null_id)
{
	ck_assert_ptr_null (PianoFindStationById (NULL, "tok"));
	ck_assert_ptr_null (PianoFindStationById (NULL, NULL));
}
END_TEST

START_TEST (test_response_create_station_null_name)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_CREATE_STATION,
	        "{\"stat\":\"ok\",\"result\":{\"stationName\":null,"
	        "\"stationToken\":\"tok\"}}");
	ck_assert_int_eq (ret, PIANO_RET_INVALID_RESPONSE);
	ck_assert_ptr_null (ph.stations);
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_response_create_station_ok)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_CREATE_STATION,
	        "{\"stat\":\"ok\",\"result\":{\"stationName\":\"X\","
	        "\"stationToken\":\"tok\"}}");
	ck_assert_int_eq (ret, PIANO_RET_OK);
	ck_assert_str_eq (ph.stations->id, "tok");
	PianoDestroy (&ph);
}
END_TEST

/* "encoding":null in the audio map must not reach strcmp */
/* An encoding no format matches leaves the song's format unknown */
START_TEST (test_response_playlist_unknown_encoding)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataGetPlaylist_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	data.quality = PIANO_AQ_HIGH;
	req.type = PIANO_REQUEST_GET_PLAYLIST;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"items\":[{"
	        "\"artistName\":\"A\",\"songName\":\"S\","
	        "\"audioUrlMap\":{\"highQuality\":{\"encoding\":\"nope\","
	        "\"audioUrl\":\"http://x/y\"}}}]}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_ptr_nonnull (data.retPlaylist);
	ck_assert_int_eq (data.retPlaylist->audioFormat, PIANO_AF_UNKNOWN);
	PianoDestroyPlaylist (data.retPlaylist);
	PianoDestroy (&ph);
}
END_TEST

/* Helper: run GET_PLAYLIST on a response body; caller destroys the playlist */
static PianoSong_t *parsePlaylist (const char *json) {
	static PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataGetPlaylist_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	data.quality = PIANO_AQ_HIGH;
	req.type = PIANO_REQUEST_GET_PLAYLIST;
	req.data = &data;
	req.responseData = (char *) json;
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	return data.retPlaylist;
}

/* Items without a usable songName or artistName are skipped */
START_TEST (test_response_playlist_skips_missing_title_or_artist)
{
	PianoSong_t *pl = parsePlaylist ("{\"stat\":\"ok\",\"result\":{\"items\":["
	        "{\"artistName\":\"A\",\"songName\":null},"
	        "{\"artistName\":\"A\"},"
	        "{\"artistName\":null,\"songName\":\"S\"},"
	        "{\"artistName\":{\"x\":1},\"songName\":\"S\"},"
	        "null]}}");
	ck_assert_ptr_null (pl);
}
END_TEST

/* A good item next to bad ones survives; a missing album becomes "" */
START_TEST (test_response_playlist_keeps_good_item_beside_bad)
{
	PianoSong_t *pl = parsePlaylist ("{\"stat\":\"ok\",\"result\":{\"items\":["
	        "{\"artistName\":\"A\",\"songName\":null},"
	        "{\"artistName\":\"B\",\"songName\":\"T\"},"
	        "{\"songName\":\"U\"}]}}");
	ck_assert_ptr_nonnull (pl);
	ck_assert_ptr_null (pl->head.next);
	ck_assert_str_eq (pl->title, "T");
	ck_assert_str_eq (pl->artist, "B");
	ck_assert_ptr_nonnull (pl->album);
	ck_assert_str_eq (pl->album, "");
	PianoDestroyPlaylist (pl);
}
END_TEST

/* A destroyed category frees its name and nested genres */
/* calloc that succeeds for the first g_callocOk calls, then fails */
static int g_callocOk;
static void *failing_calloc (size_t nmemb, size_t size) {
	if (g_callocOk-- > 0) {
		return calloc (nmemb, size);
	}
	return NULL;
}

/* Out of memory while building the second playlist item frees the first */
START_TEST (test_response_playlist_oom_frees_partial_list)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataGetPlaylist_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	data.quality = PIANO_AQ_HIGH;
	req.type = PIANO_REQUEST_GET_PLAYLIST;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"items\":["
	        "{\"artistName\":\"A\",\"songName\":\"S1\"},"
	        "{\"artistName\":\"B\",\"songName\":\"S2\"}]}}";
	g_callocOk = 1;
	PianoResponseSetCallocTestHook (failing_calloc);
	PianoReturn_t ret = PianoResponse (&ph, &req);
	PianoResponseSetCallocTestHook (NULL);
	ck_assert_int_eq (ret, PIANO_RET_OUT_OF_MEMORY);
	ck_assert_ptr_null (data.retPlaylist);
	PianoDestroy (&ph);
}
END_TEST

/* Out of memory on a genre (or its category) frees what was built so far */
START_TEST (test_response_genre_oom_frees_partial_category)
{
	static const char *const json = "{\"stat\":\"ok\",\"result\":{\"categories\":[{"
	        "\"categoryName\":\"Rock\",\"stations\":[{"
	        "\"stationName\":\"Classic\",\"stationToken\":\"g1\"}]}]}}";
	/* _i = successful callocs before the failure: 0 = the category itself,
	 * 1 = the first genre of that category */
	PianoHandle_t ph;
	PianoRequest_t req;

	g_callocOk = _i;
	PianoResponseSetCallocTestHook (failing_calloc);
	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_GENRE_STATIONS, json);
	PianoResponseSetCallocTestHook (NULL);
	ck_assert_int_eq (ret, PIANO_RET_OUT_OF_MEMORY);
	ck_assert_ptr_null (ph.genreStations);
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_destroy_genre_category_frees_nested)
{
	PianoGenreCategory_t *cat = calloc (1, sizeof (*cat));
	ck_assert_ptr_nonnull (cat);
	cat->name = strdup ("Rock");
	for (int i = 0; i < 2; i++) {
		PianoGenre_t *g = calloc (1, sizeof (*g));
		ck_assert_ptr_nonnull (g);
		g->name = strdup ("n");
		g->musicId = strdup ("id");
		cat->genres = PianoListAppendP (cat->genres, g);
	}
	PianoDestroyGenreCategory (cat);
	PianoDestroyGenreCategory (NULL);
}
END_TEST

/* A genre category's stations become its genres */
START_TEST (test_response_genre_stations_parsed)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_GENRE_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"categories\":[{"
	        "\"categoryName\":\"Rock\",\"stations\":[{"
	        "\"stationName\":\"Classic\",\"stationToken\":\"g1\"}]}]}}");
	ck_assert_int_eq (ret, PIANO_RET_OK);
	ck_assert_ptr_nonnull (ph.genreStations);
	ck_assert_str_eq (ph.genreStations->name, "Rock");
	ck_assert_ptr_nonnull (ph.genreStations->genres);
	ck_assert_str_eq (ph.genreStations->genres->name, "Classic");
	ck_assert_str_eq (ph.genreStations->genres->musicId, "g1");
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_response_playlist_null_encoding)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataGetPlaylist_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	data.quality = PIANO_AQ_HIGH;
	req.type = PIANO_REQUEST_GET_PLAYLIST;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"items\":[{"
	        "\"artistName\":\"A\",\"songName\":\"S\","
	        "\"audioUrlMap\":{\"highQuality\":{\"encoding\":null,"
	        "\"audioUrl\":\"http://x/y\"}}}]}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_ptr_nonnull (data.retPlaylist);
	ck_assert_int_eq (data.retPlaylist->audioFormat, PIANO_AF_UNKNOWN);
	PianoDestroyPlaylist (data.retPlaylist);
	PianoDestroy (&ph);
}
END_TEST

/* Container values (object/array) are not usable strings: station skipped */
START_TEST (test_response_station_container_name_skipped)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	PianoReturn_t ret = callResponse (&ph, &req, PIANO_REQUEST_GET_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"stations\":["
	        "{\"stationName\":{},\"stationToken\":\"a\"},"
	        "{\"stationName\":[],\"stationToken\":\"b\"},"
	        "{\"stationName\":42,\"stationToken\":\"c\"}]}}");
	ck_assert_int_eq (ret, PIANO_RET_OK);
	/* numbers are still stringified, containers are rejected */
	ck_assert_ptr_nonnull (ph.stations);
	ck_assert_str_eq (ph.stations->name, "42");
	ck_assert_ptr_null (ph.stations->head.next);
	PianoDestroy (&ph);
}
END_TEST

/* "stations":null and null entries are treated as empty / skipped */
START_TEST (test_response_stations_null_and_null_entry)
{
	PianoHandle_t ph;
	PianoRequest_t req;

	ck_assert_int_eq (callResponse (&ph, &req, PIANO_REQUEST_GET_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"stations\":null}}"), PIANO_RET_OK);
	ck_assert_ptr_null (ph.stations);
	PianoDestroy (&ph);

	ck_assert_int_eq (callResponse (&ph, &req, PIANO_REQUEST_GET_STATIONS,
	        "{\"stat\":\"ok\",\"result\":{\"stations\":[null,"
	        "{\"stationName\":\"n\",\"stationToken\":\"t\"}]}}"), PIANO_RET_OK);
	ck_assert_ptr_nonnull (ph.stations);
	ck_assert_str_eq (ph.stations->id, "t");
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_response_login_step0_missing_partner_id)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;

	initCryptoHandle (&ph, &req, &login, 0);
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{"
	        "\"syncTime\":\"00\",\"partnerAuthToken\":\"t\"}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_INVALID_RESPONSE);
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_response_login_step1_null_user_id)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataLogin_t login;

	initCryptoHandle (&ph, &req, &login, 1);
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"userId\":null,"
	        "\"userAuthToken\":\"tok\"}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_INVALID_RESPONSE);
	ck_assert_ptr_null (ph.user.authToken);
	PianoDestroy (&ph);
}
END_TEST

/* Three explanations exercise every separator branch */
START_TEST (test_response_explain_three_traits)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataExplain_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	req.type = PIANO_REQUEST_EXPLAIN;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"explanations\":["
	        "{\"focusTraitName\":\"a\"},{\"focusTraitName\":\"b\"},"
	        "{\"focusTraitName\":\"c\"}]}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_ptr_nonnull (strstr (data.retExplain, "a, b and c."));
	free (data.retExplain);
	PianoDestroy (&ph);
}
END_TEST

/* A normal playlist item with a known encoding, rating and gain */
START_TEST (test_response_playlist_mp3_item)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataGetPlaylist_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	data.quality = PIANO_AQ_HIGH;
	req.type = PIANO_REQUEST_GET_PLAYLIST;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"items\":["
	        "{\"songName\":\"no artist\"},"
	        "{\"artistName\":\"A\",\"songName\":\"S\",\"songRating\":1,"
	        "\"trackGain\":\"-3.5\",\"trackLength\":120,"
	        "\"audioUrlMap\":{\"highQuality\":{\"encoding\":\"mp3\","
	        "\"audioUrl\":\"http://x/y\"}}}]}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_ptr_nonnull (data.retPlaylist);
	ck_assert_int_eq (data.retPlaylist->audioFormat, PIANO_AF_MP3);
	ck_assert_str_eq (data.retPlaylist->audioUrl, "http://x/y");
	ck_assert_int_eq (data.retPlaylist->rating, PIANO_RATE_LOVE);
	ck_assert_int_eq (data.retPlaylist->length, 120);
	PianoDestroyPlaylist (data.retPlaylist);
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_response_search_results)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataSearch_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	req.type = PIANO_REQUEST_SEARCH;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{"
	        "\"artists\":[{\"artistName\":\"A\",\"musicToken\":\"m1\"},"
	        "{\"artistName\":null,\"musicToken\":\"m2\"}],"
	        "\"songs\":[{\"songName\":\"S\",\"artistName\":\"A\","
	        "\"musicToken\":\"m3\"}]}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_ptr_nonnull (data.searchResult.artists);
	ck_assert_str_eq (data.searchResult.artists->name, "A");
	ck_assert_ptr_null (((PianoArtist_t *) data.searchResult.artists->head.next)->name);
	ck_assert_ptr_nonnull (data.searchResult.songs);
	PianoDestroySearchResult (&data.searchResult);
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_response_station_info_seeds_and_feedback)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataGetStationInfo_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	req.type = PIANO_REQUEST_GET_STATION_INFO;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{"
	        "\"music\":{\"songs\":[{\"songName\":\"S\",\"artistName\":\"A\","
	        "\"seedId\":\"s1\"}],\"artists\":[{\"artistName\":\"B\","
	        "\"seedId\":\"s2\"}]},"
	        "\"feedback\":{\"thumbsUp\":[{\"songName\":\"U\",\"artistName\":\"A\","
	        "\"feedbackId\":\"f1\",\"isPositive\":true,\"trackLength\":5}],"
	        "\"thumbsDown\":\"bogus\"}}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_ptr_nonnull (data.info.songSeeds);
	ck_assert_ptr_nonnull (data.info.artistSeeds);
	ck_assert_ptr_nonnull (data.info.feedback);
	ck_assert_int_eq (data.info.feedback->rating, PIANO_RATE_LOVE);
	PianoDestroyStationInfo (&data.info);
	PianoDestroy (&ph);
}
END_TEST

START_TEST (test_response_station_modes)
{
	PianoHandle_t ph;
	PianoRequest_t req;
	PianoRequestDataGetStationModes_t data;

	memset (&data, 0, sizeof (data));
	memset (&ph, 0, sizeof (ph));
	memset (&req, 0, sizeof (req));
	req.type = PIANO_REQUEST_GET_STATION_MODES;
	req.data = &data;
	req.responseData = (char *) "{\"stat\":\"ok\",\"result\":{\"currentModeId\":1,"
	        "\"availableModes\":[7,{\"modeId\":1,\"modeName\":\"Mode\","
	        "\"modeDescription\":null}]}}";
	ck_assert_int_eq (PianoResponse (&ph, &req), PIANO_RET_OK);
	ck_assert_ptr_nonnull (data.retModes);
	ck_assert_str_eq (data.retModes->name, "Mode");
	ck_assert_ptr_null (data.retModes->description);
	ck_assert (data.retModes->active);
	ck_assert_ptr_null (data.retModes->head.next);
	PianoDestroyStationMode (data.retModes);
	PianoDestroy (&ph);
}
END_TEST

Suite *libpiano_response_suite (void) {
	Suite *s = suite_create ("libpiano_response");
	TCase *tc = tcase_create ("JSON parsing");
	tcase_add_test (tc, test_response_fail_maps_error_code);
	tcase_add_test (tc, test_response_playlist_skips_missing_title_or_artist);
	tcase_add_test (tc, test_response_playlist_keeps_good_item_beside_bad);
	tcase_add_test (tc, test_destroy_genre_category_frees_nested);
	tcase_add_test (tc, test_response_playlist_oom_frees_partial_list);
	tcase_add_loop_test (tc, test_response_genre_oom_frees_partial_category, 0, 2);
	tcase_add_test (tc, test_response_missing_stat_field);
	tcase_add_test (tc, test_response_invalid_json_does_not_crash);
	tcase_add_test (tc, test_response_fail_missing_code_field);
	tcase_add_test (tc, test_response_ok_genre_stations_empty_does_not_crash);
	tcase_add_test (tc, test_response_null_stat_is_invalid);
	tcase_add_test (tc, test_response_station_null_name_skipped);
	tcase_add_test (tc, test_response_quickmix_missing_station_token);
	tcase_add_test (tc, test_response_quickmix_null_id_entry);
	tcase_add_test (tc, test_response_stations_not_array);
	tcase_add_test (tc, test_response_explain_null_focus_trait);
	tcase_add_test (tc, test_response_login_step0_missing_auth_token);
	tcase_add_test (tc, test_response_login_step0_null_sync_time);
	tcase_add_test (tc, test_response_login_step0_ok);
	tcase_add_test (tc, test_response_login_step1_missing_token);
	tcase_add_test (tc, test_response_login_step1_ok);
	tcase_add_test (tc, test_request_login_step1_null_partner_token);
	tcase_add_test (tc, test_request_standard_null_user_token);
	tcase_add_test (tc, test_request_login_step1_with_partner_token);
	tcase_add_test (tc, test_response_create_station_null_name);
	tcase_add_test (tc, test_response_playlist_unknown_encoding);
	tcase_add_test (tc, test_response_genre_stations_parsed);
	tcase_add_test (tc, test_response_lists_not_arrays);
	tcase_add_test (tc, test_response_station_container_name_skipped);
	tcase_add_test (tc, test_response_stations_null_and_null_entry);
	tcase_add_test (tc, test_response_login_step0_missing_partner_id);
	tcase_add_test (tc, test_response_login_step1_null_user_id);
	tcase_add_test (tc, test_response_explain_three_traits);
	tcase_add_test (tc, test_response_playlist_mp3_item);
	tcase_add_test (tc, test_response_search_results);
	tcase_add_test (tc, test_response_station_info_seeds_and_feedback);
	tcase_add_test (tc, test_response_station_modes);
	tcase_add_test (tc, test_response_create_station_null_token);
	tcase_add_test (tc, test_find_station_by_id_empty_list_or_null_id);
	tcase_add_test (tc, test_response_create_station_ok);
	tcase_add_test (tc, test_response_playlist_null_encoding);
	suite_add_tcase (s, tc);
	return s;
}
