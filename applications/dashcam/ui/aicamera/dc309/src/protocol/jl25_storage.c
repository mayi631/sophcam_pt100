/*
 * JL25xx 协议 /Storage 命令组（协议 2.3 节）
 *
 *   /Storage?GetDirFileInfo                    - 文件数量统计
 *   /Storage?GetSD_Info                        - SD 卡容量
 *   /Storage?GetFileTHumb=fid&width=160        - 缩略图（JPEG 二进制）
 *   /Storage?Download=fid[@offset]             - 文件下载（支持断点续传）
 *   /Storage?fid1,fid2,...                     - 删除文件
 *   /Storage?GetFileList                       - 全量文件列表（JSON）
 *   /Storage?GetFilePage=n&type=t&numPerPage=m - 分页文件列表（JSON）
 *
 * fid 映射规则：fid 即设备真实文件名（与相册 page_album.c 一致），
 * 照片形如 "DSCF00001.jpg"，视频形如 "DSCF00001.mov"。
 * 设备实际存储：照片在 DCIM/PHOTO 下，视频在 DCIM/MOVIE 下，
 * 查找时按目录探测。缩略图在 .thumb/PHOTO_SMALL|MOVIE_SMALL 下
 * （视频缩略图与原文件同名，扩展名为 .jpg）。
 */
#define DEBUG
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "jl25_internal.h"
#include "jl25_protocol.h"
#include "mlog.h"
#include "mode.h"

#ifdef COMPONENTS_THUMBNAIL_EXTRACTOR_ON
#include "thumbnail_extractor.h"
#endif

#define JL25_SD_ROOT "/mnt/sd"
#define JL25_PHOTO_DIR JL25_SD_ROOT "/DCIM/PHOTO"
#define JL25_MOVIE_DIR JL25_SD_ROOT "/DCIM/MOVIE"
#define JL25_THUMB_S_DIR JL25_SD_ROOT "/.thumb/PHOTO_SMALL"
#define JL25_THUMB_MOV_S JL25_SD_ROOT "/.thumb/MOVIE_SMALL"

/* ==================== fid 与路径转换 ==================== */

/* 按扩展名判断文件类型（协议 t 字段） */
static int jl25_file_type(const char* name)
{
    const char* ext = strrchr(name, '.');
    if (ext == NULL) {
        return -1;
    }
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        return JL25_FILETYPE_JPG;
    }
    if (strcasecmp(ext, ".mp4") == 0) {
        return JL25_FILETYPE_MP4;
    }
    if (strcasecmp(ext, ".avi") == 0) {
        return JL25_FILETYPE_AVI;
    }
    if (strcasecmp(ext, ".mov") == 0) {
        return JL25_FILETYPE_MP4; /* 设备录像为 mov，协议按视频处理 */
    }
    if (strcasecmp(ext, ".wav") == 0) {
        return JL25_FILETYPE_WAV;
    }
    return -1;
}

/* fid -> 实际路径。fid 即设备真实文件名（如 "DSCF00001.jpg"）。
 * 照片在 PHOTO 目录、视频在 MOVIE 目录，按扩展名先猜目录再探测。
 * 返回 0 成功。
 */
static int jl25_fid_to_path(const char* fid, char* path, size_t path_sz)
{
    int type = jl25_file_type(fid);
    if (type == JL25_FILETYPE_JPG) {
        snprintf(path, path_sz, "%s/%s", JL25_PHOTO_DIR, fid);
        return 0;
    }
    if (type == JL25_FILETYPE_MP4) {
        snprintf(path, path_sz, "%s/%s", JL25_MOVIE_DIR, fid);
        return 0;
    }
    /* 未知类型：先 PHOTO 后 MOVIE 探测 */
    snprintf(path, path_sz, "%s/%s", JL25_PHOTO_DIR, fid);
    if (access(path, F_OK) == 0) {
        return 0;
    }
    snprintf(path, path_sz, "%s/%s", JL25_MOVIE_DIR, fid);
    if (access(path, F_OK) == 0) {
        return 0;
    }
    return -1;
}

