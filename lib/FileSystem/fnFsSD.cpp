/* TODO: Check why using the SD/FAT routines takes up a large amount of the stack (around 4.5K)
*/

#include "fnFsSD.h"

// #include <esp_vfs.h>
// #include <esp_vfs_fat.h>

#include <algorithm>
#include <memory>
#include <vector>
#include <sys/stat.h>
#include <errno.h>
#include "compat_string.h"

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnFileLocal.h"

#if defined(_WIN32)
#include <direct.h>
#define mkdir(A, B) _mkdir(A)
#endif


#include "utils.h"

// #include <esp_idf_version.h>
// #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
// #define SDSPI_DEFAULT_DMA 1
// #endif

// Our global SD interface
FileSystemSDFAT fnSDFAT;

/*
 We maintain directory information cached to allow
 for sorting and to provide telldir/seekdir
*/
std::vector<fsdir_entry> _dir_entries;
uint16_t _dir_entry_current = 0;

bool _fssd_fsdir_sort_name_ascend(fsdir_entry &left, fsdir_entry &right)
{
    return strcasecmp(left.filename, right.filename) < 0;
}

bool _fssd_fsdir_sort_name_descend(fsdir_entry &left, fsdir_entry &right)
{
    return strcasecmp(left.filename, right.filename) > 0;
}

bool _fssd_fsdir_sort_time_ascend(fsdir_entry &left, fsdir_entry &right)
{
    return left.modified_time > right.modified_time;
}

bool _fssd_fsdir_sort_time_descend(fsdir_entry &left, fsdir_entry &right)
{
    return left.modified_time < right.modified_time;
}

typedef bool (*sort_fn_t)(fsdir_entry &left, fsdir_entry &right);

// /*
//   Converts the FatFs ftime and fdate to a POSIX time_t value
// */
// time_t _fssd_fatdatetime_to_epoch(WORD ftime, WORD fdate)
// {
//     // Convert date and time

//     // 5 bits 4-0 = seconds / 2 (0-29)
//     #define TIMEBITS_SECOND 0x001FUL
//     // 6 bits 10-5 = minutes (0-59)
//     #define TIMEBITS_MINUTE 0x07E0UL
//     // 5 bits 15-11 = hour (0-23)
//     #define TIMEBITS_HOUR 0xF800UL

//     // 5 bits 4-0 = day (0-31)
//     #define DATEBITS_DAY 0x001FUL
//     // 4 bits 8-5 = month (1-12)
//     #define DATEBITS_MONTH 0x01E0UL
//     // 7 bits 15-9 = year from 1980 (0-127)
//     #define DATEBITS_YEAR 0xFE00UL

//     struct tm tmtime;

//     tmtime.tm_sec = (ftime & TIMEBITS_SECOND) * 2;
//     tmtime.tm_min = (ftime & TIMEBITS_MINUTE) >> 5;
//     tmtime.tm_hour = (ftime & TIMEBITS_HOUR) >> 11;

//     tmtime.tm_mday = (fdate & DATEBITS_DAY);
//     tmtime.tm_mon = ((fdate & DATEBITS_MONTH) >> 5) -1; // tm_mon = months 0-11
//     tmtime.tm_year = ((fdate & DATEBITS_YEAR) >> 9) + 80; //tm_year = years since 1900

//     tmtime.tm_isdst = 0;

//     #ifdef DEBUG
//     /*
//         Debug_printf("FileSystemSDFAT direntry: \"%s\"\n", _direntry.filename);
//         Debug_printf("FileSystemSDFAT date (0x%04x): yr=%d, mn=%d, da=%d; time (0x%04x) hr=%d, mi=%d, se=%d\n", 
//             finfo.fdate,
//             tmtime.tm_year, tmtime.tm_mon, tmtime.tm_mday,
//             finfo.ftime,
//             tmtime.tm_hour, tmtime.tm_min, tmtime.tm_sec);
//     */
//     #endif

//     return mktime(&tmtime);

// }


bool FileSystemSDFAT::is_dir(const char *path)
{
    char * fpath = _make_fullpath(path);
    struct stat info;
    stat( fpath, &info);
    return (info.st_mode == S_IFDIR) ? true: false;
}

