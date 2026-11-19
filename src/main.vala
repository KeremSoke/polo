/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

[CCode (cname = "mallopt", cheader_filename = "malloc.h")]
extern int mallopt (int param, int value);

[CCode (cname = "M_MMAP_THRESHOLD", cheader_filename = "malloc.h")]
extern const int M_MMAP_THRESHOLD;

public static int main (string[] args) {
    /* A fixed threshold keeps glibc from raising it after the wallpaper's
     * multi-megabyte buffers are freed, which fragments the heap. */
    mallopt (M_MMAP_THRESHOLD, 128 * 1024);
    Intl.setlocale (LocaleCategory.ALL, "");
    return new Polo.Application ().run (args);
}