/* 实际路径 -> fid（即文件名本身） */
static void jl25_path_to_fid(const char* path, char* fid, size_t fid_sz)
{
    const char* name = strrchr(path, '/');
    snprintf(fid, fid_sz, "%s", (name != NULL) ? name + 1 : path);
}

/* DOS FAT 时间（协议 3.3 节） */
static uint32_t jl25_dos_datetime(time_t t)
{
    struct tm tmv;
    localtime_r(&t, &tmv);
    uint32_t date = ((tmv.tm_year - 80) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday;
    uint32_t time = (tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2);
    return ((date & 0xFFFF) << 16) | (time & 0xFFFF);
}

/* ==================== 文件枚举 ==================== */

typedef struct {
    char name[128];
    char fid[160];
    int type;
    uint32_t size;
    uint32_t dt; /* DOS datetime */
    time_t mtime;
} jl25_fileinfo_t;

/* 扫描目录，收集 type_filter 过滤后的文件（type_filter<0 不过滤）。
 * 返回文件数量；files 数组由调用方提供（可为 NULL，仅计数）。
 */
static int jl25_scan_files(const char* dir, int type_filter,
    jl25_fileinfo_t* files, int max_cnt)
{
    DIR* d = opendir(dir);
    if (d == NULL) {
        return 0;
    }
    struct dirent* ent;
    int cnt = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        char path[256];
        struct stat sb;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &sb) != 0 || !S_ISREG(sb.st_mode)) {
            continue;
        }
        int type = jl25_file_type(ent->d_name);
        if (type < 0) {
            continue;
        }
        if (type_filter >= 0 && type != type_filter) {
            continue;
        }
        if (files != NULL && cnt < max_cnt) {
            jl25_fileinfo_t* f = &files[cnt];
            snprintf(f->name, sizeof(f->name), "%s", ent->d_name);
            jl25_path_to_fid(path, f->fid, sizeof(f->fid));
            f->type = type;
            f->size = (uint32_t)sb.st_size;
            f->mtime = sb.st_mtime;
            f->dt = jl25_dos_datetime(sb.st_mtime);
        }
        cnt++;
    }
    closedir(d);
    return cnt;
}

/* mtime 升序比较 */
static int jl25_fileinfo_cmp(const void* a, const void* b)
{
    const jl25_fileinfo_t* fa = (const jl25_fileinfo_t*)a;
    const jl25_fileinfo_t* fb = (const jl25_fileinfo_t*)b;
    if (fa->mtime < fb->mtime) {
        return -1;
    }
    if (fa->mtime > fb->mtime) {
        return 1;
    }
    return strcmp(fa->name, fb->name);
}

/* 扫描全部媒体文件（PHOTO + MOVIE），按时间排序。
 * files 需由调用方分配，容量 jl25_max_files；返回总数。
 */
#define JL25_MAX_FILE_LIST 512
static int jl25_scan_all(int type_filter, jl25_fileinfo_t* files, int max_cnt)
{
    int total = 0;
    total += jl25_scan_files(JL25_PHOTO_DIR, type_filter,
        files != NULL ? files + total : NULL,
        max_cnt - total);
    total += jl25_scan_files(JL25_MOVIE_DIR, type_filter,
        files != NULL ? files + total : NULL,
        max_cnt - total);
    if (files != NULL && total > 1) {
        qsort(files, total, sizeof(jl25_fileinfo_t), jl25_fileinfo_cmp);
    }
    return total;
}

/* ==================== JSON 转义（文件名不含特殊字符，仅防御） ==================== */

