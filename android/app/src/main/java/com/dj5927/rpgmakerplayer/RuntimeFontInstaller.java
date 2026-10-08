package com.dj5927.rpgmakerplayer;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public final class RuntimeFontInstaller {
    private static final String ASSET = "fonts/wqymicrohei.ttf";
    private static final String FILE_NAME = "wqymicrohei.ttf";

    private RuntimeFontInstaller() {}

    public static File ensure(Context context) throws Exception {
        File dir = new File(context.getFilesDir(), "runtime_fonts");
        if (!dir.exists() && !dir.mkdirs()) {
            throw new IllegalStateException("Cannot create runtime font dir: " + dir);
        }
        File target = new File(dir, FILE_NAME);

        long assetLength;
        try (InputStream in = context.getAssets().open(ASSET)) {
            assetLength = in.available();
        }
        if (target.isFile() && target.length() == assetLength && target.length() > 0) return target;

        File tmp = new File(dir, FILE_NAME + ".tmp");
        try (InputStream in = context.getAssets().open(ASSET);
             FileOutputStream out = new FileOutputStream(tmp, false)) {
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) >= 0) out.write(buffer, 0, read);
            out.getFD().sync();
        }
        if (target.exists() && !target.delete()) {
            throw new IllegalStateException("Cannot replace runtime font: " + target);
        }
        if (!tmp.renameTo(target)) {
            throw new IllegalStateException("Cannot install runtime font: " + target);
        }
        return target;
    }
}
