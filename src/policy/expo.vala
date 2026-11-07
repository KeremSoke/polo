/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Expo's layout on one output, as Wayfire's expo plugin lays it out: the
 * workspace grid of cells, each the whole output scaled by one factor, a
 * gap around and between them, centred. Room for a label can be kept under
 * each row. A workspace's content is its
 * output-sized area scaled into its cell, so points map in and out by that
 * scale.
 */

namespace Polo.Policy {
    public class ExpoLayout {
        public Marco.Rect output;
        public int columns;
        public int rows;
        public double scale;

        private double left;
        private double top;
        private double gap;
        private double below;

        public ExpoLayout (Marco.Rect output, int columns, int rows, int gap, int below = 0) {
            this.output = output;
            this.columns = int.max (columns, 1);
            this.rows = int.max (rows, 1);
            this.gap = int.max (gap, 0);
            this.below = int.max (below, 0);
            double width = double.max (output.width - (this.columns + 1) * this.gap, 1.0);
            double height = double.max (output.height - (this.rows + 1) * this.gap - this.rows * this.below, 1.0);
            scale = double.min (width / (this.columns * output.width), height / (this.rows * output.height));
            double grid_width = this.columns * output.width * scale + (this.columns - 1) * this.gap;
            double grid_height = this.rows * (output.height * scale + this.below) + (this.rows - 1) * this.gap;
            left = output.x + (output.width - grid_width) / 2.0;
            top = output.y + (output.height - grid_height) / 2.0;
        }

        private double cell_x (int column) {
            return left + column * (output.width * scale + gap);
        }

        private double cell_y (int row) {
            return top + row * (output.height * scale + gap + below);
        }

        public Marco.Rect cell (int column, int row) {
            int x = (int) GLib.Math.round (cell_x (column));
            int y = (int) GLib.Math.round (cell_y (row));
            return Marco.Rect () {
                x = x, y = y,
                width = (int) GLib.Math.round (output.width * scale),
                height = (int) GLib.Math.round (output.height * scale)
            };
        }

        public void into_cell (int column, int row, double x, double y, out double cx, out double cy) {
            cx = cell_x (column) + (x - output.x) * scale;
            cy = cell_y (row) + (y - output.y) * scale;
        }

        public void out_of_cell (int column, int row, double cx, double cy, out double x, out double y) {
            x = output.x + (cx - cell_x (column)) / scale;
            y = output.y + (cy - cell_y (row)) / scale;
        }

        public bool hit (double x, double y, out int column, out int row) {
            column = (int) GLib.Math.floor ((x - left + gap / 2.0) / (output.width * scale + gap));
            row = (int) GLib.Math.floor ((y - top + gap / 2.0) / (output.height * scale + gap + below));
            if (column < 0 || column >= columns || row < 0 || row >= rows) {
                return false;
            }
            return x >= cell_x (column) && x < cell_x (column) + output.width * scale &&
                   y >= cell_y (row) && y < cell_y (row) + output.height * scale;
        }

        public double zoom_factor (double t) {
            return (1.0 / scale) * (1.0 - t) + t;
        }

        public void zoom_point (int column, int row, double t, double x, double y, out double zx, out double zy) {
            double k = zoom_factor (t);
            zx = output.x + (cell_x (column) - output.x) * t + (x - cell_x (column)) * k;
            zy = output.y + (cell_y (row) - output.y) * t + (y - cell_y (row)) * k;
        }
    }
}