static void jl25_json_escape(const char* in, char* out, size_t out_sz)
{
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 2 < out_sz; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

/* ==================== 命令实现 ==================== */

/* GetDirFileInfo：文件数量统计（协议 2.3.1） */
static void jl25_stor_getdirfileinfo(int fd)
{
    char resp[256];
    int n_photo = jl25_scan_files(JL25_PHOTO_DIR, -1, NULL, 0);
    int n_movie = jl25_scan_files(JL25_MOVIE_DIR, -1, NULL, 0);
    int n_jpg = jl25_scan_files(JL25_PHOTO_DIR, JL25_FILETYPE_JPG, NULL, 0);
    int n_mp4 = jl25_scan_files(JL25_MOVIE_DIR, JL25_FILETYPE_MP4, NULL, 0);

    snprintf(resp, sizeof(resp),
        "{\"Result\":0,"
        "\"NumberOfDirs\":2,"
        "\"NumberOfFiles\":%d,"
        "\"NumberOfJPG\":%d,"
        "\"NumberOfMP4\":%d}",
        n_photo + n_movie, n_jpg, n_mp4);
    jl25_http_send_json(fd, resp);
}

/* GetSD_Info：SD 卡容量（协议 2.3.2，单位：512 字节扇区） */
static void jl25_stor_getsdinfo(int fd)
{
    char resp[192];
    struct statvfs vfs;
    if (statvfs(JL25_SD_ROOT, &vfs) != 0) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }
    uint64_t total = (uint64_t)vfs.f_blocks * vfs.f_frsize / 512;
    uint64_t freeb = (uint64_t)vfs.f_bavail * vfs.f_frsize / 512;
    snprintf(resp, sizeof(resp),
        "{\"Result\":0,\"TotalSize\":%llu,\"FreeSize\":%llu}",
        (unsigned long long)total, (unsigned long long)freeb);
    jl25_http_send_json(fd, resp);
}

static int jl25_storage_jpeg_valid(const char* path);

