package com.dj5927.rpgmakerplayer;

import java.io.Serializable;

public final class GameEntry implements Serializable {
    public enum Engine {
        EASYRPG("2K / 2K3"), XP("XP"), VX("VX"), VXACE("VX Ace"), MV("MV"), MZ("MZ");
        public final String label;
        Engine(String label) { this.label = label; }
    }

    public final String title;
    public final Engine engine;
    public final String documentId;
    public final String webRootDocumentId;
    public final boolean archive;
    public final String thumbnailDocumentId;
    public final String sharedRtpDocumentId;
    public final String saveRootDocumentId;

    public GameEntry(String title, Engine engine, String documentId, String webRootDocumentId, boolean archive) {
        this(title, engine, documentId, webRootDocumentId, archive, null, null, documentId);
    }

    public GameEntry(String title, Engine engine, String documentId, String webRootDocumentId,
                     boolean archive, String thumbnailDocumentId) {
        this(title, engine, documentId, webRootDocumentId, archive, thumbnailDocumentId, null, documentId);
    }

    public GameEntry(String title, Engine engine, String documentId, String webRootDocumentId,
                     boolean archive, String thumbnailDocumentId, String sharedRtpDocumentId) {
        this(title, engine, documentId, webRootDocumentId, archive, thumbnailDocumentId,
                sharedRtpDocumentId, documentId);
    }

    public GameEntry(String title, Engine engine, String documentId, String webRootDocumentId,
                     boolean archive, String thumbnailDocumentId, String sharedRtpDocumentId,
                     String saveRootDocumentId) {
        this.title = title;
        this.engine = engine;
        this.documentId = documentId;
        this.webRootDocumentId = webRootDocumentId;
        this.archive = archive;
        this.thumbnailDocumentId = thumbnailDocumentId;
        this.sharedRtpDocumentId = sharedRtpDocumentId;
        this.saveRootDocumentId = saveRootDocumentId == null || saveRootDocumentId.isEmpty()
                ? documentId : saveRootDocumentId;
    }

    public GameEntry withTitle(String newTitle) {
        return new GameEntry(newTitle, engine, documentId, webRootDocumentId, archive,
                thumbnailDocumentId, sharedRtpDocumentId, saveRootDocumentId);
    }

    public String stableId() { return engine.name() + ":" + documentId; }
}
