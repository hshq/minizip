/* minizip.c
   Version 2.0.1, October 16th, 2017
   part of the MiniZip project

   Copyright (C) 2012-2017 Nathan Moinvaziri
     https://github.com/nmoinvaz/minizip
   Copyright (C) 2009-2010 Mathias Svensson
     Modifications for Zip64 support
     http://result42.com
   Copyright (C) 2007-2008 Even Rouault
     Modifications of Unzip for Zip64
   Copyright (C) 1998-2010 Gilles Vollant
     http://www.winimage.com/zLibDll/minizip.html

   This program is distributed under the terms of the same license as zlib.
   See the accompanying LICENSE file for the full text of the license.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "mz.h"
#include "mz_os.h"
#include "mz_strm.h"
#include "mz_strm_split.h"
#include "mz_zip.h"

/***************************************************************************/

void minizip_banner()
{
    printf("Minizip %s - https://github.com/nmoinvaz/minizip\n", MZ_VERSION);
    printf("---------------------------------------------------\n");
}

void minizip_help()
{
    printf("Usage : minizip [-o] [-a] [-0 to -9] [-b|-m] [-s] [-j] [-p password] [-k 512] file.zip [files_to_add]\n\n");
    printf("  -o  Overwrite existing file.zip\n");
    printf("  -a  Append to existing file.zip\n");
    printf("  -0  Store only\n");
    printf("  -1  Compress faster\n");
    printf("  -9  Compress better\n");
#ifdef HAVE_BZIP2
    printf("  -b  BZIP2 compression\n");
#endif
#ifdef HAVE_LZMA
    printf("  -m  LZMA compression\n");
#endif
#ifdef HAVE_AES
    printf("  -s  AES encryption\n");
#endif
    printf("  -p  Encryption password\n");
    printf("  -k  Disk size in KB\n");
    printf("  -j  Exclude path and store only the file name\n\n");
}

/***************************************************************************/

typedef struct minizip_opt_s {
    uint8_t exclude_path;
} minizip_opt;

/***************************************************************************/

int32_t minizip_add_file(void *handle, const char *path, minizip_opt *options, mz_zip_compress *compress_info, mz_zip_crypt *crypt_info)
{
    mz_zip_file file_info = { 0 };
    int32_t read = 0;
    int32_t written = 0;
    int16_t err = MZ_OK;
    int16_t err_close = MZ_OK;
    void *stream = NULL;
    const char *filenameinzip = NULL;
    char buf[INT16_MAX];


    // Construct the filename that our file will be stored in the zip as.
    // The path name saved, should not include a leading slash.
    // If it did, windows/xp and dynazip couldn't read the zip file. 

    filenameinzip = path;
    while (filenameinzip[0] == '\\' || filenameinzip[0] == '/')
        filenameinzip += 1;

    // Should the file be stored with any path info at all?
    if (options->exclude_path)
    {
        const char *match = NULL;
        const char *last_slash = NULL;

        for (match = filenameinzip; *match; match += 1)
        {
            if (*match == '\\' || *match == '/')
                last_slash = match;
        }

        if (last_slash != NULL)
            filenameinzip = last_slash + 1; // base filename follows last slash
    }
    
    // Get information about the file on disk so we can store it in zip
    printf("Adding: %s\n", filenameinzip);

    file_info.filename = filenameinzip;

    if (mz_file_get_size(path) >= UINT32_MAX)
        file_info.zip64 = 1;

    mz_os_get_file_date(path, &file_info.dos_date);

    // Add to zip
    err = mz_zip_entry_open(handle, &file_info, compress_info, crypt_info);
    if (err != MZ_OK)
    {
        printf("Error in opening %s in zip file (%d)\n", filenameinzip, err);
        return err;
    }

    mz_stream_os_create(&stream);

    err = mz_stream_os_open(stream, path, MZ_STREAM_MODE_READ);

    if (err == MZ_OK)
    {
        // Read contents of file and write it to zip
        do
        {
            read = mz_stream_os_read(stream, buf, sizeof(buf));
            if (read < 0)
            {
                err = mz_stream_os_error(stream);
                printf("Error %d in reading %s\n", err, filenameinzip);
                break;
            }
            if (read == 0)
                break;

            written = mz_zip_entry_write(handle, buf, read);
            if (written != read)
            {
                err = mz_stream_os_error(stream);
                printf("Error in writing %s in the zip file (%d)\n", filenameinzip, err);
                break;
            }
        }
        while (err == MZ_OK);

        mz_stream_os_close(stream);
    }
    else
    {
        printf("Error in opening %s for reading\n", path);
    }

    mz_stream_os_delete(&stream);

    err_close = mz_zip_entry_close(handle);
    if (err_close != MZ_OK)
        printf("Error in closing %s in the zip file (%d)\n", filenameinzip, err_close);

    return err;
}

