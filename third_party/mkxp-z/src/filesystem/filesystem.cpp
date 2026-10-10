/*
** filesystem.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "filesystem.h"

#include "util/boost-hash.h"
#include "util/debugwriter.h"
#include "util/exception.h"
#include "util/util.h"
#include "display/font.h"
#include "crypto/rgssad.h"

#include "eventthread.h"
#include "sharedstate.h"

#include <physfs.h>

#include <algorithm>
#include <dirent.h>
#include <map>
#include <set>
#include <stack>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include <iconv.h>
#endif

#ifdef __WIN32__
#include <direct.h>
#endif

struct SDLRWIoContext {
  SDL_RWops *ops;
  std::string filename;

  SDLRWIoContext(const char *filename)
      : ops(SDL_RWFromFile(filename, "r")), filename(filename) {
    if (!ops)
      throw Exception(Exception::SDLError, "Failed to open file: %s",
                      SDL_GetError());
  }

  ~SDLRWIoContext() { SDL_RWclose(ops); }
};

static PHYSFS_Io *createSDLRWIo(const char *filename);

static SDL_RWops *getSDLRWops(PHYSFS_Io *io) {
  return static_cast<SDLRWIoContext *>(io->opaque)->ops;
}

static PHYSFS_sint64 SDLRWIoRead(struct PHYSFS_Io *io, void *buf,
                                 PHYSFS_uint64 len) {
  return SDL_RWread(getSDLRWops(io), buf, 1, len);
}

static int SDLRWIoSeek(struct PHYSFS_Io *io, PHYSFS_uint64 offset) {
  return (SDL_RWseek(getSDLRWops(io), offset, RW_SEEK_SET) != -1);
}

static PHYSFS_sint64 SDLRWIoTell(struct PHYSFS_Io *io) {
  return SDL_RWseek(getSDLRWops(io), 0, RW_SEEK_CUR);
}

static PHYSFS_sint64 SDLRWIoLength(struct PHYSFS_Io *io) {
  return SDL_RWsize(getSDLRWops(io));
}

static struct PHYSFS_Io *SDLRWIoDuplicate(struct PHYSFS_Io *io) {
  SDLRWIoContext *ctx = static_cast<SDLRWIoContext *>(io->opaque);
  int64_t offset = io->tell(io);
  PHYSFS_Io *dup = createSDLRWIo(ctx->filename.c_str());

  if (dup)
    SDLRWIoSeek(dup, offset);

  return dup;
}

static void SDLRWIoDestroy(struct PHYSFS_Io *io) {
  delete static_cast<SDLRWIoContext *>(io->opaque);
  delete io;
}

static PHYSFS_Io SDLRWIoTemplate = {0,
                                    0, /* version, opaque */
                                    SDLRWIoRead,
                                    0, /* write */
                                    SDLRWIoSeek,
                                    SDLRWIoTell,
                                    SDLRWIoLength,
                                    SDLRWIoDuplicate,
                                    0, /* flush */
                                    SDLRWIoDestroy};

static PHYSFS_Io *createSDLRWIo(const char *filename) {
  SDLRWIoContext *ctx;

  try {
    ctx = new SDLRWIoContext(filename);
  } catch (const Exception &e) {
    Debug() << "Failed mounting" << filename;
    return 0;
  }

  PHYSFS_Io *io = new PHYSFS_Io;
  *io = SDLRWIoTemplate;
  io->opaque = ctx;

  return io;
}

static inline PHYSFS_File *sdlPHYS(SDL_RWops *ops) {
  return static_cast<PHYSFS_File *>(ops->hidden.unknown.data1);
}

static Sint64 SDL_RWopsSize(SDL_RWops *ops) {
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return -1;

  return PHYSFS_fileLength(f);
}

static Sint64 SDL_RWopsSeek(SDL_RWops *ops, int64_t offset, int whence) {
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return -1;

  int64_t base;

  switch (whence) {
  default:
  case RW_SEEK_SET:
    base = 0;
    break;
  case RW_SEEK_CUR:
    base = PHYSFS_tell(f);
    break;
  case RW_SEEK_END:
    base = PHYSFS_fileLength(f);
    break;
  }

  int result = PHYSFS_seek(f, base + offset);

  return (result != 0) ? PHYSFS_tell(f) : -1;
}