static int jl25_storage_extract_thumb(const char* source_path,
    const char* thumb_path)
{
#ifdef COMPONENTS_THUMBNAIL_EXTRACTOR_ON
    THUMBNAIL_EXTRACTOR_HANDLE_T extractor = NULL;
    THUMBNAIL_PACKET_S packet = { 0 };
    char tmp_path[320];
    FILE* fp;
    int ret = -1;

    if (mkdir(JL25_SD_ROOT "/.thumb", 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    if (mkdir(JL25_THUMB_S_DIR, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    if (mkdir(JL25_THUMB_MOV_S, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    if (THUMBNAIL_EXTRACTOR_Create(&extractor) != 0) {
        return -1;
    }

    if (THUMBNAIL_EXTRACTOR_GetThumbnailByType(extractor, source_path,
            &packet, 0)
            == 0
        && packet.data != NULL && packet.size > 0) {
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", thumb_path);
        fp = fopen(tmp_path, "wb");
        if (fp != NULL) {
            size_t written = fwrite(packet.data, 1, packet.size, fp);
            int close_ret = fclose(fp);
            fp = NULL;
            if (written == (size_t)packet.size && close_ret == 0 && jl25_storage_jpeg_valid(tmp_path) && rename(tmp_path, thumb_path) == 0) {
                ret = 0;
            } else {
                unlink(tmp_path);
            }
        }
    }

    THUMBNAIL_EXTRACTOR_ClearPacket(&packet);
    THUMBNAIL_EXTRACTOR_Destroy(&extractor);
    return ret;
#else
    (void)source_path;
    (void)thumb_path;
    return -1;
#endif
}

static int jl25_storage_jpeg_valid(const char* path)
{
    FILE* fp;
    struct stat sb;
    unsigned char head[2];
    unsigned char tail[2];

    if (stat(path, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_size < 4) {
        return 0;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fread(head, 1, sizeof(head), fp) != sizeof(head) || fseeko(fp, -2, SEEK_END) != 0 || fread(tail, 1, sizeof(tail), fp) != sizeof(tail)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return head[0] == 0xff && head[1] == 0xd8 && tail[0] == 0xff && tail[1] == 0xd9;
}

/*
 * Send a media file without loading it into a fixed-size buffer.  The old
 * thumbnail path used fread(..., 512 KB), which truncated high-resolution
 * photos when the small thumbnail had not been generated yet.
 */
static int jl25_storage_send_file_locked(int fd, const char* path,
    const char* content_type, const char* fid)
{
    FILE* fp;
    struct stat sb;
    char header[512];
    char* buf;
    uint64_t remaining;

    fp = fopen(path, "rb");
    if (fp == NULL || fstat(fileno(fp), &sb) != 0 || !S_ISREG(sb.st_mode)) {
        if (fp != NULL) {
            fclose(fp);
        }
        return -1;
    }
    if (sb.st_size <= 0) {
        fclose(fp);
        return -1;
    }

    buf = (char*)malloc(JL25_FILE_CHUNK_SIZE);
    if (buf == NULL) {
        fclose(fp);
        return -1;
    }

    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Server: SOPHCAM-1.0\r\n"
        "Connection: close\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %llu\r\n"
        "\r\n",
        content_type, (unsigned long long)sb.st_size);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header) || jl25_http_send_all(fd, header, (size_t)header_len) != 0) {
        free(buf);
        fclose(fp);
        return -1;
    }

    remaining = (uint64_t)sb.st_size;
    while (remaining > 0) {
        size_t to_read = remaining > JL25_FILE_CHUNK_SIZE
            ? JL25_FILE_CHUNK_SIZE
            : (size_t)remaining;
        size_t n = fread(buf, 1, to_read, fp);
        if (n == 0 || jl25_http_send_all(fd, buf, n) != 0) {
            MLOG_ERR("jl25: getthumb send failed, fid=[%s] path=[%s]\n",
                fid, path);
            free(buf);
            fclose(fp);
            return -1;
        }
        remaining -= n;
    }
    free(buf);
    fclose(fp);
    return 0;
}

static void jl25_storage_send_thumb_fid_locked(const char* fid, int fd)
{
    char thumb_path[256];
    char path[256];
    char basenoext[128];
    char* dot;

    if (fid == NULL || fid[0] == '\0' || strchr(fid, '/') != NULL || strchr(fid, '\\') != NULL || strstr(fid, "..") != NULL || jl25_fid_to_path(fid, path, sizeof(path)) != 0) {
        MLOG_ERR("jl25: getthumb fid_to_path fail, fid=[%s]\n",
            fid != NULL ? fid : "");
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", 15);
        return;
    }

    /* 从 fid 找到对应的已提取缩略图（与原文件同名，视频缩略图扩展名为 .jpg） */
    snprintf(basenoext, sizeof(basenoext), "%s", fid);
    dot = strrchr(basenoext, '.');
    if (dot != NULL) {
        *dot = '\0';
    }

    if (jl25_file_type(fid) == JL25_FILETYPE_JPG) {
        snprintf(thumb_path, sizeof(thumb_path), "%s/%s.jpg",
            JL25_THUMB_S_DIR, basenoext);
    } else {
        snprintf(thumb_path, sizeof(thumb_path), "%s/%s.jpg",
            JL25_THUMB_MOV_S, basenoext);
    }

    MLOG_INFO("jl25: getthumb request fid=[%s] source=[%s] thumb=[%s]\n",
        fid, path, thumb_path);

    /*
     * A remotely captured photo can be requested before the UI has created
     * its .thumb file.  Generate the APP0 thumbnail on demand first.
     */
    if (access(thumb_path, F_OK) == 0 && !jl25_storage_jpeg_valid(thumb_path)) {
        MLOG_INFO("jl25: getthumb invalid thumbnail removed, fid=[%s]\n", fid);
        unlink(thumb_path);
    }
    if (!jl25_storage_jpeg_valid(thumb_path) && jl25_storage_extract_thumb(path, thumb_path) == 0) {
        MLOG_INFO("jl25: getthumb generated thumbnail, fid=[%s]\n", fid);
    }

    if (jl25_storage_jpeg_valid(thumb_path) && jl25_storage_send_file_locked(fd, thumb_path, "image/jpeg", fid) == 0) {
        return;
    }

    /*
     * Last-resort compatibility fallback for photos: send the complete JPEG,
     * never a video file mislabeled as image/jpeg.
     */
    if (jl25_file_type(fid) == JL25_FILETYPE_JPG && jl25_storage_jpeg_valid(path) && jl25_storage_send_file_locked(fd, path, "image/jpeg", fid) == 0) {
        MLOG_INFO("jl25: getthumb fallback original, fid=[%s]\n", fid);
        return;
    }

    {
        MLOG_ERR("jl25: getthumb open fail, thumb=[%s] orig=[%s] errno=%d\n",
            thumb_path, path, errno);
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", 15);
    }
}

void jl25_storage_send_thumb_fid(const char* fid, int fd)
{
    if (jl25_storage_access_begin() != 0) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }
    jl25_storage_send_thumb_fid_locked(fid, fd);
    jl25_storage_access_end();
}

/* GetFileTHumb：缩略图（协议 2.3.3）
 * 优先返回已提取的缩略图（.thumb 目录），无则返回原图（照片小图场景可接受）。
 */
static void jl25_stor_getthumb(int fd, const char* query)
{
    char fid[160];

    if (jl25_query_get(query, "GetFileTHumb", fid, sizeof(fid)) == NULL && jl25_query_get(query, "GetFilethumb", fid, sizeof(fid)) == NULL) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }
    jl25_storage_send_thumb_fid_locked(fid, fd);
}

/* Download：文件下载，支持 @offset 断点续传与多文件顺序下载（协议 2.3.4） */
static const char* jl25_request_range(const char* request)
{
    const char* p = request;

    if (request == NULL) {
        return NULL;
    }
    while ((p = strchr(p, '\n')) != NULL) {
        p++;
        if (strncasecmp(p, "Range:", 6) == 0) {
            return p + 6;
        }
    }
    return NULL;
}

/* Parse one HTTP byte range. Return 1 for valid, 0 for absent, -1 invalid. */
static int jl25_parse_range(const char* request, uint64_t total,
    uint64_t* start, uint64_t* end)
{
    const char* value = jl25_request_range(request);
    char* ep;
    unsigned long long first;
    unsigned long long last;

    if (value == NULL) {
        return 0;
    }
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    if (strncasecmp(value, "bytes=", 6) != 0) {
        return -1;
    }
    value += 6;
    first = strtoull(value, &ep, 10);
    if (ep == value || *ep != '-') {
        return -1;
    }
    if (first >= total) {
        return -1;
    }
    value = ep + 1;
    if (*value == '\r' || *value == '\n' || *value == '\0') {
        last = total - 1;
    } else {
        last = strtoull(value, &ep, 10);
        if (ep == value || last < first) {
            return -1;
        }
        if (last >= total) {
            last = total - 1;
        }
    }
    *start = first;
    *end = last;
    return 1;
}

static const char* jl25_download_content_type(const char* fid)
{
    const char* ext = strrchr(fid, '.');

    if (ext != NULL && strcasecmp(ext, ".jpg") == 0) {
        return "image/jpeg";
    }
    if (ext != NULL && (strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".mp4") == 0)) {
        return "video/quicktime";
    }
    return "application/octet-stream";
}

static void jl25_storage_download_fid_locked(const char* fid, uint64_t offset,
    const char* request, int fd)
{
    char path[256];
    struct stat sb;
    FILE* fp;
    uint64_t total;
    uint64_t start = offset;
    uint64_t end;
    int range_ret;
    int status = 200;
    const char* status_text = "OK";
    char header[512];
    int header_len;
    char* buf;

    if (fid == NULL || fid[0] == '\0' || strchr(fid, '/') != NULL || strchr(fid, '\\') != NULL || strstr(fid, "..") != NULL) {
        jl25_http_send_response(fd, 400, "Bad Request", "text/html",
            "{\"Result\":-1}", 13);
        return;
    }
    if (jl25_fid_to_path(fid, path, sizeof(path)) != 0) {
        MLOG_ERR("jl25: download fid_to_path fail, fid=[%s]\n", fid);
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", 15);
        return;
    }
    fp = fopen(path, "rb");
    if (fp == NULL || fstat(fileno(fp), &sb) != 0 || !S_ISREG(sb.st_mode)) {
        int saved_errno = errno;
        if (fp != NULL) {
            fclose(fp);
        }
        MLOG_ERR("jl25: download open fail, path=[%s] errno=%d\n",
            path, saved_errno);
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", 15);
        return;
    }
    total = (uint64_t)sb.st_size;
    if (total == 0 || start >= total) {
        fclose(fp);
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }
    end = total - 1;

    /* HTTP Range takes precedence for normal media URLs. */
    range_ret = jl25_parse_range(request, total, &start, &end);
    if (range_ret < 0 || start >= total || end < start) {
        char range_header[128];
        fclose(fp);
        snprintf(range_header, sizeof(range_header),
            "HTTP/1.1 416 Range Not Satisfiable\r\n"
            "Server: SOPHCAM-1.0\r\nConnection: close\r\n"
            "Content-Range: bytes */%llu\r\n"
            "Content-Length: 0\r\n\r\n",
            (unsigned long long)total);
        jl25_http_send_all(fd, range_header, strlen(range_header));
        return;
    }
    if (range_ret == 1) {
        status = 206;
        status_text = "Partial Content";
    } else if (offset > 0) {
        end = total - 1;
    }

    if (fseeko(fp, (off_t)start, SEEK_SET) != 0) {
        fclose(fp);
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }
    MLOG_INFO("jl25: download fid=[%s] path=[%s] range=%llu-%llu/%llu\n",
        fid, path, (unsigned long long)start,
        (unsigned long long)end, (unsigned long long)total);

    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Server: SOPHCAM-1.0\r\n"
        "Connection: close\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Type: %s\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Content-Length: %llu\r\n",
        status, status_text, jl25_download_content_type(fid),
        fid, (unsigned long long)(end - start + 1));
    if (status == 206) {
        header_len += snprintf(header + header_len,
            sizeof(header) - (size_t)header_len,
            "Content-Range: bytes %llu-%llu/%llu\r\n",
            (unsigned long long)start,
            (unsigned long long)end,
            (unsigned long long)total);
    }
    header_len += snprintf(header + header_len,
        sizeof(header) - (size_t)header_len, "\r\n");
    if (jl25_http_send_all(fd, header, (size_t)header_len) != 0) {
        fclose(fp);
        return;
    }

    buf = (char*)malloc(JL25_FILE_CHUNK_SIZE);
    if (buf == NULL) {
        fclose(fp);
        return;
    }
    uint64_t remaining = end - start + 1;
    uint64_t sent_bytes = 0;
    uint64_t reported_bytes = 0;
    while (remaining > 0) {
        size_t to_read = remaining > JL25_FILE_CHUNK_SIZE ? JL25_FILE_CHUNK_SIZE : (size_t)remaining;
        size_t n = fread(buf, 1, to_read, fp);
        if (n == 0 || jl25_http_send_all(fd, buf, n) != 0) {
            free(buf);
            fclose(fp);
            JL25_PushEvent(JL25_EVENT_DOWNLOAD_ABORT, NULL);
            return;
        }
        remaining -= n;
        sent_bytes += n;
        if (sent_bytes - reported_bytes >= (1024 * 1024)) {
            reported_bytes = sent_bytes;
            JL25_PushEvent(JL25_EVENT_DOWNLOAD_1MB, NULL);
        }
    }
    free(buf);
    fclose(fp);
}

void jl25_storage_download_fid(const char* fid, uint64_t offset,
    const char* request, int fd)
{
    if (jl25_storage_access_begin() != 0) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }
    jl25_storage_download_fid_locked(fid, offset, request, fd);
    jl25_storage_access_end();
}

