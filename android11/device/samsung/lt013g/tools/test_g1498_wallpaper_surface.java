package com.android.systemui.glwallpaper;

public final class test_g1498_wallpaper_surface {
    private static void expect(int[] actual, int width, int height) {
        if (actual[0] != width || actual[1] != height) {
            throw new AssertionError(actual[0] + "x" + actual[1] + " != " + width + "x" + height);
        }
    }

    private static void expectClose(double a, double b) {
        if (Math.abs(a - b) > 1e-9) {
            throw new AssertionError(a + " != " + b);
        }
    }

    public static void main(String[] args) {
        expect(WallpaperSurfaceSizeCalculator.calculate(1920, 1920, 800, 1280, true), 1280, 1280);
        expect(WallpaperSurfaceSizeCalculator.calculate(1920, 1920, 1280, 800, true), 1280, 1280);
        expect(WallpaperSurfaceSizeCalculator.calculate(1920, 1920, 800, 1280, false), 1920, 1920);
        expect(WallpaperSurfaceSizeCalculator.calculate(1024, 1024, 800, 1280, true), 1024, 1024);
        expect(WallpaperSurfaceSizeCalculator.calculate(2500, 1800, 800, 1280, true), 2500, 1800);
        expect(WallpaperSurfaceSizeCalculator.calculate(0, 0, 800, 1280, true), 0, 0);
        expect(WallpaperSurfaceSizeCalculator.calculate(1920, 1920, 0, 1280, true), 1920, 1920);
        final int oldWidth = 1920;
        final int oldHeight = 1920;
        final int newWidth = 1280;
        final int newHeight = 1280;
        final int visibleWidth = 800;
        final int visibleHeight = 1280;
        final double beforeX = (oldWidth - oldHeight * visibleWidth / (double) visibleHeight)
                / (2.0 * oldWidth);
        final double afterX = (newWidth - newHeight * visibleWidth / (double) visibleHeight)
                / (2.0 * newWidth);
        expectClose(beforeX, afterX);
        expectClose(1.0 - beforeX, 1.0 - afterX);
        final double beforeLeftCrop = (oldHeight * visibleWidth / (double) visibleHeight)
                / oldWidth;
        final double afterLeftCrop = (newHeight * visibleWidth / (double) visibleHeight)
                / newWidth;
        expectClose(beforeLeftCrop, afterLeftCrop);
        expectClose(0.0, 0.0);
        System.out.println("G1498_SQUARE_WALLPAPER_PORTRAIT_LANDSCAPE_CROP_AND_OFF_SWITCH_PASS");
    }
}
