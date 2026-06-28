# Account Config Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reject invalid file-backed accounts at config load time — missing snippet files and duplicate usernames — so phantom accounts are never created in `settings->accounts`.

**Architecture:** Refactor `BarSettingsBuildAccounts` in `src/settings.c` to add file-backed accounts one at a time instead of pre-allocating `accountLineCount + 1` slots. Each candidate account is loaded into a temporary `BarAccount_t`; on missing file or duplicate username the entry is destroyed, an error is logged, and the account is skipped. Existing happy-path tests remain unchanged. TDD: add failing tests first, then implement.

**Tech Stack:** C, Check unit tests (`test/unit/test_settings.c`), `make test` / `make test-all`.

**Note on requirement B:** The spec text duplicated the “file not found” message for duplicate accounts. This plan uses a distinct duplicate-username error (see Task 4). Duplicates are detected after inheritance + snippet merge, comparing **username** only (password / `password_command` differences do not make a second account distinct).

---

## File map

| File | Change |
|------|--------|
| `src/settings.c` | `BarSettingsReadAccountFile` returns `bool`; add `BarAccountUsernamesEqual`; refactor `BarSettingsBuildAccounts` to skip invalid accounts |
| `test/unit/test_settings.c` | Six new tests + suite registration |
| `CONFIG.md` | Document rejection rules under “Multiple Pandora accounts” |

No locale change: config-load errors use `log_write(LOG_ERROR, …)` like existing `Warning: Cannot open account file` (developer/operator surface, not web UI).

---

## Duplicate detection rules

Two accounts are **duplicates** when both have non-NULL `username` and `strcmp(username)` is 0 after inheritance + snippet merge.

Password and `password_command` are **not** compared. Same username with different passwords (or one account using `password` and another `password_command`) is still a duplicate and the file-backed account is rejected.

Comparison uses the merged `username` on each `BarAccount_t` (after inheritance + snippet overrides).

---

## Task 1: Failing test — missing account file (requirement A)

**Files:**
- Modify: `test/unit/test_settings.c`
- Test: `test/unit/test_settings.c`

- [ ] **Step 1: Add helper to assert account id is absent**

Add near the top of `test/unit/test_settings.c` (after `read_settings_in_dir`):

```c
static bool settings_has_account_id (const BarSettings_t *s, const char *id) {
	if (s->accounts == NULL || id == NULL) {
		return false;
	}
	for (size_t i = 0; i < s->accountCount; i++) {
		if (s->accounts[i].id != NULL && strcmp (s->accounts[i].id, id) == 0) {
			return true;
		}
	}
	return false;
}
```

- [ ] **Step 2: Write failing tests**

Add these tests before `Suite *settings_suite`:

```c
START_TEST (test_settings_rejects_missing_account_file_with_primary) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = mainuser\n"
			"password = mainpass\n"
			"main_account_id = home\n"
			"account = work:does-not-exist.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 1);
	ck_assert (settings_has_account_id (&s, "home"));
	ck_assert (!settings_has_account_id (&s, "work"));
	ck_assert (!BarSettingsSetActiveAccountById (&s, "work"));

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_rejects_missing_account_file_only) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"account = solo:missing.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 0);
	ck_assert_ptr_null (BarSettingsGetActiveAccount (&s));

	BarSettingsDestroy (&s);
}
END_TEST
```

- [ ] **Step 3: Register tests in `settings_suite`**

```c
	tcase_add_test (tc, test_settings_rejects_missing_account_file_with_primary);
	tcase_add_test (tc, test_settings_rejects_missing_account_file_only);
```

- [ ] **Step 4: Run tests — expect FAIL**

```bash
cd /Users/khawes/stash/pianobar/remote-pianobar
make test 2>&1 | tail -30
```

Expected: `test_settings_rejects_missing_account_file_with_primary` fails (`accountCount` is 2 today, not 1). `test_settings_rejects_missing_account_file_only` fails (`accountCount` is 1 today, not 0).

- [ ] **Step 5: Commit tests only**

```bash
git add test/unit/test_settings.c
git commit -m "test: add failing cases for missing account snippet files"
```

---

## Task 2: Implement missing-file rejection (requirement A)

**Files:**
- Modify: `src/settings.c:451-587`

- [ ] **Step 1: Change `BarSettingsReadAccountFile` to return `bool`**

Replace the function signature and missing-file branch:

