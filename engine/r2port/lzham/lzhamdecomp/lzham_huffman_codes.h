// Origin: LZHAM Alpha 8, copied from C:\msys64\home\r3muxd\revpk-mutated\lzham_alpha. Copyright Richard Geldreich, Jr.; MIT license in ../LICENSE.txt.
// File: lzham_huffman_codes.h
// See Copyright Notice and license at the end of include/lzham.h
#pragma once

namespace lzham
{
   //const uint cHuffmanMaxSupportedSyms = 600;
   const uint cHuffmanMaxSupportedSyms = 1024;
   
   uint get_generate_huffman_codes_table_size();
   
   bool generate_huffman_codes(void* pContext, uint num_syms, const uint16* pFreq, uint8* pCodesizes, uint& max_code_size, uint& total_freq_ret);

} // namespace lzham