static size_t SDL_RWopsRead(SDL_RWops *ops, void *buffer, size_t size,
                            size_t maxnum) {
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return 0;

  PHYSFS_sint64 result = PHYSFS_readBytes(f, buffer, size * maxnum);

  return (result != -1) ? (result / size) : 0;
}

static size_t SDL_RWopsWrite(SDL_RWops *ops, const void *buffer, size_t size,
                             size_t num) {
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return 0;

  PHYSFS_sint64 result = PHYSFS_writeBytes(f, buffer, size * num);

  return (result != -1) ? (result / size) : 0;
}

static int SDL_RWopsClose(SDL_RWops *ops) {
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return -1;

  int result = PHYSFS_close(f);
  ops->hidden.unknown.data1 = 0;

  return (result != 0) ? 0 : -1;
}

static int SDL_RWopsCloseFree(SDL_RWops *ops) {
  int result = SDL_RWopsClose(ops);

  SDL_FreeRW(ops);

  return result;
}

/* Copies the first srcN characters from src into dst,
 * or the full string if srcN == -1. Never writes more
 * than dstMax, and guarantees dst to be null terminated.
 * Returns copied bytes (minus terminating null) */
static size_t strcpySafe(char *dst, const char *src, size_t dstMax, int srcN) {
  if (srcN < 0)
    srcN = strlen(src);

  size_t cpyMax = std::min<size_t>(dstMax - 1, srcN);

  memcpy(dst, src, cpyMax);
  dst[cpyMax] = '\0';

  return cpyMax;
}

/* Attempt to locate an extension string in a filename.
 * Either a pointer into the input string pointing at the
 * extension, or null is returned */
static const char *findExt(const char *filename) {
  size_t len;

  for (len = strlen(filename); len > 0; --len) {
    if (filename[len] == '/')
      return 0;

    if (filename[len] == '.')
      return &filename[len + 1];
  }

  return 0;
}

static void initReadOps(PHYSFS_File *handle, SDL_RWops &ops, bool freeOnClose) {
  ops.size = SDL_RWopsSize;
  ops.seek = SDL_RWopsSeek;
  ops.read = SDL_RWopsRead;
  ops.write = SDL_RWopsWrite;

  if (freeOnClose)
    ops.close = SDL_RWopsCloseFree;
  else
    ops.close = SDL_RWopsClose;

  ops.type = SDL_RWOPS_PHYSFS;
  ops.hidden.unknown.data1 = handle;
}

static void strTolower(std::string &str) {
  for (size_t i = 0; i < str.size(); ++i)
    str[i] = static_cast<char>(tolower(static_cast<unsigned char>(str[i])));
}

const Uint32 SDL_RWOPS_PHYSFS = SDL_RWOPS_UNKNOWN + 10;

struct FileSystemPrivate {
  struct ResourceMount {
    std::string path;
    bool directory = false;
    BoostHash<std::string, std::string> casePaths;
  };

  /* Maps: lower case full filepath,
   * To:   mixed case full filepath */
  BoostHash<std::string, std::string> pathCache;
  /* Maps: lower case directory path,
   * To:   list of lower case filenames */
  BoostHash<std::string, std::vector<std::string>> fileLists;

  /* This is for compatibility with games that take Windows'
   * case insensitivity for granted */
  bool havePathCache;

  /* Per-mounted-directory ASCII case index. PHYSFS_enumerate merges mounts
   * before callback invocation, and can hide distinct names on lower-priority
   * mounts. Store every mount separately in PHYSFS search order. */
  std::vector<ResourceMount> resourceMounts;
};

static void throwPhysfsError(const char *desc) {
  PHYSFS_ErrorCode ec = PHYSFS_getLastErrorCode();
  const char *englishStr;
    if (ec == 0) {
        // Sometimes on Windows PHYSFS_init can return null
        // but the error code never changes
        englishStr = "unknown error";
    } else {
        englishStr = PHYSFS_getErrorByCode(ec);
    }

  throw Exception(Exception::PHYSFSError, "%s: %s", desc, englishStr);
}

