package com.dj5927.rpgmakerplayer;

public final class GameEntryMetadataSmoke {
    public static void main(String[] args) {
        GameEntry archive = new GameEntry(
                "RelicsWalker-v103a",
                GameEntry.Engine.EASYRPG,
                "archive-document-id",
                null,
                true,
                "thumbnail-document-id",
                "shared-rtp-document-id",
                "wrapper-folder-document-id");

        GameEntry renamed = archive.withTitle("렐릭스 워커");
        require("렐릭스 워커".equals(renamed.title), "title not changed");
        require(renamed.engine == archive.engine, "engine changed");
        require(renamed.archive, "archive flag lost");
        require(archive.documentId.equals(renamed.documentId), "archive documentId changed");
        require(archive.saveRootDocumentId.equals(renamed.saveRootDocumentId),
                "archive wrapper/saveRootDocumentId lost");
        require(archive.thumbnailDocumentId.equals(renamed.thumbnailDocumentId),
                "thumbnail metadata lost");
        require(archive.sharedRtpDocumentId.equals(renamed.sharedRtpDocumentId),
                "shared RTP metadata lost");
        require(archive.stableId().equals(renamed.stableId()), "stable id changed after rename");
        System.out.println("GAME_ENTRY_METADATA_SMOKE_PASS");
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