bool FileSystemSDFAT::dir_open(const char * path, const char * pattern, uint16_t diropts)
{
    Debug_printf("FileSystemSDFAT::dir_open \"%s\"\n", path);
    // TODO: Add pattern and sorting options

    // Throw out any existing directory entry data
    _dir_entries.clear();
    _dir_entry_current = 0;

    char * fpath = _make_fullpath(path);
    Debug_printf("FileSystemSDFAT::dir_open - opendir \"%s\"\n", fpath);
    _dir = opendir(fpath);
    free(fpath);

    if(_dir == nullptr)
        return false;

    bool have_pattern = pattern != nullptr && pattern[0] != '\0';

    // Read all the directory entries and store them
    // We temporarily keep separate lists of files and directories so we can sort them separately
    std::vector<fsdir_entry> store_directories;
    std::vector<fsdir_entry> store_files;
    fsdir_entry *entry;
    struct dirent *d;
    struct stat s;

    while((d = readdir(_dir)) != nullptr)
    {
        // // An empty name indicates the end of the directory
        // if(finfo.fname[0] == '\0')
        //     break;
        // Ignore items starting with '.'
        if(d->d_name[0] == '.')
            continue;
        // // Ignore items marked hidden or system
        // if(finfo.fattrib & AM_HID || finfo.fattrib & AM_SYS)
        //     continue;
        // Ignore some special files we create on SD
        if(strcmp(d->d_name, "paper") == 0 
        || strcmp(d->d_name, "fnconfig.ini") == 0
        || strcmp(d->d_name, "rs232dump") == 0)
            continue;

        // Debug_printf("Entry %s (%d)\n", d->d_name, d->d_type);

        // Determine which list to put this in
        if(d->d_type == DT_DIR || d->d_type == DT_LNK) // well, assume symlinks points to directories only
        {
            store_directories.push_back(fsdir_entry());
            entry = &store_directories.back();
            entry->isDir = true;
        }
        else
        {
            // Skip this entry if we have a search filter and it doesn't match it
            if(have_pattern && util_wildcard_match(d->d_name, pattern) == false)
                continue;

            store_files.push_back(fsdir_entry());
            entry = &store_files.back();
            entry->isDir = false;
        }

        // Copy the data we want into the record
        strlcpy(entry->filename, d->d_name, sizeof(entry->filename));
        fpath = _make_fullpath(entry->filename);
        if(stat(fpath, &s) == 0)
        {
            entry->size = s.st_size;
            entry->modified_time = s.st_mtime;
        }
        free(fpath);
    }

    // Choose the appropriate sorting function
    sort_fn_t sortfn;
    if (diropts & DIR_OPTION_FILEDATE)
    {
        sortfn = (diropts & DIR_OPTION_DESCENDING) ? _fssd_fsdir_sort_time_descend : _fssd_fsdir_sort_time_ascend;
    }
    else
    {
        sortfn = (diropts & DIR_OPTION_DESCENDING) ? _fssd_fsdir_sort_name_descend : _fssd_fsdir_sort_name_ascend;        
    }

    // Sort each list
    std::sort(store_directories.begin(), store_directories.end(), sortfn);
    std::sort(store_files.begin(), store_files.end(), sortfn);

    // Combine the folder and file entries
    _dir_entries.reserve( store_directories.size() + store_files.size() );
    _dir_entries = store_directories; // This copies the contents from one vector to the other
    _dir_entries.insert( _dir_entries.end(), store_files.begin(), store_files.end() );

    // Future operations will be performed on the cache
    closedir(_dir);

    return true;
}

void FileSystemSDFAT::dir_close()
{
    // Throw out any existing directory entry data
    _dir_entries.clear();
    _dir_entries.shrink_to_fit();
    _dir_entry_current = 0;
}

fsdir_entry * FileSystemSDFAT::dir_read()
{
    if(_dir_entry_current < _dir_entries.size())
    {
        //Debug_printf("#%d = \"%s\"\r\n", _dir_entry_current, _dir_entries[_dir_entry_current].filename);
        return &_dir_entries[_dir_entry_current++];
    }
    else
        return nullptr;
}

uint16_t FileSystemSDFAT::dir_tell()
{
    if(_dir_entries.empty())
        return FNFS_INVALID_DIRPOS;
    else
        return _dir_entry_current;
}

bool FileSystemSDFAT::dir_seek(uint16_t pos)
{
    if(pos < _dir_entries.size())
    {
        _dir_entry_current = pos;
        return true;
    }
    else
        return false;
}