FileSystem::FileSystem(const char *argv0, bool allowSymlinks) {
  if (PHYSFS_init(argv0) == 0)
    throwPhysfsError("Error initializing PhysFS");

  /* One error (=return 0) turns the whole product to 0 */

  int er = 1;

  er *= PHYSFS_registerArchiver(&RGSS1_Archiver);
  er *= PHYSFS_registerArchiver(&RGSS2_Archiver);
  er *= PHYSFS_registerArchiver(&RGSS3_Archiver);

  if (er == 0)
    throwPhysfsError("Error registering PhysFS RGSS archiver");

  p = new FileSystemPrivate;
  p->havePathCache = false;

  if (allowSymlinks)
    PHYSFS_permitSymbolicLinks(1);
}

FileSystem::~FileSystem() {
  delete p;

  if (PHYSFS_deinit() == 0)
    Debug() << "PhyFS failed to deinit.";
}

void FileSystem::addPath(const char *path, const char *mountpoint, bool reload) {
  /* Try the normal mount first */
    int state = PHYSFS_mount(path, mountpoint, 1);
  if (!state) {
    /* If it didn't work, try mounting via a wrapped
     * SDL_RWops */
    PHYSFS_Io *io = createSDLRWIo(path);

    if (io)
      state = PHYSFS_mountIo(io, path, 0, 1);
  }
    if (!state) {
        PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
        throw Exception(Exception::PHYSFSError, "Failed to mount %s (%s)", path, PHYSFS_getErrorByCode(err));
    }
    
    if (reload) reloadPathCache();
}

void FileSystem::removePath(const char *path, bool reload) {
    
    if (!PHYSFS_unmount(path)) {
        PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
        throw Exception(Exception::PHYSFSError, "Failed to unmount %s (%s)", path, PHYSFS_getErrorByCode(err));
    }
    
    if (reload) reloadPathCache();
}

struct CacheEnumData {
  FileSystemPrivate *p;
  std::stack<std::vector<std::string> *> fileLists;
  std::vector<std::string> mountOrder;

#ifdef __APPLE__
  iconv_t nfd2nfc;
  char buf[512];
#endif

  CacheEnumData(FileSystemPrivate *p) : p(p) {
    /* Preserve PhysFS search priority: game, then user RTP, then shared RTP. */
    char **paths = PHYSFS_getSearchPath();
    if (paths) {
      for (char **entry = paths; *entry; ++entry)
        mountOrder.push_back(*entry);
      PHYSFS_freeList(paths);
    }
#ifdef __APPLE__
    nfd2nfc = iconv_open("utf-8", "utf-8-mac");
#endif
  }

  ~CacheEnumData() {
#ifdef __APPLE__
    iconv_close(nfd2nfc);
#endif
  }

  size_t priority(const std::string &filename) const {
    const char *source = PHYSFS_getRealDir(filename.c_str());
    if (!source) return mountOrder.size();
    for (size_t i = 0; i < mountOrder.size(); ++i)
      if (mountOrder[i] == source) return i;
    return mountOrder.size();
  }

  /* Converts in-place */
  void toNFC(char *inout) {
#ifdef __APPLE__
    size_t srcSize = strlen(inout);
    size_t bufSize = sizeof(buf);
    char *bufPtr = buf;
    char *inoutPtr = inout;

    /* Reserve room for null terminator */
    --bufSize;

    iconv(nfd2nfc, &inoutPtr, &srcSize, &bufPtr, &bufSize);
    /* Null-terminate */
    *bufPtr = 0;
    strcpy(inout, buf);
#else
    (void)inout;
#endif
  }
};

