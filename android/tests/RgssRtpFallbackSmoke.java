package com.dj5927.rpgmakerplayer;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public final class RgssRtpFallbackSmoke {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        Path temp = Files.createTempDirectory("rpgmp-rtp-smoke-");
        File rtp = temp.resolve("RTP").toFile();
        File xp = new File(rtp, "XP");
        File vx = new File(rtp, "vx");
        File vxace = new File(rtp, "VXAce");
        File fonts = new File(rtp, "FONTS");
        check(xp.mkdirs(), "xp mkdir");
        check(vx.mkdirs(), "vx mkdir");
        check(vxace.mkdirs(), "vxace mkdir");
        check(fonts.mkdirs(), "fonts mkdir");

        List<File> ace = RgssRtpFallback.searchRootsFromRtpRoot(rtp, "VXACE");
        check(ace.size() == 2, "VX Ace should mount engine RTP + shared rtp root");
        check(ace.get(0).getCanonicalFile().equals(vxace.getCanonicalFile()), "VX Ace engine root order");
        check(ace.get(1).getCanonicalFile().equals(rtp.getCanonicalFile()), "shared rtp root order");
        check(RgssRtpFallback.fontDirectoryFromRtpRoot(rtp).getCanonicalFile().equals(fonts.getCanonicalFile()),
                "case-insensitive fonts fallback");

        List<File> xpRoots = RgssRtpFallback.searchRootsFromRtpRoot(rtp, "XP");
        check(xpRoots.get(0).getCanonicalFile().equals(xp.getCanonicalFile()), "XP engine root");
        List<File> vxRoots = RgssRtpFallback.searchRootsFromRtpRoot(rtp, "VX");
        check(vxRoots.get(0).getCanonicalFile().equals(vx.getCanonicalFile()), "VX engine root");

        File noRtp = temp.resolve("NoRtp").toFile();
        check(RgssRtpFallback.searchRootsFromRtpRoot(noRtp, "VXACE").isEmpty(),
                "missing RTP must stay optional");

        System.out.println("RGSS_RTP_FALLBACK_SMOKE_PASS");
    }
}