FILE * FileSystemSDFAT::file_open(const char* path, const char* mode)
{
    //Debug_printf("sdfileopen1: task hwm %u, %p\r\n", uxTaskGetStackHighWaterMark(NULL), pxTaskGetStackStart(NULL));
    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    //Debug_printf("sdfileopen2: task hwm %u, %p\r\n", uxTaskGetStackHighWaterMark(NULL), pxTaskGetStackStart(NULL));
#ifdef DEBUG
    Debug_printf("fopen = %s : %s\r\n", path, result == nullptr ? "err" : "ok");
#endif    
    return result;
}


FileHandler * FileSystemSDFAT::filehandler_open(const char* path, const char* mode)
{
    Debug_printf("FileSystemSDFAT::filehandler_open %s %s\n", path, mode);
    FILE * fh = file_open(path, mode);
    return (fh == nullptr) ? nullptr : new FileHandlerLocal(fh);
}


bool FileSystemSDFAT::exists(const char* path)
{
    char * fpath = _make_fullpath(path);
    struct stat st;
    int i = stat(fpath, &st);
#ifdef DEBUG
    //Debug_printf("sdFileSystem::exists returned %d on \"%s\"\r\n", result, path);
#endif
    free(fpath);
    return (i == 0);
}

bool FileSystemSDFAT::remove(const char* path)
{
    char * fpath = _make_fullpath(path);
    int i = ::remove(fpath);
#ifdef DEBUG
    //Debug_printf("sdFileSystem::remove returned %d on \"%s\"\r\n", result, path);
#endif
    free(fpath);
    return (i == 0);
}

/* Checks that path exists and creates if it doesn't including any parent directories
   Each directory along the path is limited to 64 characters
   An initial "/" is optional, but you should not include an ending "/"

   Examples:
   "abc"
   "/abc"
   "/abc/def"
   "abc/def/ghi"
*/
bool FileSystemSDFAT::create_path(const char *path)
{
    char segment[64];

    char *fpath = _make_fullpath(path);
    const char *end = fpath;
    bool done = false;

    while (!done)
    {
        bool found = false;

        if(*end == '\0')
        {
            done = true;
            // Only indicate we found a segment if we're not still pointing to the start
            if(end != fpath)
                found = true;
        } else if(*end == '/')
        {
            // Only indicate we found a segment if this isn't a starting '/'
            if(end != fpath)
                found = true;
        }

        if(found)
        {
            /* We copy the segment from the fullpath using a length of (end - fullpath) + 1
               This allows for the ending terminator but not for the trailing '/'
               If we're done (at the end of fullpath), we assume there's no  trailing '/' so the length
               is (end - fullpath) + 2
            */
            strlcpy(segment, fpath, end - fpath + (done ? 2 : 1));
            Debug_printf("Checking/creating directory: \"%s\"\n", segment);
            // if(0 != f_mkdir(segment))
            // {
            //     Debug_printf("FAILED errno=%d\n", errno);
            // }
            if(0 != ::mkdir(segment, S_IRWXU))
            {
                if(errno != EEXIST)
                {
                    Debug_printf("FAILED errno=%d\n", errno);
                    free(fpath);
                    return false;
                }
            }
        }

        end++;
    }
    free(fpath);

    return true;
}

bool FileSystemSDFAT::rename(const char* pathFrom, const char* pathTo)
{
    char * spath = _make_fullpath(pathFrom);
    char * dpath = _make_fullpath(pathTo);
    int i = ::rename(spath, dpath);
#ifdef DEBUG
    Debug_printf("FileSystemSDFAT::rename returned %d on \"%s\" -> \"%s\" (%s -> %s)\n", i, pathFrom, pathTo, spath, dpath);
#endif
    free(spath);
    free(dpath);
    return (i == 0);
}

uint64_t FileSystemSDFAT::card_size()
{
    return _card_capacity;
}

uint64_t FileSystemSDFAT::total_bytes()
{
	// FATFS* fsinfo;
	// DWORD fre_clust;
    uint64_t size = 0ULL;

	// if (f_getfree("0:", &fre_clust, &fsinfo) == 0)
    // {
    //     // cluster_size * num_clusters * sector_size
    //     size = ((uint64_t)(fsinfo->csize)) * (fsinfo->n_fatent - 2) * (fsinfo->ssize);
    // }
	return size;
}

uint64_t FileSystemSDFAT::used_bytes()
{
	// FATFS* fsinfo;
	// DWORD fre_clust;
    uint64_t size = 0ULL;

	// if(f_getfree("0:", &fre_clust, &fsinfo) == 0)
    // {
    //     // cluster_size * (num_clusters - free_clusters) * sector_size
	//     size = ((uint64_t)(fsinfo->csize)) * ((fsinfo->n_fatent-2)-(fsinfo->free_clst)) * (fsinfo->ssize);
    // }
	return size;
}

