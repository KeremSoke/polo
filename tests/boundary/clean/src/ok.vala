// API names in comments and strings must not count as dependencies.
void use_backend () {
    unowned string v = Polo.Backend.libweston_version ();
    print ("Wl.Display %s\n", v);
}