```c
static bool BarSettingsReadAccountFile (BarAccount_t *acct, const char *filepath) {
	FILE *fd = fopen (filepath, "r");
	if (fd == NULL) {
		log_write (LOG_ERROR,
				"Error: Account file not found: %s\n", filepath);
		return false;
	}
	/* ... existing parse loop unchanged ... */
	fclose (fd);
	return true;
}
```

- [ ] **Step 2: Refactor `BarSettingsBuildAccounts` to skip on missing file**

Replace the pre-sized `calloc(total, …)` + loop with incremental build:

```c
static bool BarSettingsAppendAccount (BarSettings_t *settings, BarAccount_t *acct) {
	BarAccount_t *newAccounts = realloc (settings->accounts,
			(settings->accountCount + 1) * sizeof (BarAccount_t));
	if (newAccounts == NULL) {
		log_write (LOG_ERROR, "Out of memory building account list\n");
		return false;
	}
	settings->accounts = newAccounts;
	settings->accounts[settings->accountCount] = *acct;
	settings->accountCount++;
	memset (acct, 0, sizeof (*acct)); /* ownership transferred */
	return true;
}

static void BarSettingsBuildAccounts (BarSettings_t *settings,
		const char *mainAccountId, const char *defaultAccount,
		const char *accountLabel,
		BarAccountLine_t *accountLines, size_t accountLineCount,
		const char *configDir, const char *userhome) {
	settings->accounts = NULL;
	settings->accountCount = 0;

	bool hasMainCredentials = (settings->username != NULL);

	if (hasMainCredentials) {
		BarAccount_t primary = {0};
		primary.id = strdup (mainAccountId ? mainAccountId : "default");
		primary.label = strdup (accountLabel ? accountLabel :
				(mainAccountId ? mainAccountId : "default"));
		primary.username = strdup (settings->username);
		if (settings->password) {
			primary.password = strdup (settings->password);
		}
		if (settings->passwordCmd) {
			primary.passwordCmd = strdup (settings->passwordCmd);
		}
		if (settings->autostartStation) {
			primary.autostartStation = strdup (settings->autostartStation);
		}
		if (!BarSettingsAppendAccount (settings, &primary)) {
			BarAccountDestroy (&primary);
		}
	}

	for (size_t i = 0; i < accountLineCount; i++) {
		BarAccount_t acct = {0};
		acct.id = strdup (accountLines[i].id);
		acct.label = strdup (accountLines[i].id);

		if (settings->username) {
			acct.username = strdup (settings->username);
		}
		if (settings->password) {
			acct.password = strdup (settings->password);
		}
		if (settings->passwordCmd) {
			acct.passwordCmd = strdup (settings->passwordCmd);
		}

		char *resolved = BarSettingsResolveAccountPath (accountLines[i].path,
				configDir, userhome);
		if (resolved == NULL) {
			log_write (LOG_ERROR,
					"Error: Out of memory resolving account file for '%s'\n",
					accountLines[i].id);
			BarAccountDestroy (&acct);
			continue;
		}

		if (!BarSettingsReadAccountFile (&acct, resolved)) {
			log_write (LOG_ERROR,
					"Error: Account '%s' not loaded (missing file: %s)\n",
					accountLines[i].id, resolved);
			BarAccountDestroy (&acct);
			free (resolved);
			continue;
		}
		free (resolved);

		/* duplicate check added in Task 4 — for now append unconditionally */
		if (!BarSettingsAppendAccount (settings, &acct)) {
			BarAccountDestroy (&acct);
		}
	}

	/* existing default_account resolution unchanged, using settings->accountCount */
	settings->activeAccountIndex = 0;
	if (defaultAccount) {
		bool found = false;
		for (size_t i = 0; i < settings->accountCount; i++) {
			if (streq (settings->accounts[i].id, defaultAccount)) {
				settings->activeAccountIndex = i;
				found = true;
				break;
			}
		}
		if (!found) {
			log_write (LOG_ERROR, "Warning: default_account '%s' not found, "
					"using first account\n", defaultAccount);
		}
	}
}
```

- [ ] **Step 3: Run Task 1 tests — expect PASS**

```bash
make test 2>&1 | grep -E 'settings_rejects_missing|tests passed|FAILED'
```

Expected: both missing-file tests PASS; full settings suite PASS.

- [ ] **Step 4: Commit**

```bash
git add src/settings.c
git commit -m "fix: skip file-backed accounts when snippet file is missing"
```

