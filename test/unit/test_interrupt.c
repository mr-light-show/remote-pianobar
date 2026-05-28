/*
Copyright (c) 2026

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
#include <signal.h>

#include "../../src/interrupt.h"

START_TEST (test_interrupt_increment_no_target_is_noop)
{
	BarInterruptSetTarget (NULL);
	BarInterruptIncrement ();
	ck_assert_ptr_null (BarInterruptGetTarget ());
}
END_TEST

START_TEST (test_interrupt_increment_updates_target)
{
	sig_atomic_t flag = 0;
	BarInterruptSetTarget (&flag);
	BarInterruptIncrement ();
	ck_assert_int_eq (flag, 1);
	BarInterruptIncrement ();
	ck_assert_int_eq (flag, 2);
	BarInterruptSetTarget (NULL);
}
END_TEST

Suite *
interrupt_suite (void)
{
	Suite *s = suite_create ("interrupt");
	TCase *tc = tcase_create ("core");
	tcase_add_test (tc, test_interrupt_increment_no_target_is_noop);
	tcase_add_test (tc, test_interrupt_increment_updates_target);
	suite_add_tcase (s, tc);
	return s;
}