static PHYSFS_EnumerateCallbackResult cacheEnumCB(void *d, const char *origdir,
                                                  const char *fname) {
  if (shState && shState->rtData().rqTerm)
    throw Exception(Exception::MKXPError, "Game close requested. Aborting path cache enumeration.");

  CacheEnumData &data = *static_cast<CacheEnumData *>(d);
  char fullPath[512];

  if (!*origdir)
    snprintf(fullPath, sizeof(fullPath), "%s", fname);
  else
    snprintf(fullPath, sizeof(fullPath), "%s/%s", origdir, fname);

  /* Deal with OSX' weird UTF-8 standards */
  data.toNFC(fullPath);

  std::string mixedCase(fullPath);
  std::string lowerCase = mixedCase;
  strTolower(lowerCase);

  PHYSFS_Stat stat;
  PHYSFS_stat(fullPath, &stat);

  if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
	/* Index directories too: Ruby's FileTest.directory? and read-only
	 * File.open under e.g. graphics/Pictures require corrected parents. */
	if (!data.p->pathCache.contains(lowerCase) ||
	    data.priority(mixedCase) < data.priority(data.p->pathCache[lowerCase]))
	  data.p->pathCache.insert(lowerCase, mixedCase);
    /* Create a new list for this directory */
    std::vector<std::string> &list = data.p->fileLists[lowerCase];

    /* Iterate over its contents */
    data.fileLists.push(&list);
    PHYSFS_enumerate(fullPath, cacheEnumCB, d);
    data.fileLists.pop();
  } else {
    /* Get the file list for the directory we're currently
     * traversing and append this filename to it */
    std::vector<std::string> &list = *data.fileLists.top();

    std::string lowerFilename(fname);
    strTolower(lowerFilename);
    list.push_back(lowerFilename);

    /* Add the lower -> mixed mapping of the file's full path */
    if (!data.p->pathCache.contains(lowerCase) ||
        data.priority(mixedCase) < data.priority(data.p->pathCache[lowerCase]))
      data.p->pathCache.insert(lowerCase, mixedCase);
  }

  return PHYSFS_ENUM_OK;
}

/* Enumerate each physical mount independently instead of trusting the
 * merged PHYSFS_enumerate result for case-colliding files. The resulting
 * paths are read-only; symlinks are ignored to avoid escaping a game root. */
static void indexResourceMountDirectory(FileSystemPrivate *state,
                                        FileSystemPrivate::ResourceMount &mount,
                                        const std::string &relative,
                                        unsigned depth, size_t &visited,
                                        std::map<std::string, std::set<std::string>> &knownFiles) {
  if (depth > 24 || visited >= 400000 ||
      (shState && shState->rtData().rqTerm)) return;
  const std::string fullDir = mount.path +
      (relative.empty() ? "" : "/" + relative);
  DIR *dir = opendir(fullDir.c_str());
  if (!dir) return;
  std::vector<std::string> names;
  for (struct dirent *item = readdir(dir); item; item = readdir(dir)) {
    if (strcmp(item->d_name, ".") && strcmp(item->d_name, ".."))
      names.push_back(item->d_name);
  }
  closedir(dir);
  std::sort(names.begin(), names.end());

  for (size_t i = 0; i < names.size() && visited < 400000; ++i) {
    const std::string &name = names[i];
    if (relative.empty()) {
      std::string rootName = name;
      strTolower(rootName);
      if (rootName != "graphics" && rootName != "audio" &&
          rootName != "data" && rootName != "fonts" &&
          rootName != "movies") continue;
    }
    const std::string path = relative.empty() ? name : relative + "/" + name;
    if (path.size() > 4096) continue;
    struct stat statData;
    if (lstat((mount.path + "/" + path).c_str(), &statData) != 0 ||
        (!S_ISDIR(statData.st_mode) && !S_ISREG(statData.st_mode)))
      continue;
    ++visited;
    std::string lowerPath = path;
    strTolower(lowerPath);
    if (!mount.casePaths.contains(lowerPath))
      mount.casePaths.insert(lowerPath, path);

    if (S_ISDIR(statData.st_mode)) {
      indexResourceMountDirectory(state, mount, path, depth + 1, visited,
                                  knownFiles);
    } else {
      const size_t lastSlash = lowerPath.find_last_of('/');
      const std::string dirKey = lastSlash == std::string::npos ? "" :
                                 lowerPath.substr(0, lastSlash);
      const std::string basename = lastSlash == std::string::npos ?
                                   lowerPath : lowerPath.substr(lastSlash + 1);
      /* PhysFS already indexed most filenames. Avoid doubling the directory
       * listing and repeatedly sorting identical filenames at load time. */
      if (knownFiles[dirKey].insert(basename).second)
        state->fileLists[dirKey].push_back(basename);
    }
  }
}

static size_t physfsMountPriority(const std::vector<FileSystemPrivate::ResourceMount> &mounts,
                                 const std::string &path) {
  const char *root = PHYSFS_getRealDir(path.c_str());
  if (!root) return mounts.size();
  for (size_t i = 0; i < mounts.size(); ++i)
    if (mounts[i].path == root) return i;
  return mounts.size();
}

