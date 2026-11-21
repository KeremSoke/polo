/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Which programs Polo vouches for without having started them: a listed path
 * that is a root-owned file nobody else can write.
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
    if (Posix.getuid () != 0) {
        assert (!TrustRules.root_owned_file (path));
        assert (!TrustRules.executable_trusted (path, { path }));
    }
    GLib.FileUtils.chmod (path, 0666);
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

int main (string[] args) {
    GLib.Test.init (ref args);
    GLib.Test.add_func ("/trust-rules/root-owned-file", test_root_owned_file);
    GLib.Test.add_func ("/trust-rules/user-owned-file", test_user_owned_file_is_refused);
    GLib.Test.add_func ("/trust-rules/list", test_list_decides);
    return GLib.Test.run ();
}