---

## Task 3: Failing tests — duplicate username (requirement B)

**Files:**
- Modify: `test/unit/test_settings.c`

- [ ] **Step 1: Write failing tests**

```c
START_TEST (test_settings_rejects_duplicate_credentials_explicit) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char accpath[512];
	snprintf (accpath, sizeof (accpath), "%s/pianobar/work.conf", tmpl);
	ck_assert_int_eq (write_file (accpath,
			"user = mainuser\n"
			"password = mainpass\n"
			"account_label = Work\n"), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = mainuser\n"
			"password = mainpass\n"
			"main_account_id = home\n"
			"account = work:work.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 1);
	ck_assert (settings_has_account_id (&s, "home"));
	ck_assert (!settings_has_account_id (&s, "work"));

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_rejects_duplicate_credentials_inherited) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char accpath[512];
	snprintf (accpath, sizeof (accpath), "%s/pianobar/work.conf", tmpl);
	/* File exists but does not override user/password — inherits main creds */
	ck_assert_int_eq (write_file (accpath, "account_label = Work\n"), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = mainuser\n"
			"password = mainpass\n"
			"main_account_id = home\n"
			"account = work:work.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 1);
	ck_assert (!settings_has_account_id (&s, "work"));

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_rejects_duplicate_username_different_password) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char accpath[512];
	snprintf (accpath, sizeof (accpath), "%s/pianobar/work.conf", tmpl);
	ck_assert_int_eq (write_file (accpath,
			"user = mainuser\n"
			"password = differentpass\n"
			"account_label = Work\n"), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = mainuser\n"
			"password = mainpass\n"
			"main_account_id = home\n"
			"account = work:work.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 1);
	ck_assert (settings_has_account_id (&s, "home"));
	ck_assert (!settings_has_account_id (&s, "work"));

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_rejects_duplicate_credentials_between_files) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char a1[512], a2[512];
	snprintf (a1, sizeof (a1), "%s/pianobar/a.conf", tmpl);
	snprintf (a2, sizeof (a2), "%s/pianobar/b.conf", tmpl);
	ck_assert_int_eq (write_file (a1, "user = shared\npassword = secret\n"), 0);
	ck_assert_int_eq (write_file (a2, "user = shared\npassword = secret\n"), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"account = a:a.conf\n"
			"account = b:b.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 1);
	ck_assert (settings_has_account_id (&s, "a"));
	ck_assert (!settings_has_account_id (&s, "b"));

	BarSettingsDestroy (&s);
}
END_TEST

START_TEST (test_settings_accepts_distinct_file_credentials) {
	char tmpl[] = "/tmp/piano_set_XXXXXX";
	ck_assert_ptr_nonnull (mkdtemp (tmpl));
	ck_assert_int_eq (mkdir_pianobar (tmpl), 0);

	char accpath[512];
	snprintf (accpath, sizeof (accpath), "%s/pianobar/work.conf", tmpl);
	ck_assert_int_eq (write_file (accpath,
			"user = otheruser\n"
			"password = otherpass\n"), 0);

	char cfg[512];
	config_path (tmpl, cfg, sizeof (cfg));
	ck_assert_int_eq (write_file (cfg,
			"user = mainuser\n"
			"password = mainpass\n"
			"main_account_id = home\n"
			"account = work:work.conf\n"), 0);

	BarSettings_t s;
	read_settings_in_dir (&s, tmpl);

	ck_assert_uint_eq (s.accountCount, 2);
	ck_assert (settings_has_account_id (&s, "home"));
	ck_assert (settings_has_account_id (&s, "work"));

	BarSettingsDestroy (&s);
}
END_TEST
```

- [ ] **Step 2: Register in `settings_suite`**

```c
	tcase_add_test (tc, test_settings_rejects_duplicate_credentials_explicit);
	tcase_add_test (tc, test_settings_rejects_duplicate_credentials_inherited);
	tcase_add_test (tc, test_settings_rejects_duplicate_username_different_password);
	tcase_add_test (tc, test_settings_rejects_duplicate_credentials_between_files);
	tcase_add_test (tc, test_settings_accepts_distinct_file_credentials);
```

- [ ] **Step 3: Run tests — expect FAIL on duplicate tests**

```bash
make test 2>&1 | grep -E 'duplicate_credentials|tests passed|FAILED'
```