static void createOrderedResourceMountCache(FileSystemPrivate *p) {
  p->resourceMounts.clear();
  std::map<std::string, std::set<std::string>> knownFiles;
  for (auto it = p->fileLists.cbegin(); it != p->fileLists.cend(); ++it)
    knownFiles[it->first].insert(it->second.begin(), it->second.end());
  char **paths = PHYSFS_getSearchPath();
  if (!paths) return;
  for (char **entry = paths; *entry; ++entry) {
    FileSystemPrivate::ResourceMount mount;
    mount.path = *entry;
    struct stat mounted;
    mount.directory = (stat(mount.path.c_str(), &mounted) == 0 &&
                       S_ISDIR(mounted.st_mode));
    if (mount.directory) {
      size_t visited = 0;
      indexResourceMountDirectory(p, mount, "", 0, visited, knownFiles);
    }
    p->resourceMounts.push_back(std::move(mount));
  }
  PHYSFS_freeList(paths);

  /* Keep RGSS archive matches supplied by PhysFS, but re-evaluate regular
   * files per mount in reverse order: game-local resources beat RTP files
   * regardless of casing. */
  for (size_t i = p->resourceMounts.size(); i > 0; --i) {
    const size_t priority = i - 1;
    const FileSystemPrivate::ResourceMount &mount = p->resourceMounts[priority];
    if (!mount.directory) continue;
    for (auto it = mount.casePaths.cbegin(); it != mount.casePaths.cend(); ++it) {
      const std::string &lower = it->first;
      if (p->pathCache.contains(lower)) {
        const size_t existing = physfsMountPriority(p->resourceMounts,
                                                   p->pathCache[lower]);
        if (existing <= priority) continue;
      }
      p->pathCache.insert(lower, it->second);
    }
  }
}

void FileSystem::createPathCache() {
  Debug() << "Loading path cache...";

  CacheEnumData data(p);
  data.fileLists.push(&p->fileLists[""]);
  PHYSFS_enumerate("", cacheEnumCB, &data);

  createOrderedResourceMountCache(p);

  p->havePathCache = true;

  Debug() << "Path cache completed.";
}

void FileSystem::reloadPathCache() {
    if (!p->havePathCache) return;
    
    p->fileLists.clear();
    p->pathCache.clear();
    p->resourceMounts.clear();
    createPathCache();
}

struct FontSetsCBData {
  FileSystemPrivate *p;
  SharedFontState *sfs;
};

static PHYSFS_EnumerateCallbackResult fontSetEnumCB(void *data, const char *dir,
                                                    const char *fname) {
  FontSetsCBData *d = static_cast<FontSetsCBData *>(data);

  /* Only consider filenames with font extensions */
  const char *ext = findExt(fname);

  if (!ext)
    return PHYSFS_ENUM_OK;

  char lowExt[8];
  size_t i;

  for (i = 0; i < sizeof(lowExt) - 1 && ext[i]; ++i)
    lowExt[i] = tolower(ext[i]);
  lowExt[i] = '\0';

  if (strcmp(lowExt, "ttf") && strcmp(lowExt, "otf"))
    return PHYSFS_ENUM_OK;

  char filename[512];
  snprintf(filename, sizeof(filename), "%s/%s", dir, fname);

  PHYSFS_File *handle = PHYSFS_openRead(filename);

  if (!handle)
    return PHYSFS_ENUM_ERROR;

  SDL_RWops ops;
  initReadOps(handle, ops, false);

  d->sfs->initFontSetCB(ops, filename);

  SDL_RWclose(&ops);

  return PHYSFS_ENUM_OK;
}

/* Basically just a case-insensitive search
 * for the folder "Fonts"... */
static PHYSFS_EnumerateCallbackResult
findFontsFolderCB(void *data, const char *, const char *fname) {
  size_t i = 0;
  char buffer[512];
  const char *s = fname;

  while (*s && i < sizeof(buffer))
    buffer[i++] = tolower(*s++);

  buffer[i] = '\0';

  if (strcmp(buffer, "fonts") == 0)
    PHYSFS_enumerate(fname, fontSetEnumCB, data);

  return PHYSFS_ENUM_OK;
}

void FileSystem::initFontSets(SharedFontState &sfs) {
  FontSetsCBData d = {p, &sfs};

  PHYSFS_enumerate("", findFontsFolderCB, &d);
}

struct OpenReadEnumData {
  FileSystem::OpenHandler &handler;
  SDL_RWops ops;

  /* The filename (without directory) we're looking for */
  const char *filename;
  size_t filenameN;

