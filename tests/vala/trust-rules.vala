/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Which programs Polo vouches for without having started them: a listed path
 * that is a root-owned file nobody else can write, and the descendants of a
 * helper Polo started.
 */

using Polo.Policy;

void test_root_owned_file () {
    assert (TrustRules.root_owned_file ("/usr/bin/true"));
    assert (!TrustRules.root_owned_file (null));
    assert (!TrustRules.root_owned_file ("usr/bin/true"));
    assert (!TrustRules.root_owned_file ("/usr/bin"));
    assert (!TrustRules.root_owned_file ("/usr/bin/this-does-not-exist"));
    assert (!TrustRules.root_owned_file ("/usr/bin/true (deleted)"));
}

void test_user_owned_file_is_refused () {
    string path;
    try {
        int fd = GLib.FileUtils.open_tmp ("polo-trust-XXXXXX", out path);
        Posix.close (fd);
    } catch (GLib.FileError e) {
        error ("%s", e.message);
    }
    assert (!TrustRules.root_owned_file (path));
    assert (!TrustRules.executable_trusted (path, { path }));
    GLib.FileUtils.unlink (path);
}

void test_list_decides () {
    assert (TrustRules.executable_trusted ("/usr/bin/true", { "/usr/bin/true" }));
    assert (TrustRules.executable_trusted ("/usr/bin/true", { "/usr/bin/false", "/usr/bin/true" }));
    assert (!TrustRules.executable_trusted ("/usr/bin/true", { "/usr/bin/false" }));
    assert (!TrustRules.executable_trusted ("/usr/bin/true", {}));
    assert (!TrustRules.executable_trusted (null, { "/usr/bin/true" }));
}

void test_descendants () {
    int me = (int) Posix.getpid ();
    GLib.Subprocess child;
    try {
        child = new GLib.Subprocess.newv ({ "sleep", "30" }, GLib.SubprocessFlags.NONE);
    } catch (GLib.Error e) {
        error ("%s", e.message);
    }
    int pid = int.parse (child.get_identifier ());

    assert (TrustRules.has_ancestor (pid, { me }));
    assert (TrustRules.has_ancestor (pid, { 1234567, me }));
    assert (!TrustRules.has_ancestor (pid, {}));
    assert (!TrustRules.has_ancestor (pid, { 1234567 }));
    assert (!TrustRules.has_ancestor (pid, { 1 }));
    assert (!TrustRules.has_ancestor (me, { pid }));
    assert (!TrustRules.has_ancestor (1, { me }));
    assert (!TrustRules.has_ancestor (-5, { me }));
    child.force_exit ();
}

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/trust-rules/root-owned-file", test_root_owned_file);
    GLib.Test.add_func ("/trust-rules/user-owned-file", test_user_owned_file_is_refused);
    GLib.Test.add_func ("/trust-rules/list", test_list_decides);
    GLib.Test.add_func ("/trust-rules/descendants", test_descendants);
    return GLib.Test.run ();
}