/* Download：文件下载，支持 @offset 断点续传与多文件顺序下载。 */
static void jl25_stor_download(int fd, const char* query)
{
    char dl[512];
    if (jl25_query_get(query, "Download", dl, sizeof(dl)) == NULL) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }

    char* saveptr = NULL;
    char* tok = strtok_r(dl, ",", &saveptr);
    while (tok != NULL) {
        char fid[160];
        uint64_t offset = 0;
        char* at = strchr(tok, '@');
        if (at != NULL) {
            size_t flen = (size_t)(at - tok);
            if (flen >= sizeof(fid)) {
                flen = sizeof(fid) - 1;
            }
            memcpy(fid, tok, flen);
            fid[flen] = '\0';
            offset = strtoull(at + 1, NULL, 10);
        } else {
            snprintf(fid, sizeof(fid), "%s", tok);
        }
        jl25_storage_download_fid_locked(fid, offset, NULL, fd);
        tok = strtok_r(NULL, ",", &saveptr);
    }
}

static int jl25_storage_delete_fid_locked(const char* fid)
{
    char path[256];

    if (fid == NULL || fid[0] == '\0'
        || jl25_fid_to_path(fid, path, sizeof(path)) != 0
        || unlink(path) != 0) {
        return -1;
    }

    /* Keep the album index consistent with the original media file. */
    char basenoext[128];
    snprintf(basenoext, sizeof(basenoext), "%s", fid);
    char* dot = strrchr(basenoext, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    char thumb[256];
    snprintf(thumb, sizeof(thumb), "%s/%s.jpg",
        JL25_THUMB_S_DIR, basenoext);
    unlink(thumb);
    snprintf(thumb, sizeof(thumb), "%s/%s.jpg",
        JL25_THUMB_MOV_S, basenoext);
    unlink(thumb);
    return 0;
}

int jl25_storage_delete_fid(const char* fid)
{
    int ret;

    if (jl25_storage_access_begin() != 0) {
        return -1;
    }
    ret = jl25_storage_delete_fid_locked(fid);
    jl25_storage_access_end();
    return ret;
}

/* Delete：删除文件，多个 fid 用 ',' 分隔（协议 2.3.5）。
 * 协议为 /Storage?fid1,fid2,...（无参数名，query 即 fid 列表）
 */
static void jl25_stor_delete(int fd, const char* query)
{
    char* list = (char*)query;
    int fail = 0;
    int cnt = 0;

    char* saveptr = NULL;
    char* tok = strtok_r(list, ",", &saveptr);
    while (tok != NULL) {
        if (jl25_storage_delete_fid_locked(tok) == 0) {
            cnt++;
        } else {
            fail = 1;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"Result\":%d}", fail ? -1 : 0);
    jl25_http_send_json(fd, resp);
    (void)cnt;
}

/* GetFileList：全量列表（协议 2.3.7） */
static void jl25_stor_getfilelist(int fd)
{
    static jl25_fileinfo_t files[JL25_MAX_FILE_LIST];
    int total = jl25_scan_all(-1, files, JL25_MAX_FILE_LIST);

    /* 响应体可能较大，动态分配 */
    size_t cap = total * 192 + 128;
    char* json = (char*)malloc(cap);
    if (json == NULL) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }

    size_t o = 0;
    o += (size_t)snprintf(json + o, cap - o,
        "{\"Result\":0,\"id\":82215466,\"media\":[");
    /* 协议按目录分组；设备只有 PHOTO/MOVIE 两个目录 */
    const char* dirs[2] = { "PHOTO", "MOVIE" };
    for (int d = 0; d < 2; d++) {
        o += (size_t)snprintf(json + o, cap - o,
            "{\"d\":\"%s\",\"fs\":[", dirs[d]);
        int first = 1;
        for (int i = 0; i < total; i++) {
            const jl25_fileinfo_t* f = &files[i];
            int is_photo = (f->type == JL25_FILETYPE_JPG);
            if ((d == 0) != (is_photo != 0)) {
                continue;
            }
            char esc_name[192];
            jl25_json_escape(f->name, esc_name, sizeof(esc_name));
            o += (size_t)snprintf(json + o, cap - o,
                "%s{\"n\":\"%s\",\"t\":%d,\"dt\":%u,\"s\":%u,\"fid\":\"%s\"}",
                first ? "" : ",",
                esc_name, f->type, f->dt, f->size, f->fid);
            first = 0;
        }
        o += (size_t)snprintf(json + o, cap - o, "]}");
        if (d == 0) {
            o += (size_t)snprintf(json + o, cap - o, ",");
        }
    }
    o += (size_t)snprintf(json + o, cap - o, "]}");

    jl25_http_send_response(fd, 200, "OK", "text/html", json, o);
    free(json);
}

/* GetFilePage：分页列表（协议 2.3.8） */
static void jl25_stor_getfilepage(int fd, const char* query)
{
    char val[32];
    int page = 0;
    int per_page = 16;
    int type_filter = -1;

    if (jl25_query_get(query, "GetFilePage", val, sizeof(val)) != NULL) {
        page = atoi(val);
    }
    if (jl25_query_get(query, "numPerPage", val, sizeof(val)) != NULL) {
        per_page = atoi(val);
        if (per_page <= 0 || per_page > 128) {
            per_page = 16;
        }
    }
    if (jl25_query_get(query, "type", val, sizeof(val)) != NULL) {
        if (strcasecmp(val, "Image") == 0) {
            type_filter = JL25_FILETYPE_JPG;
        } else if (strcasecmp(val, "Video") == 0) {
            type_filter = JL25_FILETYPE_MP4;
        }
    }

    static jl25_fileinfo_t files[JL25_MAX_FILE_LIST];
    int total = jl25_scan_all(type_filter, files, JL25_MAX_FILE_LIST);

    int start = page * per_page;
    if (start > total) {
        start = total;
    }
    int end = start + per_page;
    if (end > total) {
        end = total;
    }

    size_t cap = (size_t)(end - start) * 192 + 128;
    char* json = (char*)malloc(cap);
    if (json == NULL) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }

    size_t o = 0;
    o += (size_t)snprintf(json + o, cap - o,
        "{\"number_of_files\":%d,\"page\":%d,\"fs\":[",
        total, page);
    for (int i = start; i < end; i++) {
        const jl25_fileinfo_t* f = &files[i];
        char esc_name[192];
        jl25_json_escape(f->name, esc_name, sizeof(esc_name));
        o += (size_t)snprintf(json + o, cap - o,
            "%s{\"n\":\"%s\",\"t\":%d,\"dt\":%u,\"s\":%u,\"fid\":\"%s\"}",
            (i == start) ? "" : ",",
            esc_name, f->type, f->dt, f->size, f->fid);
    }
    o += (size_t)snprintf(json + o, cap - o, "]}");

    jl25_http_send_response(fd, 200, "OK", "text/html", json, o);
    free(json);
}