int32_t minizip_add(void *handle, const char *path, minizip_opt *options, mz_zip_compress *compress_info, mz_zip_crypt *crypt_info, uint8_t recursive)
{
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    int16_t err = 0;
    char full_path[320];


    if (mz_os_is_dir(path) != MZ_OK)
        return minizip_add_file(handle, path, options, compress_info, crypt_info);

    dir = mz_os_open_dir(path);

    if (dir == NULL)
    {
        printf("Cannot enumerate directory %s\n", path);
        return MZ_EXIST_ERROR;
    }

    while ((entry = mz_os_read_dir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        full_path[0] = 0;
        mz_path_combine(full_path, path, sizeof(full_path));
        mz_path_combine(full_path, entry->d_name, sizeof(full_path));

        if (!recursive && mz_os_is_dir(full_path))
            continue;

        err = minizip_add(handle, full_path, options, compress_info, crypt_info, recursive);
        if (err != MZ_OK)
            return err;
    }

    mz_os_close_dir(dir);
    return MZ_OK;
}

#ifndef NOMAIN
int main(int argc, char *argv[])
{
    void *handle = NULL;
    void *file_stream = NULL;
    void *split_stream = NULL;
    void *open_stream = NULL;
    char *path = NULL;
    minizip_opt options;
    mz_zip_compress compress_info;
    mz_zip_crypt crypt_info;
    int64_t disk_size = 0;
    int32_t path_arg = 0;
    uint8_t opt_append = 0;
    uint8_t opt_open_existing = 0;
    uint8_t opt_exclude_path = 0;
    int16_t mode = 0;
    int16_t err_close = 0;
    int16_t err = 0;
    int16_t i = 0;

    minizip_banner();
    if (argc == 1)
    {
        minizip_help();
        return 0;
    }

    memset(&options, 0, sizeof(options));
    memset(&compress_info, 0, sizeof(compress_info));
    memset(&crypt_info, 0, sizeof(crypt_info));

    compress_info.method = MZ_COMPRESS_METHOD_DEFLATE;
    compress_info.level = MZ_COMPRESS_LEVEL_DEFAULT;

    // Parse command line options
    for (i = 1; i < argc; i++)
    {
        if ((*argv[i]) == '-')
        {
            const char *p = argv[i]+1;

            while ((*p) != '\0')
            {
                char c = *(p++);
                if ((c == 'o') || (c == 'O'))
                    opt_append = 1;
                if ((c == 'a') || (c == 'A'))
                    opt_open_existing = 1;
                if ((c >= '0') && (c <= '9'))
                {
                    compress_info.level = (c - '0');
                    if (compress_info.level == 0)
                        compress_info.method = MZ_COMPRESS_METHOD_RAW;
                }
                if ((c == 'j') || (c == 'J'))
                    options.exclude_path = 1;
#ifdef HAVE_BZIP2
                if ((c == 'b') || (c == 'B'))
                    compress_info.method = MZ_COMPRESS_METHOD_BZIP2;
#endif
#ifdef HAVE_LZMA
                if ((c == 'm') || (c == 'M'))
                    compress_info.method = MZ_COMPRESS_METHOD_LZMA;
#endif
#ifdef HAVE_AES
                if ((c == 's') || (c == 'S'))
                    crypt_info.aes = 1;
#endif
                if (((c == 'k') || (c == 'k')) && (i + 1 < argc))
                {
                    disk_size = atoi(argv[i + 1]) * 1024;
                    i += 1;
                }
                if (((c == 'p') || (c == 'P')) && (i + 1 < argc))
                {
                    crypt_info.password = argv[i + 1];
                    i += 1;
                }
            }

            continue;
        }

        if (path_arg == 0)
            path_arg = i;
    }

    if (path_arg == 0)
    {
        minizip_help();
        return 0;
    }

    path = argv[path_arg];

    if (opt_open_existing)
    {
        // If the file doesn't exist, we don't append file
        if (mz_file_exists(path) != MZ_OK)
            opt_append = 0;
    }
    else if (opt_append == 0)
    {
        // If ask the user what to do because append and overwrite args not set
        if (mz_file_exists(path) != MZ_OK)
        {
            char rep = 0;
            do
            {
                char answer[128];
                printf("The file %s exists. Overwrite ? [y]es, [n]o, [a]ppend : ", path);
                if (scanf("%1s", answer) != 1)
                    exit(EXIT_FAILURE);
                rep = answer[0];

                if ((rep >= 'a') && (rep <= 'z'))
                    rep -= 0x20;
            }
            while ((rep != 'Y') && (rep != 'N') && (rep != 'A'));

            if (rep == 'A')
            {
                opt_open_existing = 1;
            }
            else if (rep == 'N')
            {
                minizip_help();
                return 0;
            }
        }
    }

    mz_stream_os_create(&file_stream);

    mode = MZ_STREAM_MODE_READWRITE;
    if (opt_append)
        mode |= MZ_STREAM_MODE_APPEND;
    else
        mode |= MZ_STREAM_MODE_CREATE;

    if (disk_size > 0)
    {
        mz_stream_split_create(&split_stream);
        mz_stream_set_base(split_stream, file_stream);
        mz_stream_split_set_prop_int64(split_stream, MZ_STREAM_PROP_DISK_SIZE, disk_size);

        open_stream = split_stream;
    }
    else
    {
        open_stream = file_stream;
    }
    
    err = mz_stream_open(open_stream, path, mode);

    if (err != MZ_OK)
    {
        printf("Error opening file %s\n", path);
    }
    else
    {
        handle = mz_zip_open(opt_open_existing, open_stream);

        if (handle == NULL)
        {
            printf("Error opening zip %s\n", path);
            err = MZ_FORMAT_ERROR;
        }
        else
        {
            printf("Creating %s\n", path);

            // Go through command line args looking for files to add to zip
            for (i = path_arg + 1; (i < argc) && (err == MZ_OK); i += 1)
                err = minizip_add(handle, argv[i], &options, &compress_info, &crypt_info, 1);

            err_close = mz_zip_close(handle, NULL, MZ_VERSION_MADEBY);

            if (err_close != MZ_OK)
                printf("Error in closing %s (%d)\n", path, err_close);
        }

        mz_stream_os_close(file_stream);
    }

    if (split_stream != NULL)
        mz_stream_split_delete(&split_stream);

    mz_stream_os_delete(&file_stream);

    return err;
}
#endif