  /* Optional hash to translate full filepaths
   * (used with path cache) */
  BoostHash<std::string, std::string> *pathTrans;

  /* Number of files we've attempted to read and parse */
  size_t matchCount;
  bool stopSearching;

  /* In case of a PhysFS error, save it here so it
   * doesn't get changed before we get back into our code */
  const char *physfsError;

  OpenReadEnumData(FileSystem::OpenHandler &handler, const char *filename,
                   size_t filenameN,
                   BoostHash<std::string, std::string> *pathTrans)
      : handler(handler), filename(filename), filenameN(filenameN),
        pathTrans(pathTrans), matchCount(0), stopSearching(false),
        physfsError(0) {}
};

static PHYSFS_EnumerateCallbackResult
openReadEnumCB(void *d, const char *dirpath, const char *filename) {
  OpenReadEnumData &data = *static_cast<OpenReadEnumData *>(d);
  char buffer[512];
  const char *fullPath;

  if (data.stopSearching)
    return PHYSFS_ENUM_STOP;

  /* If there's not even a partial match, continue searching */
  if (strncmp(filename, data.filename, data.filenameN) != 0)
    return PHYSFS_ENUM_OK;

  if (!*dirpath) {
    fullPath = filename;
  } else {
    snprintf(buffer, sizeof(buffer), "%s/%s", dirpath, filename);
    fullPath = buffer;
  }

  char last = filename[data.filenameN];
  /* If fname matches up to a following '.' (meaning the rest is part
   * of the extension), or up to a following '\0' (full match), we've
   * found our file */
  if (last != '.' && last != '\0')
    return PHYSFS_ENUM_OK;

  /* If the path cache is active, translate from lower case
   * to mixed case path */
  if (data.pathTrans)
    fullPath = (*data.pathTrans)[fullPath].c_str();

  PHYSFS_File *phys = PHYSFS_openRead(fullPath);

  if (!phys) {
    /* Failing to open this file here means there must
     * be a deeper rooted problem somewhere within PhysFS.
     * Just abort alltogether. */
    data.stopSearching = true;
    data.physfsError = PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());

    return PHYSFS_ENUM_ERROR;
  }
  initReadOps(phys, data.ops, false);

  const char *ext = findExt(filename);

  if (data.handler.tryRead(data.ops, ext))
    data.stopSearching = true;

  ++data.matchCount;
  return PHYSFS_ENUM_OK;
}

void FileSystem::openRead(OpenHandler &handler, const char *filename) {
  std::string filename_nm = normalize(filename, false, false);
  char buffer[512];
  size_t len = strcpySafe(buffer, filename_nm.c_str(), sizeof(buffer), -1);
  char *delim;

  if (p->havePathCache)
    for (size_t i = 0; i < len; ++i)
      buffer[i] = tolower(buffer[i]);

  /* Find the deliminator separating directory and file name */
  for (delim = buffer + len; delim > buffer; --delim)
    if (*delim == '/')
      break;

  const bool root = (delim == buffer);

  const char *file = buffer;
  const char *dir = "";

  if (!root) {
    /* Cut the buffer in half so we can use it
     * for both filename and directory path */
    *delim = '\0';
    file = delim + 1;
    dir = buffer;
  }
  OpenReadEnumData data(handler, file, len + buffer - delim - !root,
                        p->havePathCache ? &p->pathCache : 0);

  if (p->havePathCache) {
    /* Get the list of files contained in this directory
     * and manually iterate over them */
    const std::vector<std::string> &fileList = p->fileLists[dir];

    std::vector<std::string> ordered(fileList);
    std::stable_sort(ordered.begin(), ordered.end(), [&handler](const std::string &a, const std::string &b) {
      return handler.extensionPriority(findExt(a.c_str())) > handler.extensionPriority(findExt(b.c_str()));
    });
    for (size_t i = 0; i < ordered.size(); ++i)
      openReadEnumCB(&data, dir, ordered[i].c_str());
  } else {
    PHYSFS_enumerate(dir, openReadEnumCB, &data);
  }

  if (data.physfsError)
    throw Exception(Exception::PHYSFSError, "PhysFS: %s", data.physfsError);

  if (data.matchCount == 0)
    throw Exception(Exception::NoFileError, "%s", filename);
}