/* ==================== 入口 ==================== */

void jl25_storage_handle(const char* query, int fd)
{
    if (jl25_storage_access_begin() != 0) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }

    if (strstr(query, "GetDirFileInfo") != NULL || strstr(query, "getdirfileinfo") != NULL) {
        jl25_stor_getdirfileinfo(fd);
    } else if (strstr(query, "GetSD_Info") != NULL || strstr(query, "getsd_info") != NULL) {
        jl25_stor_getsdinfo(fd);
    } else if (strstr(query, "GetFileTHumb") != NULL || strstr(query, "getfilethumb") != NULL) {
        jl25_stor_getthumb(fd, query);
    } else if (strstr(query, "Download") != NULL) {
        jl25_stor_download(fd, query);
    } else if (strstr(query, "GetFileList") != NULL || strstr(query, "getfilelist") != NULL) {
        jl25_stor_getfilelist(fd);
    } else if (strstr(query, "GetFilePage") != NULL || strstr(query, "getfilepage") != NULL) {
        jl25_stor_getfilepage(fd, query);
    } else if (strstr(query, "UpgradeFW") != NULL) {
        /* 固件升级走设备本地升级流程，APP 通道不支持 */
        jl25_http_send_json(fd, "{\"Result\":-1}");
    } else if (strchr(query, '=') == NULL && strchr(query, ',') != NULL) {
        /* 无参数名的 fid 列表 -> 删除 */
        jl25_stor_delete(fd, query);
    } else {
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", 15);
    }
    jl25_storage_access_end();
}
