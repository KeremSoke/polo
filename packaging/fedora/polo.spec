%global weston_version 16.0.1
# The private libweston is Polo's alone: keep it out of the package's
# provides and requires.
%global __provides_exclude_from ^%{_libdir}/polo/.*$
%global __requires_exclude ^libweston-16\\.so.*$

Name:           polo
Version:        1.0.1
Release:        1%{?dist}
Summary:        Wayland compositor and session for the MATE desktop

License:        GPL-3.0-or-later AND GPL-2.0-or-later AND LGPL-2.0-or-later AND LGPL-2.1-or-later AND MIT
URL:            https://github.com/KeremSoke/polo
# meson dist --include-subprojects: includes the patched Weston tree.
Source0:        %{url}/releases/download/v%{version}/%{name}-%{version}.tar.xz

BuildRequires:  gcc
BuildRequires:  meson >= 1.4.0
BuildRequires:  vala >= 0.56
BuildRequires:  systemd-rpm-macros
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconfig(glib-2.0) >= 2.80
BuildRequires:  pkgconfig(gio-unix-2.0)
BuildRequires:  pkgconfig(dconf)
BuildRequires:  pkgconfig(gtk+-3.0) >= 3.24
BuildRequires:  pkgconfig(gtk+-wayland-3.0)
BuildRequires:  pkgconfig(gtk-layer-shell-0)
BuildRequires:  pkgconfig(mate-desktop-2.0)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-protocols)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(libinput)
BuildRequires:  pkgconfig(libevdev)
BuildRequires:  pkgconfig(libseat)
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(systemd)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(gbm)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(libdisplay-info)
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(cairo)
BuildRequires:  pkgconfig(pango)
BuildRequires:  pkgconfig(lcms2)
BuildRequires:  pkgconfig(libpng)
BuildRequires:  pkgconfig(xcb)
BuildRequires:  pkgconfig(xcb-ewmh)
BuildRequires:  pkgconfig(xcb-icccm)
BuildRequires:  pkgconfig(xcb-errors)
BuildRequires:  pkgconfig(xcb-cursor)
BuildRequires:  pkgconfig(xcursor)
BuildRequires:  pkgconfig(xwayland)
BuildRequires:  pkgconfig(libcanberra)
BuildRequires:  pkgconfig(libpulse)
BuildRequires:  pkgconfig(sm)
BuildRequires:  pkgconfig(ice)
BuildRequires:  pkgconfig(json-glib-1.0)

Requires:       xorg-x11-server-Xwayland
Requires:       mate-panel >= 1.28
Requires:       caja
Requires:       mate-settings-daemon
Requires:       xdg-desktop-portal-wlr
Requires:       xdg-desktop-portal-gtk
Requires:       dconf
Recommends:     mate-screensaver
Recommends:     grim
Recommends:     slurp
Recommends:     wl-clipboard

Provides:       bundled(libweston) = %{weston_version}

%description
Polo is a Wayland compositor and window manager for MATE, built on a
private, patched copy of the Weston compositor library. It reads Marco's
settings, manages X11 programs through its own XWayland window manager, and
provides the "MATE (Polo)" session: it starts the session's programs and is
the session manager itself.

%prep
%autosetup -p1

%build
%meson -Dtests=false
%meson_build

%install
%meson_install
# libweston's headers, pkg-config files and protocol XML are for building
# against it; nothing does but Polo.
rm -rf %{buildroot}%{_libdir}/polo/include \
       %{buildroot}%{_libdir}/polo/pkgconfig \
       %{buildroot}%{_libdir}/polo/share \
       %{buildroot}%{_libdir}/polo/libweston-16.so

%check
desktop-file-validate %{buildroot}%{_datadir}/wayland-sessions/polo.desktop || :

%post
dconf update &>/dev/null || :

%postun
dconf update &>/dev/null || :

%files
%license LICENSES/*
%doc README.md
%{_bindir}/polo
%{_bindir}/polo-wayland.sh
%{_bindir}/polo-screenshot
%{_libexecdir}/polo-shell-ui
%{_libexecdir}/polo-share-chooser
%dir %{_libdir}/polo
%{_libdir}/polo/libweston-16.so.0*
%{_libdir}/polo/libweston-16/
%{_datadir}/wayland-sessions/polo.desktop
%{_datadir}/glib-2.0/schemas/org.mate.polo.gschema.xml
%{_datadir}/polo/
%config(noreplace) %{_sysconfdir}/dconf/profile/polo
%dir %{_sysconfdir}/dconf/db/polo.d
%dir %{_sysconfdir}/dconf/db/polo.d/locks
%config(noreplace) %{_sysconfdir}/dconf/db/polo.d/00-polo
%config(noreplace) %{_sysconfdir}/dconf/db/polo.d/locks/00-polo
%{_userunitdir}/polo-session.target
%{_mandir}/man1/polo.1*
%{_mandir}/man7/polo-session.7*

%changelog
* Fri Nov 13 2026 Kerem Soke - 1.0.1-1
- Fixes for the magnifier, desktop menus, panel crashes, install and Xwayland

* Wed Nov 11 2026 Kerem Soke - 1.0.0-1
- First release
