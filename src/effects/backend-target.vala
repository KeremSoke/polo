/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

namespace Polo.Effects {
    public delegate GLib.GenericArray<Backend.View> ViewCollector ();

    private class ViewComponent : GLib.Object {
        public Backend.View? view;
        public Polo.Fx.Grid? grid = null;
        private ulong destroyed_handler;

        public signal void gone ();

        public ViewComponent (Backend.View view) {
            this.view = view;
            destroyed_handler = view.destroyed.connect (on_destroyed);
        }

        private void on_destroyed () {
            release ();
            gone ();
        }

        public void release () {
            if (view != null) {
                view.disconnect (destroyed_handler);
                view = null;
            }
        }
    }

    public class BackendTarget : Target {
        private unowned Backend.Toplevel toplevel;
        private ViewCollector collect;
        private GLib.GenericArray<ViewComponent> components =
            new GLib.GenericArray<ViewComponent> ();
        private ulong unmanaged_handler;
        private uint grid_cols = 0;
        private uint grid_rows = 0;
        private bool stale = false;

        public BackendTarget (Window window, Backend.Toplevel toplevel, owned ViewCollector collect) {
            Object (window: window);
            this.toplevel = toplevel;
            this.collect = (owned) collect;
            add_components ();
            unmanaged_handler = window.unmanaged.connect (() => gone ());
        }

        protected override void dispose () {
            if (unmanaged_handler != 0) {
                window.disconnect (unmanaged_handler);
                unmanaged_handler = 0;
            }
            drop_components ();
            base.dispose ();
        }

        private void add_components () {
            foreach (unowned Backend.View view in collect ()) {
                var component = new ViewComponent (view);
                component.gone.connect (on_component_gone);
                components.add (component);
            }
        }

        private void drop_components () {
            foreach (var component in components) {
                component.gone.disconnect (on_component_gone);
                component.release ();
            }
            components = new GLib.GenericArray<ViewComponent> ();
        }

        private void on_component_gone () {
            stale = true;
        }

        private bool refresh () {
            if (!stale) {
                return true;
            }
            stale = false;
            drop_components ();
            add_components ();
            return grid_cols == 0 || prepare_component_grids (grid_cols, grid_rows);
        }

        public bool prepare_component_grids (uint cols, uint rows) {
            grid_cols = cols;
            grid_rows = rows;
            foreach (var component in components) {
                if (component.view != null && component.grid == null) {
                    component.grid = Polo.Fx.Grid.create (cols, rows);
                    if (component.grid == null) {
                        return false;
                    }
                }
            }
            return true;
        }

        public bool get_surface_rect (out Polo.Marco.Rect rect) {
            rect = {};
            bool have_rect = false;
            foreach (var component in components) {
                if (component.view == null) {
                    continue;
                }
                double x, y;
                int width, height;
                component.view.get_position (out x, out y);
                component.view.get_size (out width, out height);
                if (width <= 0 || height <= 0) {
                    continue;
                }
                var part = Polo.Marco.Rect () {
                    x = (int) x, y = (int) y, width = width, height = height
                };
                if (!have_rect) {
                    rect = part;
                    have_rect = true;
                } else {
                    int right = int.max (rect.x + rect.width, part.x + part.width);
                    int bottom = int.max (rect.y + rect.height, part.y + part.height);
                    rect.x = int.min (rect.x, part.x);
                    rect.y = int.min (rect.y, part.y);
                    rect.width = right - rect.x;
                    rect.height = bottom - rect.y;
                }
            }
            return have_rect;
        }

        public override bool deform () {
            if (grid == null || !toplevel.is_mapped () || !refresh ()) {
                return false;
            }
            Polo.Marco.Rect domain;
            if (!get_surface_rect (out domain) || domain.width <= 0 || domain.height <= 0) {
                return false;
            }
            foreach (var component in components) {
                if (component.view == null) {
                    continue;
                }
                double x, y;
                int width, height;
                component.view.get_position (out x, out y);
                component.view.get_size (out width, out height);
                if (width <= 0 || height <= 0) {
                    continue;
                }
                if (component.grid == null || component.grid.cols != grid.cols ||
                    component.grid.rows != grid.rows) {
                    return false;
                }
                float u0 = (float) (x - domain.x) / domain.width;
                float v0 = (float) (y - domain.y) / domain.height;
                float u1 = (float) (x + width - domain.x) / domain.width;
                float v1 = (float) (y + height - domain.y) / domain.height;
                component.grid.set_subgrid (grid, u0, v0, u1, v1);
                if (!component.view.set_deformation (component.grid.vertices,
                                                     component.grid.cols,
                                                     component.grid.rows)) {
                    return false;
                }
            }
            return true;
        }

        public override void undeform () {
            foreach (var component in components) {
                if (component.view != null) {
                    component.view.clear_deformation ();
                }
            }
        }
    }
}