void FileSystem::openReadRaw(SDL_RWops &ops, const char *filename,
                             bool freeOnClose) {

  std::string path = normalize(filename, false, false);
  std::string resolved;
  if (findResource(filename, resolved, true))
    path = resolved;
  PHYSFS_File *handle = PHYSFS_openRead(path.c_str());

  if (!handle)
    throw Exception(Exception::NoFileError, "%s", filename);

  initReadOps(handle, ops, freeOnClose);
    return;
}

std::string FileSystem::normalize(const char *pathname, bool preferred,
                            bool absolute) {
    return filesystemImpl::normalizePath(pathname, preferred, absolute);
}

bool FileSystem::exists(const char *filename) {
  const std::string path = normalize(filename, false, false);
  if (PHYSFS_exists(path.c_str()))
    return true;
  return resourceExists(filename);
}

/* Lowercase only ASCII bytes. UTF-8 game filenames stay byte-for-byte
 * unchanged outside ASCII; never pass negative signed chars to tolower(). */
static std::string resourceLower(std::string text) {
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c >= 'A' && c <= 'Z') text[i] = static_cast<char>(c + ('a' - 'A'));
  }
  return text;
}

/* Allow relative virtual assets only; NEVER normalize ../ into an allowed
 * path or redirect a save/config/system file through the RTP mounts. */
static std::string safeGameResourcePath(const char *filename) {
  if (!filename) return std::string();
  std::string path(filename);
  if (path.empty() || path.size() > 4096 || path.find(':') != std::string::npos)
    return std::string();
  std::replace(path.begin(), path.end(), '\\', '/');
  while (path.compare(0, 2, "./") == 0) path.erase(0, 2);
  if (path.empty() || path[0] == '/') return std::string();
  size_t offset = 0;
  std::string root;
  while (offset < path.size()) {
    const size_t slash = path.find('/', offset);
    const std::string part = path.substr(offset, slash == std::string::npos
                                        ? std::string::npos : slash - offset);
    if (part.empty() || part == "." || part == "..")
      return std::string();
    if (offset == 0) root = resourceLower(part);
    if (slash == std::string::npos) break;
    offset = slash + 1;
  }
  if (root != "graphics" && root != "audio" && root != "fonts" &&
      root != "movies" && root != "data")
    return std::string();
  return path;
}

bool FileSystem::findResource(const char *filename, std::string &virtualPath,
                              bool fileOnly) {
  virtualPath.clear();
  const std::string requested = safeGameResourcePath(filename);
  if (requested.empty()) return false;

  PHYSFS_Stat stat;
  const bool exactFound = PHYSFS_stat(requested.c_str(), &stat) &&
      (!fileOnly || stat.filetype == PHYSFS_FILETYPE_REGULAR);
  if (!p->havePathCache)
    return exactFound ? (virtualPath = requested, true) : false;

  const std::string lower = resourceLower(requested);
  if (p->pathCache.contains(lower)) {
    const std::string &actual = p->pathCache[lower];
    if (PHYSFS_stat(actual.c_str(), &stat) &&
        (!fileOnly || stat.filetype == PHYSFS_FILETYPE_REGULAR)) {
      /* The cache uses the same ordered game/RTP virtual namespace as the
       * normal Bitmap/Audio loader. If an exact-case spelling exists only
       * in a LOWER priority RTP while the game has a differently cased
       * file, the game's file must win; don't blindly prefer that exact
       * spelling. Within one mount, exact-case files still win. */
      if (exactFound) {
        const char *requestedMount = PHYSFS_getRealDir(requested.c_str());
        const std::string exactRoot = requestedMount ? requestedMount : "";
        const char *cachedMount = PHYSFS_getRealDir(actual.c_str());
        const std::string cacheRoot = cachedMount ? cachedMount : "";
        if (!exactRoot.empty() && exactRoot == cacheRoot) {
          virtualPath = requested;
          return true;
        }
      }
      virtualPath = actual;
      return true;
    }
  }
  if (exactFound) {
    virtualPath = requested;
    return true;
  }
  return false;
}

bool FileSystem::resourceExists(const char *filename, bool fileOnly) {
  std::string virtualPath;
  return findResource(filename, virtualPath, fileOnly);
}

