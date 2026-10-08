package com.dj5927.rpgmakerplayer;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Resolves optional shared RGSS RTP fallback folders.
 *
 * Expected layout:
 *   <selected game root>/rtp/xp
 *   <selected game root>/rtp/vx
 *   <selected game root>/rtp/vxace
 *   <selected game root>/rtp/fonts
 *
 * The engine-specific directory is mounted first, followed by the rtp root.
 * Mounting the mirrored root also exposes rtp/fonts as a virtual Fonts/ directory to
 * mkxp-z's built-in font inventory scanner. Directory names are matched
 * case-insensitively because Android filesystems may be case-sensitive while
 * legacy RPG Maker game packs often are not consistent about casing.
 */
public final class RgssRtpFallback {
    private RgssRtpFallback() {}

    public static List<File> searchRootsFromRtpRoot(File rtpRoot, String engine) {
        if (rtpRoot == null || !rtpRoot.isDirectory()) return Collections.emptyList();

        ArrayList<File> out = new ArrayList<>();
        String engineFolder = engineFolder(engine);
        if (engineFolder != null) {
            File engineRoot = findDirectoryIgnoreCase(rtpRoot, engineFolder);
            if (engineRoot != null) out.add(engineRoot);
        }

        // Keep this last: it exposes rtp/fonts to mkxp-z's Fonts scanner and
        // also allows explicitly shared RTP resources without overriding the
        // engine-specific pack above it.
        out.add(rtpRoot);
        return out;
    }

    public static File fontDirectoryFromRtpRoot(File rtpRoot) {
        if (rtpRoot == null || !rtpRoot.isDirectory()) return null;
        return findDirectoryIgnoreCase(rtpRoot, "fonts");
    }

    private static String engineFolder(String engine) {
        if (engine == null) return null;
        if ("XP".equalsIgnoreCase(engine)) return "xp";
        if ("VX".equalsIgnoreCase(engine)) return "vx";
        if ("VXACE".equalsIgnoreCase(engine) || "VX ACE".equalsIgnoreCase(engine)) return "vxace";
        return null;
    }

    private static File findDirectoryIgnoreCase(File parent, String wanted) {
        File direct = new File(parent, wanted);
        if (direct.isDirectory()) return direct;

        File[] children = parent.listFiles();
        if (children == null) return null;
        for (File child : children) {
            if (child.isDirectory() && child.getName().equalsIgnoreCase(wanted)) return child;
        }
        return null;
    }
}
