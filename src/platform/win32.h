// Win32 API functions used by engine code, with Windows semantics on every platform.
//
// Hybrid builds (a DLL inside the original process) call the real Win32 functions. Native builds
// use portable equivalents: private-profile (.ini) access with case-insensitive sections and keys,
// file lookup with case-insensitive path resolution (game data file names are case-insensitive on
// Windows), and a headless MessageBoxA that prints to stderr.
//
// Paths may use '\\' or '/' separators; native builds treat both as directory separators.
#pragma once
#include <cstdint>

namespace win32 {

// GetPrivateProfileStringA: copies the value of key in section (or def if missing) into buf,
// truncated to size - 1 characters; returns the number of characters copied.
uint32_t GetPrivateProfileStringA(const char* section, const char* key, const char* def, char* buf,
                                  uint32_t size, const char* file);
// WritePrivateProfileStringA: sets key in section (creating section/file as needed). Returns
// non-zero on success.
int32_t WritePrivateProfileStringA(const char* section, const char* key, const char* value, const char* file);
// GetCurrentDirectoryA: writes the current directory (without trailing separator); returns its
// length, or the required size if size is too small.
uint32_t GetCurrentDirectoryA(uint32_t size, char* buf);
int32_t SetCurrentDirectoryA(const char* path);
// GetDriveTypeA for a root such as "D:\\". Native builds have no drive letters: DRIVE_NO_ROOT_DIR (1).
uint32_t GetDriveTypeA(const char* root);
// FindFirstFileA + FindClose: true if a file or directory matches path (no wildcard support in
// native builds).
bool file_exists(const char* path);
// Reads a whole file (CreateFileA/ReadFile; the original maps the file into memory instead).
// Returns a buffer to release with free_file(), or nullptr; *size receives the file size.
uint8_t* read_file(const char* path, uint32_t* size);
void free_file(uint8_t* data);
// MessageBoxA with an OK button; native builds print caption and text to stderr. Returns IDOK (1).
int32_t MessageBoxA(const char* text, const char* caption, uint32_t type);
// CharUpperA on a NUL-terminated string (ASCII and Windows-1252 letters, as the "C"/1252 locale).
void CharUpperA(char* str);

}  // namespace win32

namespace platform {
// Native builds: resolves path case-insensitively (each component, '\\' or '/' separators) against
// the file system. Returns the resolved host path, or the input with separators converted when
// nothing matches.
const char* resolve_path(const char* path, char* out, uint32_t out_size);
#ifndef SMACX_HYBRID
// Native builds, portable file system access on resolved host paths (std::filesystem).
// host_stat: false if nothing exists at path; otherwise *is_dir and *size (files) are set.
bool host_stat(const char* path, bool* is_dir, uint64_t* size);
// _mkdir: true if the directory was created (false if it exists or cannot be created).
bool host_mkdir(const char* path);
#endif
}  // namespace platform