const char * FileSystemSDFAT::partition_type()
{
    static const char *names[] = 
    {
        "UNKNOWN",
        "FAT12",
        "FAT16",
        "FAT32",
        "EXFAT"
    };

	// FATFS* fsinfo;
	// DWORD fre_clust;

    // BYTE i = 0;
    int i = 0;
 	// if(f_getfree("0:", &fre_clust, &fsinfo) == 0)
    // {
    //     if(fsinfo->fs_type >= FS_FAT12 && fsinfo->fs_type <= FS_EXFAT)
    //         i = fsinfo->fs_type;
    // }
    return names[i];
}

bool FileSystemSDFAT::start(const char *sd_path)
{
    if(_started)
        return true;

    // Set our basepath
    // strlcpy(_basepath, "/sd", sizeof(_basepath));
    if (sd_path)
        strlcpy(_basepath, sd_path, sizeof(_basepath));
    else
        strlcpy(_basepath, "SD", sizeof(_basepath));

    // // Set up a configuration to the SD host interface
    // sdmmc_host_t host_config = SDSPI_HOST_DEFAULT(); 

    // // Set up SPI bus
    // spi_bus_config_t bus_cfg = 
    // {
    //     .mosi_io_num = PIN_SD_HOST_MOSI,
    //     .miso_io_num = PIN_SD_HOST_MISO,
    //     .sclk_io_num = PIN_SD_HOST_SCK,
    //     .quadwp_io_num = -1,
    //     .quadhd_io_num = -1,
    //     .max_transfer_sz = 4000
    // };

    // spi_bus_initialize(HSPI_HOST,&bus_cfg,1);

    // // sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    // // slot_config.gpio_cs = SD_HOST_CS;
    // // slot_config.gpio_miso = SD_HOST_MISO;
    // // slot_config.gpio_mosi = SD_HOST_MOSI;
    // // slot_config.gpio_sck = SD_HOST_SCK;

    // sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    // slot_config.gpio_cs = PIN_SD_HOST_CS;
    // slot_config.host_id = SPI2_HOST;

    // // Fat FS configuration options
    // esp_vfs_fat_mount_config_t mount_config;
    // mount_config.format_if_mount_failed = false;
    // mount_config.max_files = 16;

    // // This is the information we'll be given in return
    // sdmmc_card_t *sdcard_info;

    // // esp_err_t e = esp_vfs_fat_sdmmc_mount(
    // //     _basepath,
    // //     &host_config,
    // //     &slot_config,
    // //     &mount_config,
    // //     &sdcard_info
    // // );

    // esp_err_t e = esp_vfs_fat_sdspi_mount(_basepath, &host_config, &slot_config, &mount_config, &sdcard_info);

    // TODO test if _basepath directory exists
    // if(e == ESP_OK)
    {
        _started = true;
        // _card_capacity = (uint64_t)sdcard_info->csd.capacity * sdcard_info->csd.sector_size;
        _card_capacity = 0;
    #ifdef DEBUG
        Debug_println("SD mounted.");

    /*
        Debug_printf("  manufacturer: %d, oem: 0x%x \"%c%c\"\r\n", sdcard_info->cid.mfg_id, sdcard_info->cid.oem_id,
            (char)(sdcard_info->cid.oem_id >> 8 & 0xFF),(char)(sdcard_info->cid.oem_id & 0xFF));
        Debug_printf("  product: %s\r\n", sdcard_info->cid.name);
        Debug_printf("  sector size: %d, sectors: %d, capacity: %llu\r\n", sdcard_info->csd.sector_size, sdcard_info->csd.capacity, _card_capacity);
        Debug_printf("  transfer speed: %d\r\n", sdcard_info->csd.tr_speed);
        Debug_printf("  max frequency: %ukHz\r\n", sdcard_info->max_freq_khz);
        Debug_printf("  partition type: %s\r\n", partition_type());
        Debug_printf("  partition size: %llu, used: %llu\r\n", total_bytes(), used_bytes());
    */
    #endif
    }
    // else 
    // {
    //     _started = false;
    //     _card_capacity = 0;
    // #ifdef DEBUG
    //     Debug_printf("SD mount failed with code #%d, \"%s\"\n", e, esp_err_to_name(e));
    // #endif
    // }

    return _started;
}