Expected: four `rejects_duplicate_*` tests FAIL (`accountCount` too high); `accepts_distinct` PASS.

- [ ] **Step 4: Commit**

```bash
git add test/unit/test_settings.c
git commit -m "test: add failing cases for duplicate account usernames"
```

---

## Task 4: Implement duplicate-username rejection (requirement B)

**Files:**
- Modify: `src/settings.c` (before `BarSettingsBuildAccounts`)

- [ ] **Step 1: Add username comparison helper**

```c
static bool BarAccountUsernamesEqual (const BarAccount_t *a,
		const BarAccount_t *b) {
	if (a == NULL || b == NULL) {
		return false;
	}
	if (a->username == NULL || b->username == NULL) {
		return false;
	}
	return strcmp (a->username, b->username) == 0;
}

static const BarAccount_t *BarSettingsFindDuplicateAccount (
		const BarSettings_t *settings, const BarAccount_t *candidate) {
	for (size_t i = 0; i < settings->accountCount; i++) {
		if (BarAccountUsernamesEqual (candidate, &settings->accounts[i])) {
			return &settings->accounts[i];
		}
	}
	return NULL;
}
```

- [ ] **Step 2: Wire duplicate check into file-backed loop**

After successful `BarSettingsReadAccountFile`, before `BarSettingsAppendAccount`:

```c
		const BarAccount_t *dup = BarSettingsFindDuplicateAccount (settings, &acct);
		if (dup != NULL) {
			log_write (LOG_ERROR,
					"Error: Account '%s' has the same username as account '%s' "
					"(not loaded)\n",
					accountLines[i].id,
					dup->id ? dup->id : "(unknown)");
			BarAccountDestroy (&acct);
			continue;
		}
```

- [ ] **Step 3: Run full settings + regression suite**

```bash
make test 2>&1 | tail -20
make test-all
```

Expected: all new tests PASS; existing `test_settings_main_plus_file_accounts_default`, `test_settings_file_backed_account_only`, `test_settings_set_active_account_by_id` still PASS.

- [ ] **Step 4: Commit**

```bash
git add src/settings.c
git commit -m "fix: reject file-backed accounts with duplicate username"
```

---

## Task 5: Update CONFIG.md

**Files:**
- Modify: `CONFIG.md` (section “Multiple Pandora accounts”)

- [ ] **Step 1: Add validation subsection after “Inheritance”**

```markdown
**Validation at load time:**

- If an `account = id:path` snippet file cannot be opened, remote-pianobar logs an error and **does not** add that account to the runtime account list.
- If a file-backed account’s merged `user` (after inheritance) matches the `user` of an account already in the list, remote-pianobar logs an error and **does not** add the duplicate. Password / `password_command` differences do not exempt a second account with the same username.
- `default_account` must refer to an account that was actually loaded; otherwise the first loaded account is used (existing warning).
```

- [ ] **Step 2: Commit**

```bash
git add CONFIG.md
git commit -m "docs: document account snippet validation rules"
```

---

## Task 6: Final verification

- [ ] **Step 1: Run pre-push gate**

```bash
make test-all
```

Allow ≥120s. Expected: exit 0.

- [ ] **Step 2: Manual smoke (optional)**

Create a temp config with `account = work:missing.conf` and confirm log contains `Account file not found` and web UI `process.accounts` omits `work`.

---

## Test matrix (summary)

| Test | Config | Expected `accountCount` | Expected ids |
|------|--------|-------------------------|--------------|
| `rejects_missing_account_file_with_primary` | main user + missing snippet | 1 | `home` only |
| `rejects_missing_account_file_only` | snippet only, missing | 0 | — |
| `rejects_duplicate_credentials_explicit` | main + snippet same user/pass | 1 | `home` only |
| `rejects_duplicate_credentials_inherited` | main + snippet label-only | 1 | `home` only |
| `rejects_duplicate_username_different_password` | main + snippet same user, diff pass | 1 | `home` only |
| `rejects_duplicate_credentials_between_files` | two snippets, same user | 1 | `a` only |
| `accepts_distinct_file_credentials` | main + different snippet user | 2 | `home`, `work` |

---

## Out of scope

- Changing `BarSettingsRead` to `bool` and aborting startup (errors are logged; invalid accounts omitted).
- Runtime `app.pandora-reconnect` validation (config is validated once at load).
- Case-insensitive username comparison (usernames are compared as stored in config).
