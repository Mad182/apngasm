#ifndef __7Z_H
#define __7Z_H

bool compress_deflate_7z(const unsigned char* in_data, unsigned in_size, unsigned char* out_data, unsigned& out_size, unsigned num_passes, unsigned num_fast_bytes) throw ();
bool compress_rfc1950_7z(const unsigned char* in_data, unsigned in_size, unsigned char* out_data, unsigned& out_size, unsigned num_passes, unsigned num_fast_bytes) throw ();

#endif