bool FileSystem::resourceDirectory(const char *filename) {
  std::string virtualPath;
  if (!findResource(filename, virtualPath, false)) return false;
  PHYSFS_Stat stat;
  return PHYSFS_stat(virtualPath.c_str(), &stat) &&
         stat.filetype == PHYSFS_FILETYPE_DIRECTORY;
}

static PHYSFS_EnumerateCallbackResult resourceEntriesEnum(void *opaque,
                                                            const char *,
                                                            const char *name) {
  std::vector<std::string> &items = *static_cast<std::vector<std::string> *>(opaque);
  if (std::find(items.begin(), items.end(), name) == items.end())
    items.push_back(name);
  return PHYSFS_ENUM_OK;
}

bool FileSystem::resourceEntries(const char *filename,
                                 std::vector<std::string> &entries) {
  entries.clear();
  std::string virtualDir;
  if (!findResource(filename, virtualDir, false)) return false;
  PHYSFS_Stat stat;
  if (!PHYSFS_stat(virtualDir.c_str(), &stat) ||
      stat.filetype != PHYSFS_FILETYPE_DIRECTORY)
    return false;

  entries.push_back(".");
  entries.push_back("..");
  PHYSFS_enumerate(virtualDir.c_str(), resourceEntriesEnum, &entries);

  /* The regular PhysFS enumerator can miss files in a lower-priority RTP
   * directory whose parent differs ONLY by case from the game directory.
   * fileLists is the unified lowercase mount index already used by the
   * image/audio loader, so include its names as well. */
  const std::string lowerDir = resourceLower(virtualDir);
  if (p->havePathCache && p->fileLists.contains(lowerDir)) {
    const std::vector<std::string> &files = p->fileLists[lowerDir];
    for (size_t i = 0; i < files.size(); ++i) {
      const std::string lowerFull = lowerDir + "/" + files[i];
      if (!p->pathCache.contains(lowerFull)) continue;
      const std::string &actual = p->pathCache[lowerFull];
      const size_t slash = actual.find_last_of('/');
      const std::string actualName = slash == std::string::npos
                                     ? actual : actual.substr(slash + 1);
      if (std::find(entries.begin(), entries.end(), actualName) == entries.end())
        entries.push_back(actualName);
    }
  }
  return true;
}

bool FileSystem::resourceDiskPath(const char *filename, std::string &diskPath,
                                  bool directoryOnly) {
  diskPath.clear();
  std::string virtualPath;
  if (!findResource(filename, virtualPath, !directoryOnly)) return false;

  PHYSFS_Stat stat;
  if (!PHYSFS_stat(virtualPath.c_str(), &stat) ||
      (directoryOnly && stat.filetype != PHYSFS_FILETYPE_DIRECTORY))
    return false;

  /* PhysFS may find resources inside packed RGSS archives; File.open can
   * only use disk files. Never hand out an archive's pseudo-path. */
  const char *mount = PHYSFS_getRealDir(virtualPath.c_str());
  if (!mount) return false;
  const std::string mountRoot = normalize(mount, false, true);
  const std::string physical = mountRoot + "/" + virtualPath;
  struct stat physicalStat;
  if (::stat(physical.c_str(), &physicalStat) != 0 ||
      (directoryOnly ? !S_ISDIR(physicalStat.st_mode)
                     : !S_ISREG(physicalStat.st_mode)))
    return false;

  /* Standard Ruby File.open receives real paths, unlike the virtual
   * PhysFS stream reader. Refuse any intermediate symlink that escapes
   * the owning mounted directory (such as Graphics/Pictures -> /etc). */
  char *rootReal = realpath(mountRoot.c_str(), nullptr);
  char *fileReal = realpath(physical.c_str(), nullptr);
  if (!rootReal || !fileReal) {
    free(rootReal);
    free(fileReal);
    return false;
  }
  std::string rootPrefix(rootReal);
  const std::string canonicalFile(fileReal);
  free(rootReal);
  free(fileReal);
  if (rootPrefix.empty() || rootPrefix.back() != '/') rootPrefix += '/';
  if (canonicalFile.compare(0, rootPrefix.size(), rootPrefix) != 0)
    return false;
  diskPath = physical;
  return true;
}

const char *FileSystem::desensitize(const char *filename) {
  std::string fn_lower = resourceLower(normalize(filename, false, false));
  if (p->havePathCache && p->pathCache.contains(fn_lower))
    return p->pathCache[fn_lower].c_str();
  return filename;
}
