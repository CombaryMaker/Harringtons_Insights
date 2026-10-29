// HCM -- Harrington Compression Method, version 3.3
// Copyright (c) 2026 Michael Harrington. MIT License (full text at the end
// of this file).
//
// A context-mixing compressor. For every bit it asks many contexts (the
// last 1, 2, 3, 4, 6 and 8 bytes, the partial byte, the current word, the
// previous word plus the current word) for their running balance of 0s and
// 1s, checks a long-match finder, blends all of it with a learned mixer,
// refines the result, and codes the bit with a 32-bit arithmetic coder.
// The decoder runs the identical model, so the file needs nothing else.
//
// v3.5 (29 Oct 2026): parallel blocks (commands p = fast, q = max; HCM_BLOCK_MB sets the
// block size, default 16) decoded on separate processor cores, and text/binary sections
// inside one file (one model, contexts switch at each boundary; kept only if smaller).
// v3.4 (29 Oct 2026): match recovery after one wrong byte, numeric contexts for 16-bit
// samples in binary files, incompressible files stored unchanged, stricter check on old v2
// files. Every older .hcm file decodes exactly as before.
// v3.3 adds the Harrington Insights to the v3 engine: binary files get
// record contexts (the record length is found automatically), text files get
// a line-position context and word-position mixer weights. Each file is
// compressed both ways and the smaller result is kept, so no file is ever
// bigger than with v3, and decoding runs at the same speed. v3.3 reads v3
// and v2 files. Two modes: fast (about 32 MB of tables) and max (about
// 64 MB of tables and a second mixer, a little smaller files).
//
// Use: drag files onto the program. A normal file is compressed to
// NAME.hcm and checked by decoding it back before it is saved; a .hcm file
// is restored. If the program's file name contains "max" (for example
// HCM-Max.exe) it compresses in max mode.
// Command line: hcm c IN OUT (fast) | hcm x IN OUT (max) | hcm d IN OUT
//
// Build with floating-point contraction off: -ffp-contract=off.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#include <shellapi.h>
#endif

// ------------------------------------------------------------------------
// Built-in exp() and log()

// exp constants (N=128, poly order 5)
static const uint64_t EXP_INVLN2N=0x40671547652b82feULL, EXP_NEGLN2HIN=0xbf762e42fefa0000ULL, EXP_NEGLN2LON=0xbd0cf79abc9e3b3aULL, EXP_SHIFT=0x4338000000000000ULL;
static const uint64_t EXP_POLY[4]={0x3fdffffffffffdbdULL,0x3fc555555555543cULL,0x3fa55555cf172b91ULL,0x3f81111167a4d017ULL};
static const uint64_t EXP_TAB[256]={
  0x0000000000000000ULL,0x3ff0000000000000ULL,0x3c9b3b4f1a88bf6eULL,0x3feff63da9fb3335ULL,
  0xbc7160139cd8dc5dULL,0x3fefec9a3e778061ULL,0xbc905e7a108766d1ULL,0x3fefe315e86e7f85ULL,
  0x3c8cd2523567f613ULL,0x3fefd9b0d3158574ULL,0xbc8bce8023f98efaULL,0x3fefd06b29ddf6deULL,
  0x3c60f74e61e6c861ULL,0x3fefc74518759bc8ULL,0x3c90a3e45b33d399ULL,0x3fefbe3ecac6f383ULL,
  0x3c979aa65d837b6dULL,0x3fefb5586cf9890fULL,0x3c8eb51a92fdeffcULL,0x3fefac922b7247f7ULL,
  0x3c3ebe3d702f9cd1ULL,0x3fefa3ec32d3d1a2ULL,0xbc6a033489906e0bULL,0x3fef9b66affed31bULL,
  0xbc9556522a2fbd0eULL,0x3fef9301d0125b51ULL,0xbc5080ef8c4eea55ULL,0x3fef8abdc06c31ccULL,
  0xbc91c923b9d5f416ULL,0x3fef829aaea92de0ULL,0x3c80d3e3e95c55afULL,0x3fef7a98c8a58e51ULL,
  0xbc801b15eaa59348ULL,0x3fef72b83c7d517bULL,0xbc8f1ff055de323dULL,0x3fef6af9388c8deaULL,
  0x3c8b898c3f1353bfULL,0x3fef635beb6fcb75ULL,0xbc96d99c7611eb26ULL,0x3fef5be084045cd4ULL,
  0x3c9aecf73e3a2f60ULL,0x3fef54873168b9aaULL,0xbc8fe782cb86389dULL,0x3fef4d5022fcd91dULL,
  0x3c8a6f4144a6c38dULL,0x3fef463b88628cd6ULL,0x3c807a05b0e4047dULL,0x3fef3f49917ddc96ULL,
  0x3c968efde3a8a894ULL,0x3fef387a6e756238ULL,0x3c875e18f274487dULL,0x3fef31ce4fb2a63fULL,
  0x3c80472b981fe7f2ULL,0x3fef2b4565e27cddULL,0xbc96b87b3f71085eULL,0x3fef24dfe1f56381ULL,
  0x3c82f7e16d09ab31ULL,0x3fef1e9df51fdee1ULL,0xbc3d219b1a6fbffaULL,0x3fef187fd0dad990ULL,
  0x3c8b3782720c0ab4ULL,0x3fef1285a6e4030bULL,0x3c6e149289cecb8fULL,0x3fef0cafa93e2f56ULL,
  0x3c834d754db0abb6ULL,0x3fef06fe0a31b715ULL,0x3c864201e2ac744cULL,0x3fef0170fc4cd831ULL,
  0x3c8fdd395dd3f84aULL,0x3feefc08b26416ffULL,0xbc86a3803b8e5b04ULL,0x3feef6c55f929ff1ULL,
  0xbc924aedcc4b5068ULL,0x3feef1a7373aa9cbULL,0xbc9907f81b512d8eULL,0x3feeecae6d05d866ULL,
  0xbc71d1e83e9436d2ULL,0x3feee7db34e59ff7ULL,0xbc991919b3ce1b15ULL,0x3feee32dc313a8e5ULL,
  0x3c859f48a72a4c6dULL,0x3feedea64c123422ULL,0xbc9312607a28698aULL,0x3feeda4504ac801cULL,
  0xbc58a78f4817895bULL,0x3feed60a21f72e2aULL,0xbc7c2c9b67499a1bULL,0x3feed1f5d950a897ULL,
  0x3c4363ed60c2ac11ULL,0x3feece086061892dULL,0x3c9666093b0664efULL,0x3feeca41ed1d0057ULL,
  0x3c6ecce1daa10379ULL,0x3feec6a2b5c13cd0ULL,0x3c93ff8e3f0f1230ULL,0x3feec32af0d7d3deULL,
  0x3c7690cebb7aafb0ULL,0x3feebfdad5362a27ULL,0x3c931dbdeb54e077ULL,0x3feebcb299fddd0dULL,
  0xbc8f94340071a38eULL,0x3feeb9b2769d2ca7ULL,0xbc87deccdc93a349ULL,0x3feeb6daa2cf6642ULL,
  0xbc78dec6bd0f385fULL,0x3feeb42b569d4f82ULL,0xbc861246ec7b5cf6ULL,0x3feeb1a4ca5d920fULL,
  0x3c93350518fdd78eULL,0x3feeaf4736b527daULL,0x3c7b98b72f8a9b05ULL,0x3feead12d497c7fdULL,
  0x3c9063e1e21c5409ULL,0x3feeab07dd485429ULL,0x3c34c7855019c6eaULL,0x3feea9268a5946b7ULL,
  0x3c9432e62b64c035ULL,0x3feea76f15ad2148ULL,0xbc8ce44a6199769fULL,0x3feea5e1b976dc09ULL,
  0xbc8c33c53bef4da8ULL,0x3feea47eb03a5585ULL,0xbc845378892be9aeULL,0x3feea34634ccc320ULL,
  0xbc93cedd78565858ULL,0x3feea23882552225ULL,0x3c5710aa807e1964ULL,0x3feea155d44ca973ULL,
  0xbc93b3efbf5e2228ULL,0x3feea09e667f3bcdULL,0xbc6a12ad8734b982ULL,0x3feea012750bdabfULL,
  0xbc6367efb86da9eeULL,0x3fee9fb23c651a2fULL,0xbc80dc3d54e08851ULL,0x3fee9f7df9519484ULL,
  0xbc781f647e5a3ecfULL,0x3fee9f75e8ec5f74ULL,0xbc86ee4ac08b7db0ULL,0x3fee9f9a48a58174ULL,
  0xbc8619321e55e68aULL,0x3fee9feb564267c9ULL,0x3c909ccb5e09d4d3ULL,0x3feea0694fde5d3fULL,
  0xbc7b32dcb94da51dULL,0x3feea11473eb0187ULL,0x3c94ecfd5467c06bULL,0x3feea1ed0130c132ULL,
  0x3c65ebe1abd66c55ULL,0x3feea2f336cf4e62ULL,0xbc88a1c52fb3cf42ULL,0x3feea427543e1a12ULL,
  0xbc9369b6f13b3734ULL,0x3feea589994cce13ULL,0xbc805e843a19ff1eULL,0x3feea71a4623c7adULL,
  0xbc94d450d872576eULL,0x3feea8d99b4492edULL,0x3c90ad675b0e8a00ULL,0x3feeaac7d98a6699ULL,
  0x3c8db72fc1f0eab4ULL,0x3feeace5422aa0dbULL,0xbc65b6609cc5e7ffULL,0x3feeaf3216b5448cULL,
  0x3c7bf68359f35f44ULL,0x3feeb1ae99157736ULL,0xbc93091fa71e3d83ULL,0x3feeb45b0b91ffc6ULL,
  0xbc5da9b88b6c1e29ULL,0x3feeb737b0cdc5e5ULL,0xbc6c23f97c90b959ULL,0x3feeba44cbc8520fULL,
  0xbc92434322f4f9aaULL,0x3feebd829fde4e50ULL,0xbc85ca6cd7668e4bULL,0x3feec0f170ca07baULL,
  0x3c71affc2b91ce27ULL,0x3feec49182a3f090ULL,0x3c6dd235e10a73bbULL,0x3feec86319e32323ULL,
  0xbc87c50422622263ULL,0x3feecc667b5de565ULL,0x3c8b1c86e3e231d5ULL,0x3feed09bec4a2d33ULL,
  0xbc91bbd1d3bcbb15ULL,0x3feed503b23e255dULL,0x3c90cc319cee31d2ULL,0x3feed99e1330b358ULL,
  0x3c8469846e735ab3ULL,0x3feede6b5579fdbfULL,0xbc82dfcd978e9db4ULL,0x3feee36bbfd3f37aULL,
  0x3c8c1a7792cb3387ULL,0x3feee89f995ad3adULL,0xbc907b8f4ad1d9faULL,0x3feeee07298db666ULL,
  0xbc55c3d956dcaebaULL,0x3feef3a2b84f15fbULL,0xbc90a40e3da6f640ULL,0x3feef9728de5593aULL,
  0xbc68d6f438ad9334ULL,0x3feeff76f2fb5e47ULL,0xbc91eee26b588a35ULL,0x3fef05b030a1064aULL,
  0x3c74ffd70a5fddcdULL,0x3fef0c1e904bc1d2ULL,0xbc91bdfbfa9298acULL,0x3fef12c25bd71e09ULL,
  0x3c736eae30af0cb3ULL,0x3fef199bdd85529cULL,0x3c8ee3325c9ffd94ULL,0x3fef20ab5fffd07aULL,
  0x3c84e08fd10959acULL,0x3fef27f12e57d14bULL,0x3c63cdaf384e1a67ULL,0x3fef2f6d9406e7b5ULL,
  0x3c676b2c6c921968ULL,0x3fef3720dcef9069ULL,0xbc808a1883ccb5d2ULL,0x3fef3f0b555dc3faULL,
  0xbc8fad5d3ffffa6fULL,0x3fef472d4a07897cULL,0xbc900dae3875a949ULL,0x3fef4f87080d89f2ULL,
  0x3c74a385a63d07a7ULL,0x3fef5818dcfba487ULL,0xbc82919e2040220fULL,0x3fef60e316c98398ULL,
  0x3c8e5a50d5c192acULL,0x3fef69e603db3285ULL,0x3c843a59ac016b4bULL,0x3fef7321f301b460ULL,
  0xbc82d52107b43e1fULL,0x3fef7c97337b9b5fULL,0xbc892ab93b470dc9ULL,0x3fef864614f5a129ULL,
  0x3c74b604603a88d3ULL,0x3fef902ee78b3ff6ULL,0x3c83c5ec519d7271ULL,0x3fef9a51fbc74c83ULL,
  0xbc8ff7128fd391f0ULL,0x3fefa4afa2a490daULL,0xbc8dae98e223747dULL,0x3fefaf482d8e67f1ULL,
  0x3c8ec3bc41aa2008ULL,0x3fefba1bee615a27ULL,0x3c842b94c3a9eb32ULL,0x3fefc52b376bba97ULL,
  0x3c8a64a931d185eeULL,0x3fefd0765b6e4540ULL,0xbc8e37bae43be3edULL,0x3fefdbfdad9cbe14ULL,
  0x3c77893b4d91cd9dULL,0x3fefe7c1819e90d8ULL,0x3c5305c14160cc89ULL,0x3feff3c22b8f71f1ULL
};
// log constants (N=128, poly orders 6 and 12, no-FMA path)
static const uint64_t LOG_LN2HI=0x3fe62e42fefa3800ULL, LOG_LN2LO=0x3d2ef35793c76730ULL;
static const uint64_t LOG_A[5]={0xbfe0000000000001ULL,0x3fd555555551305bULL,0xbfcfffffffeb4590ULL,0x3fc999b324f10111ULL,0xbfc55575e506c89fULL};
static const uint64_t LOG_B[11]={
  0xbfe0000000000000ULL,0x3fd5555555555577ULL,0xbfcffffffffffdcbULL,0x3fc999999995dd0cULL,
  0xbfc55555556745a7ULL,0x3fc24924a344de30ULL,0xbfbfffffa4423d65ULL,0x3fbc7184282ad6caULL,
  0xbfb999eb43b068ffULL,0x3fb78182f7afd085ULL,0xbfb5521375d145cdULL};
static const uint64_t LOG_TAB[512]={ // invc, logc, chi, clo
  0x3ff734f0c3e0de9fULL,0xbfd7cc7f79e69000ULL,0x3fe61000014fb66bULL,0x3c7e026c91425b3cULL,
  0x3ff713786a2ce91fULL,0xbfd76feec20d0000ULL,0x3fe63000034db495ULL,0x3c8dbfea48005d41ULL,
  0x3ff6f26008fab5a0ULL,0xbfd713e31351e000ULL,0x3fe650000d94d478ULL,0x3c8e7fa786d6a5b7ULL,
  0x3ff6d1a61f138c7dULL,0xbfd6b85b38287800ULL,0x3fe67000074e6fadULL,0x3c61fcea6b54254cULL,
  0x3ff6b1490bc5b4d1ULL,0xbfd65d5590807800ULL,0x3fe68ffffedf0faeULL,0xbc7c7e274c590efdULL,
  0x3ff69147332f0cbaULL,0xbfd602d076180000ULL,0x3fe6b0000763c5bcULL,0xbc8ac16848dcda01ULL,
  0x3ff6719f18224223ULL,0xbfd5a8ca86909000ULL,0x3fe6d0001e5cc1f6ULL,0x3c833f1c9d499311ULL,
  0x3ff6524f99a51ed9ULL,0xbfd54f4356035000ULL,0x3fe6efffeb05f63eULL,0xbc7e80041ae22d53ULL,
  0x3ff63356aa8f24c4ULL,0xbfd4f637c36b4000ULL,0x3fe710000e869780ULL,0x3c7bff6671097952ULL,
  0x3ff614b36b9ddc14ULL,0xbfd49da7fda85000ULL,0x3fe72ffffc67e912ULL,0x3c8c00e226bd8724ULL,
  0x3ff5f66452c65c4cULL,0xbfd445923989a800ULL,0x3fe74fffdf81116aULL,0xbc6e02916ef101d2ULL,
  0x3ff5d867b5912c4fULL,0xbfd3edf439b0b800ULL,0x3fe770000f679c90ULL,0xbc67fc71cd549c74ULL,
  0x3ff5babccb5b90deULL,0xbfd396ce448f7000ULL,0x3fe78ffffa7ec835ULL,0x3c81bec19ef50483ULL,
  0x3ff59d61f2d91a78ULL,0xbfd3401e17bda000ULL,0x3fe7affffe20c2e6ULL,0xbc707e1729cc6465ULL,
  0x3ff5805612465687ULL,0xbfd2e9e2ef468000ULL,0x3fe7cfffed3fc900ULL,0xbc808072087b8b1cULL,
  0x3ff56397cee76bd3ULL,0xbfd2941b3830e000ULL,0x3fe7efffe9261a76ULL,0x3c8dc0286d9df9aeULL,
  0x3ff54725e2a77f93ULL,0xbfd23ec58cda8800ULL,0x3fe81000049ca3e8ULL,0x3c897fd251e54c33ULL,
  0x3ff52aff42064583ULL,0xbfd1e9e129279000ULL,0x3fe8300017932c8fULL,0xbc8afee9b630f381ULL,
  0x3ff50f22dbb2bddfULL,0xbfd1956d2b48f800ULL,0x3fe850000633739cULL,0x3c89bfbf6b6535bcULL,
  0x3ff4f38f4734ded7ULL,0xbfd141679ab9f800ULL,0x3fe87000204289c6ULL,0xbc8bbf65f3117b75ULL,
  0x3ff4d843cfde2840ULL,0xbfd0edd094ef9800ULL,0x3fe88fffebf57904ULL,0xbc89006ea23dcb57ULL,
  0x3ff4bd3ec078a3c8ULL,0xbfd09aa518db1000ULL,0x3fe8b00022bc04dfULL,0xbc7d00df38e04b0aULL,
  0x3ff4a27fc3e0258aULL,0xbfd047e65263b800ULL,0x3fe8cfffe50c1b8aULL,0xbc88007146ff9f05ULL,
  0x3ff4880524d48434ULL,0xbfcfeb224586f000ULL,0x3fe8effffc918e43ULL,0x3c83817bd07a7038ULL,
  0x3ff46dce1b192d0bULL,0xbfcf474a7517b000ULL,0x3fe910001efa5fc7ULL,0x3c893e9176dfb403ULL,
  0x3ff453d9d3391854ULL,0xbfcea4443d103000ULL,0x3fe9300013467bb9ULL,0x3c7f804e4b980276ULL,
  0x3ff43a2744b4845aULL,0xbfce020d44e9b000ULL,0x3fe94fffe6ee076fULL,0xbc8f7ef0d9ff622eULL,
  0x3ff420b54115f8fbULL,0xbfcd60a22977f000ULL,0x3fe96fffde3c12d1ULL,0xbc7082aa962638baULL,
  0x3ff40782da3ef4b1ULL,0xbfccc00104959000ULL,0x3fe98ffff4458a0dULL,0xbc87801b9164a8efULL,
  0x3ff3ee8f5d57fe8fULL,0xbfcc202956891000ULL,0x3fe9afffdd982e3eULL,0xbc8740e08a5a9337ULL,
  0x3ff3d5d9a00b4ce9ULL,0xbfcb81178d811000ULL,0x3fe9cfffed49fb66ULL,0x3c3fce08c19be000ULL,
  0x3ff3bd60c010c12bULL,0xbfcae2c9ccd3d000ULL,0x3fe9f00020f19c51ULL,0xbc8a3faa27885b0aULL,
  0x3ff3a5242b75dab8ULL,0xbfca45402e129000ULL,0x3fea10001145b006ULL,0x3c74ff489958da56ULL,
  0x3ff38d22cd9fd002ULL,0xbfc9a877681df000ULL,0x3fea300007bbf6faULL,0x3c8cbeab8a2b6d18ULL,
  0x3ff3755bc5847a1cULL,0xbfc90c6d69483000ULL,0x3fea500010971d79ULL,0x3c88fecadd787930ULL,
  0x3ff35dce49ad36e2ULL,0xbfc87120a645c000ULL,0x3fea70001df52e48ULL,0xbc8f41763dd8abdbULL,
  0x3ff34679984dd440ULL,0xbfc7d68fb4143000ULL,0x3fea90001c593352ULL,0xbc8ebf0284c27612ULL,
  0x3ff32f5cceffcb24ULL,0xbfc73cb83c627000ULL,0x3feab0002a4f3e4bULL,0xbc69fd043cff3f5fULL,
  0x3ff3187775a10d49ULL,0xbfc6a39a9b376000ULL,0x3feacfffd7ae1ed1ULL,0xbc823ee7129070b4ULL,
  0x3ff301c8373e3990ULL,0xbfc60b3154b7a000ULL,0x3feaefffee510478ULL,0x3c6a063ee00edea3ULL,
  0x3ff2eb4ebb95f841ULL,0xbfc5737d76243000ULL,0x3feb0fffdb650d5bULL,0x3c5a06c8381f0ab9ULL,
  0x3ff2d50a0219a9d1ULL,0xbfc4dc7b8fc23000ULL,0x3feb2ffffeaaca57ULL,0xbc79011e74233c1dULL,
  0x3ff2bef9a8b7fd2aULL,0xbfc4462c51d20000ULL,0x3feb4fffd995badcULL,0xbc79ff1068862a9fULL,
  0x3ff2a91c7a0c1babULL,0xbfc3b08abc830000ULL,0x3feb7000249e659cULL,0x3c8aff45d0864f3eULL,
  0x3ff293726014b530ULL,0xbfc31b996b490000ULL,0x3feb8ffff9871640ULL,0x3c7cfe7796c2c3f9ULL,
  0x3ff27dfa5757a1f5ULL,0xbfc2875490a44000ULL,0x3febafffd204cb4fULL,0xbc63ff27eef22bc4ULL,
  0x3ff268b39b1d3bbfULL,0xbfc1f3b9f879a000ULL,0x3febcfffd2415c45ULL,0xbc6cffb7ee3bea21ULL,
  0x3ff2539d838ff5bdULL,0xbfc160c8252ca000ULL,0x3febeffff86309dfULL,0xbc814103972e0b5cULL,
  0x3ff23eb7aac9083bULL,0xbfc0ce7f57f72000ULL,0x3fec0fffe1b57653ULL,0x3c8bc16494b76a19ULL,
  0x3ff22a012ba940b6ULL,0xbfc03cdc49fea000ULL,0x3fec2ffff1fa57e3ULL,0xbc64feef8d30c6edULL,
  0x3ff2157996cc4132ULL,0xbfbf57bdbc4b8000ULL,0x3fec4fffdcbfe424ULL,0xbc843f68bcec4775ULL,
  0x3ff201201dd2fc9bULL,0xbfbe370896404000ULL,0x3fec6fffed54b9f7ULL,0x3c847ea3f053e0ecULL,
  0x3ff1ecf4494d480bULL,0xbfbd17983ef94000ULL,0x3fec8fffeb998fd5ULL,0x3c7383068df992f1ULL,
  0x3ff1d8f5528f6569ULL,0xbfbbf9674ed8a000ULL,0x3fecb0002125219aULL,0xbc68fd8e64180e04ULL,
  0x3ff1c52311577e7cULL,0xbfbadc79202f6000ULL,0x3feccfffdd94469cULL,0x3c8e7ebe1cc7ea72ULL,
  0x3ff1b17c74cb26e9ULL,0xbfb9c0c3e7288000ULL,0x3fecefffeafdc476ULL,0x3c8ebe39ad9f88feULL,
  0x3ff19e010c2c1ab6ULL,0xbfb8a646b372c000ULL,0x3fed1000169af82bULL,0x3c757d91a8b95a71ULL,
  0x3ff18ab07bb670bdULL,0xbfb78d01b3ac0000ULL,0x3fed30000d0ff71dULL,0x3c89c1906970c7daULL,
  0x3ff1778a25efbcb6ULL,0xbfb674f145380000ULL,0x3fed4fffea790fc4ULL,0xbc580e37c558fe0cULL,
  0x3ff1648d354c31daULL,0xbfb55e0e6d878000ULL,0x3fed70002edc87e5ULL,0xbc7f80d64dc10f44ULL,
  0x3ff151b990275fddULL,0xbfb4485cdea1e000ULL,0x3fed900021dc82aaULL,0xbc747c8f94fd5c5cULL,
  0x3ff13f0ea432d24cULL,0xbfb333d94d6aa000ULL,0x3fedafffd86b0283ULL,0x3c8c7f1dc521617eULL,
  0x3ff12c8b7210f9daULL,0xbfb22079f8c56000ULL,0x3fedd000296c4739ULL,0x3c88019eb2ffb153ULL,
  0x3ff11a3028ecb531ULL,0xbfb10e4698622000ULL,0x3fedefffe54490f5ULL,0x3c6e00d2c652cc89ULL,
  0x3ff107fbda8434afULL,0xbfaffa6c6ad20000ULL,0x3fee0fffcdabf694ULL,0xbc7f8340202d69d2ULL,
  0x3ff0f5ee0f4e6bb3ULL,0xbfadda8d4a774000ULL,0x3fee2fffdb52c8ddULL,0x3c7b00c1ca1b0864ULL,
  0x3ff0e4065d2a9fceULL,0xbfabbcece4850000ULL,0x3fee4ffff24216efULL,0x3c72ffa8b094ab51ULL,
  0x3ff0d244632ca521ULL,0xbfa9a1894012c000ULL,0x3fee6fffe88a5e11ULL,0xbc57f673b1efbe59ULL,
  0x3ff0c0a77ce2981aULL,0xbfa788583302c000ULL,0x3fee9000119eff0dULL,0xbc84808d5e0bc801ULL,
  0x3ff0af2f83c636d1ULL,0xbfa5715e67d68000ULL,0x3feeafffdfa51744ULL,0x3c780006d54320b5ULL,
  0x3ff09ddb98a01339ULL,0xbfa35c8a49658000ULL,0x3feed0001a127fa1ULL,0xbc5002f860565c92ULL,
  0x3ff08cabaf52e7dfULL,0xbfa149e364154000ULL,0x3feef00007babcc4ULL,0xbc8540445d35e611ULL,
  0x3ff07b9f2f4e28fbULL,0xbf9e72c082eb8000ULL,0x3fef0ffff57a8d02ULL,0xbc4ffb3139ef9105ULL,
  0x3ff06ab58c358f19ULL,0xbf9a55f152528000ULL,0x3fef30001ee58ac7ULL,0x3c8a81acf2731155ULL,
  0x3ff059eea5ecf92cULL,0xbf963d62cf818000ULL,0x3fef4ffff5823494ULL,0x3c8a3f41d4d7c743ULL,
  0x3ff04949cdd12c90ULL,0xbf9228fb8caa0000ULL,0x3fef6ffffca94c6bULL,0xbc6202f41c987875ULL,
  0x3ff038c6c6f0ada9ULL,0xbf8c317b20f90000ULL,0x3fef8fffe1f9c441ULL,0x3c777dd1f477e74bULL,
  0x3ff02865137932a9ULL,0xbf8419355daa0000ULL,0x3fefafffd2e0e37eULL,0xbc6f01199a7ca331ULL,
  0x3ff0182427ea7348ULL,0xbf781203c2ec0000ULL,0x3fefd0001c77e49eULL,0x3c7181ee4bceacb1ULL,
  0x3ff008040614b195ULL,0xbf60040979240000ULL,0x3fefeffff7e0c331ULL,0xbc6e05370170875aULL,
  0x3fefe01ff726fa1aULL,0x3f6feff384900000ULL,0x3ff00ffff465606eULL,0xbc8a7ead491c0adaULL,
  0x3fefa11cc261ea74ULL,0x3f87dc41353d0000ULL,0x3ff02ffff3867a58ULL,0xbc977f69c3fcb2e0ULL,
  0x3fef6310b081992eULL,0x3f93cea3c4c28000ULL,0x3ff04ffffdfc0d17ULL,0x3c97bffe34cb945bULL,
  0x3fef25f63ceeadcdULL,0x3f9b9fc114890000ULL,0x3ff0700003cd4d82ULL,0x3c820083c0e456cbULL,
  0x3feee9c8039113e7ULL,0x3fa1b0d8ce110000ULL,0x3ff08ffff9f2cbe8ULL,0xbc6dffdfbe37751aULL,
  0x3feeae8078cbb1abULL,0x3fa58a5bd001c000ULL,0x3ff0b000010cda65ULL,0xbc913f7faee626ebULL,
  0x3fee741aa29d0c9bULL,0x3fa95c8340d88000ULL,0x3ff0d00001a4d338ULL,0x3c807dfa79489ff7ULL,
  0x3fee3a91830a99b5ULL,0x3fad276aef578000ULL,0x3ff0effffadafdfdULL,0xbc77040570d66bc0ULL,
  0x3fee01e009609a56ULL,0x3fb07598e598c000ULL,0x3ff110000bbafd96ULL,0x3c8e80d4846d0b62ULL,
  0x3fedca01e577bb98ULL,0x3fb253f5e30d2000ULL,0x3ff12ffffae5f45dULL,0x3c9dbffa64fd36efULL,
  0x3fed92f20b7c9103ULL,0x3fb42edd8b380000ULL,0x3ff150000dd59ad9ULL,0x3c9a0077701250aeULL,
  0x3fed5cac66fb5cceULL,0x3fb606598757c000ULL,0x3ff170000f21559aULL,0x3c8dfdf9e2e3deeeULL,
  0x3fed272caa5ede9dULL,0x3fb7da76356a0000ULL,0x3ff18ffffc275426ULL,0x3c910030dc3b7273ULL,
  0x3fecf26e3e6b2ccdULL,0x3fb9ab434e1c6000ULL,0x3ff1b000123d3c59ULL,0x3c997f7980030188ULL,
  0x3fecbe6da2a77902ULL,0x3fbb78c7bb0d6000ULL,0x3ff1cffff8299eb7ULL,0xbc65f932ab9f8c67ULL,
  0x3fec8b266d37086dULL,0x3fbd431332e72000ULL,0x3ff1effff48ad400ULL,0x3c937fbf9da75bebULL,
  0x3fec5894bd5d5804ULL,0x3fbf0a3171de6000ULL,0x3ff210000c8b86a4ULL,0x3c9f806b91fd5b22ULL,
  0x3fec26b533bb9f8cULL,0x3fc067152b914000ULL,0x3ff2300003854303ULL,0x3c93ffc2eb9fbf33ULL,
  0x3febf583eeece73fULL,0x3fc147858292b000ULL,0x3ff24fffffbcf684ULL,0x3c7601e77e2e2e72ULL,
  0x3febc4fd75db96c1ULL,0x3fc2266ecdca3000ULL,0x3ff26ffff52921d9ULL,0x3c7ffcbb767f0c61ULL,
  0x3feb951e0c864a28ULL,0x3fc303d7a6c55000ULL,0x3ff2900014933a3cULL,0xbc7202ca3c02412bULL,
  0x3feb65e2c5ef3e2cULL,0x3fc3dfc33c331000ULL,0x3ff2b00014556313ULL,0xbc92808233f21f02ULL,
  0x3feb374867c9888bULL,0x3fc4ba366b7a8000ULL,0x3ff2cfffebfe523bULL,0xbc88ff7e384fdcf2ULL,
  0x3feb094b211d304aULL,0x3fc5933928d1f000ULL,0x3ff2f0000bb8ad96ULL,0xbc85ff51503041c5ULL,
  0x3feadbe885f2ef7eULL,0x3fc66acd2418f000ULL,0x3ff30ffffb7ae2afULL,0xbc810071885e289dULL,
  0x3feaaf1d31603da2ULL,0x3fc740f8ec669000ULL,0x3ff32ffffeac5f7fULL,0xbc91ff5d3fb7b715ULL,
  0x3fea82e63fd358a7ULL,0x3fc815c0f51af000ULL,0x3ff350000ca66756ULL,0x3c957f82228b82bdULL,
  0x3fea5740ef09738bULL,0x3fc8e92954f68000ULL,0x3ff3700011fbf721ULL,0x3c8000bac40dd5ccULL,
  0x3fea2c2a90ab4b27ULL,0x3fc9bb3602f84000ULL,0x3ff38ffff9592fb9ULL,0xbc943f9d2db2a751ULL,
  0x3fea01a01393f2d1ULL,0x3fca8bed1c2c0000ULL,0x3ff3b00004ddd242ULL,0x3c857f6b707638e1ULL,
  0x3fe9d79f24db3c1bULL,0x3fcb5b515c01d000ULL,0x3ff3cffff5b2c957ULL,0x3c7a023a10bf1231ULL,
  0x3fe9ae2505c7b190ULL,0x3fcc2967ccbcc000ULL,0x3ff3efffeab0b418ULL,0x3c987f6d66b152b0ULL,
  0x3fe9852ef297ce2fULL,0x3fccf635d5486000ULL,0x3ff410001532aff4ULL,0x3c67f8375f198524ULL,
  0x3fe95cbaeea44b75ULL,0x3fcdc1bd3446c000ULL,0x3ff4300017478b29ULL,0x3c8301e672dc5143ULL,
  0x3fe934c69de74838ULL,0x3fce8c01b8cfe000ULL,0x3ff44fffe795b463ULL,0x3c89ff69b8b2895aULL,
  0x3fe90d4f2f6752e6ULL,0x3fcf5509c0179000ULL,0x3ff46fffe80475e0ULL,0xbc95c0b19bc2f254ULL,
  0x3fe8e6528effd79dULL,0x3fd00e6c121fb800ULL,0x3ff48fffef6fc1e7ULL,0x3c9b4009f23a2a72ULL,
  0x3fe8bfce9fcc007cULL,0x3fd071b80e93d000ULL,0x3ff4afffe5bea704ULL,0xbc94ffb7bf0d7d45ULL,
  0x3fe899c0dabec30eULL,0x3fd0d46b9e867000ULL,0x3ff4d000171027deULL,0xbc99c06471dc6a3dULL,
  0x3fe87427aa2317fbULL,0x3fd13687334bd000ULL,0x3ff4f0000ff03ee2ULL,0x3c977f890b85531cULL,
  0x3fe84f00acb39a08ULL,0x3fd1980d67234800ULL,0x3ff5100012dc4bd1ULL,0x3c6004657166a436ULL,
  0x3fe82a49e8653e55ULL,0x3fd1f8ffe0cc8000ULL,0x3ff530001605277aULL,0xbc96bfcece233209ULL,
  0x3fe8060195f40260ULL,0x3fd2595fd7636800ULL,0x3ff54fffecdb704cULL,0xbc8902720505a1d7ULL,
  0x3fe7e22563e0a329ULL,0x3fd2b9300914a800ULL,0x3ff56fffef5f54a9ULL,0x3c9bbfe60ec96412ULL,
  0x3fe7beb377dcb5adULL,0x3fd3187210436000ULL,0x3ff5900017e61012ULL,0x3c887ec581afef90ULL,
  0x3fe79baa679725c2ULL,0x3fd377266dec1800ULL,0x3ff5b00003c93e92ULL,0xbc9f41080abf0cc0ULL,
  0x3fe77907f2170657ULL,0x3fd3d54ffbaf3000ULL,0x3ff5d0001d4919bcULL,0xbc98812afb254729ULL,
  0x3fe756cadbd6130cULL,0x3fd432eee32fe000ULL,0x3ff5efffe7b87a89ULL,0xbc947eb780ed6904ULL
};

static inline uint64_t as_u64(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline double as_f64(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

// e^x, ported from Arm Optimized Routines (128-entry table, no-FMA path).
// The coder only ever needs |x| <= 20.
static double hcm_exp(double x) {
    uint32_t abstop = (uint32_t)(as_u64(x) >> 52) & 0x7ff;
    if (abstop < 0x3c9)  // |x| < 2^-54
        return 1.0 + x;
    double z = as_f64(EXP_INVLN2N) * x;
    double kd = z + as_f64(EXP_SHIFT);
    uint64_t ki = as_u64(kd);
    kd -= as_f64(EXP_SHIFT);
    double r = x + kd * as_f64(EXP_NEGLN2HIN) + kd * as_f64(EXP_NEGLN2LON);
    uint64_t idx = 2 * (ki % 128);
    uint64_t top = ki << (52 - 7);
    double tail = as_f64(EXP_TAB[idx]);
    uint64_t sbits = EXP_TAB[idx + 1] + top;
    double r2 = r * r;
    double tmp = tail + r
               + r2 * (as_f64(EXP_POLY[0]) + r * as_f64(EXP_POLY[1]))
               + r2 * r2 * (as_f64(EXP_POLY[2]) + r * as_f64(EXP_POLY[3]));
    double scale = as_f64(sbits);
    return scale + scale * tmp;
}

// Natural log, ported from Arm Optimized Routines (128-entry table, no-FMA
// path). The coder only ever needs positive, normal inputs.
static double hcm_log(double x) {
    uint64_t ix = as_u64(x);
    const uint64_t LO = 0x3fee000000000000ULL;  // 1 - 2^-4
    const uint64_t HI = 0x3ff1090000000000ULL;  // 1 + 0x1.09p-4
    if (ix - LO < HI - LO) {
        if (ix == 0x3ff0000000000000ULL) return 0;
        double B[11];
        for (int i = 0; i < 11; i++) B[i] = as_f64(LOG_B[i]);
        double r = x - 1.0, r2 = r * r, r3 = r * r2;
        double y = r3 * (B[1] + r * B[2] + r2 * B[3]
                   + r3 * (B[4] + r * B[5] + r2 * B[6]
                   + r3 * (B[7] + r * B[8] + r2 * B[9] + r3 * B[10])));
        double w = r * 0x1p27;
        double rhi = r + w - w;
        double rlo = r - rhi;
        w = rhi * rhi * B[0];
        double hi = r + w;
        double lo = r - hi + w;
        lo += B[0] * rlo * (rhi + r);
        y += lo;
        y += hi;
        return y;
    }
    const uint64_t OFF = 0x3fe6000000000000ULL;
    uint64_t tmp = ix - OFF;
    int i = (int)((tmp >> (52 - 7)) % 128);
    int k = (int)((int64_t)tmp >> 52);
    uint64_t iz = ix - (tmp & (0xfffULL << 52));
    double invc = as_f64(LOG_TAB[4 * i]), logc = as_f64(LOG_TAB[4 * i + 1]);
    double chi = as_f64(LOG_TAB[4 * i + 2]), clo = as_f64(LOG_TAB[4 * i + 3]);
    double z = as_f64(iz);
    double r = (z - chi - clo) * invc;
    double kd = (double)k;
    double w = kd * as_f64(LOG_LN2HI) + logc;
    double hi = w + r;
    double lo = w - hi + r + kd * as_f64(LOG_LN2LO);
    double r2 = r * r;
    double A0 = as_f64(LOG_A[0]), A1 = as_f64(LOG_A[1]), A2 = as_f64(LOG_A[2]),
           A3 = as_f64(LOG_A[3]), A4 = as_f64(LOG_A[4]);
    return lo + r2 * A0 + r * r2 * (A1 + r * A2 + r2 * (A3 + r * A4)) + hi;
}

// ========================================================================
// HCM v2 engine, kept so that v2 files can still be restored.
// ========================================================================
namespace v2 {
#define HCM_EXP(x) hcm_exp(x)
#define HCM_LOG(x) hcm_log(x)

// ------------------------------------------------------------------------
// Shared tables

static double ST[4096];  // stretch: ln(p / (1 - p)) for 12-bit p
static int RT[256];      // adaptation rate by count: 65536 / (n + 1.6)

static double squash(double x) {
    if (x > 20) x = 20;
    else if (x < -20) x = -20;
    return 4096.0 / (1.0 + HCM_EXP(-x));
}

static void init_tables(void) {
    for (int p = 1; p < 4096; p++)
        ST[p] = HCM_LOG((double)p / (double)(4096 - p));
    ST[0] = ST[1];
    for (int n = 0; n < 256; n++)
        RT[n] = (int)(65536.0 / ((double)n + 1.6));
}

// ------------------------------------------------------------------------
// Bit output and input (most significant bit first)

struct BitWriter {
    uint8_t *data;
    size_t size, cap;
    uint8_t cur;
    int nbits;
    int failed;
};

static void bw_init(BitWriter *w, size_t guess) {
    w->cap = guess < 64 ? 64 : guess;
    w->data = (uint8_t *)malloc(w->cap);
    w->size = 0; w->cur = 0; w->nbits = 0;
    w->failed = (w->data == NULL);
}

static void bw_bit(BitWriter *w, int bit) {
    w->cur = (uint8_t)((w->cur << 1) | bit);
    if (++w->nbits == 8) {
        if (w->size == w->cap && !w->failed) {
            size_t ncap = w->cap * 2;
            uint8_t *nd = (uint8_t *)realloc(w->data, ncap);
            if (nd) { w->data = nd; w->cap = ncap; } else w->failed = 1;
        }
        if (!w->failed) w->data[w->size++] = w->cur;
        w->cur = 0; w->nbits = 0;
    }
}

static void bw_put(BitWriter *w, uint32_t v, int n) {
    for (int k = n - 1; k >= 0; k--) bw_bit(w, (v >> k) & 1);
}

static void bw_flush(BitWriter *w) {  // pad the last byte with zeros
    while (w->nbits != 0) bw_bit(w, 0);
}

struct BitReader {
    const uint8_t *data;
    uint64_t nbits, pos;
};

static int br_bit(BitReader *r) {  // reads past the end as 0
    int bit = 0;
    if (r->pos < r->nbits)
        bit = (r->data[r->pos >> 3] >> (7 - (r->pos & 7))) & 1;
    r->pos++;
    return bit;
}

static uint32_t br_get(BitReader *r, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | (uint32_t)br_bit(r);
    return v;
}

// ------------------------------------------------------------------------
// 32-bit binary arithmetic coder (Witten, Neal and Cleary)

static const uint64_t TOP = 0xFFFFFFFFULL;
static const uint64_t HALF = 1ULL << 31;
static const uint64_t Q1 = 1ULL << 30;
static const uint64_t Q3 = 3ULL << 30;

struct Encoder {
    uint64_t lo, hi, pend;
    BitWriter *w;
};

static void enc_out(Encoder *e, int bit) {
    bw_bit(e->w, bit);
    for (; e->pend > 0; e->pend--) bw_bit(e->w, 1 - bit);
}

static void enc_code(Encoder *e, uint64_t cl, uint64_t ch, uint64_t tot) {
    uint64_t rng = e->hi - e->lo + 1;
    e->hi = e->lo + rng * ch / tot - 1;
    e->lo = e->lo + rng * cl / tot;
    for (;;) {
        if (e->hi < HALF) {
            enc_out(e, 0);
        } else if (e->lo >= HALF) {
            enc_out(e, 1);
            e->lo -= HALF; e->hi -= HALF;
        } else if (e->lo >= Q1 && e->hi < Q3) {
            e->pend++;
            e->lo -= Q1; e->hi -= Q1;
        } else {
            break;
        }
        e->lo <<= 1;
        e->hi = (e->hi << 1) | 1;
    }
}

static void enc_finish(Encoder *e) {
    e->pend++;
    enc_out(e, e->lo < Q1 ? 0 : 1);
}

struct Decoder {
    uint64_t lo, hi, v;
    BitReader *r;
};

static void dec_init(Decoder *d, BitReader *r) {
    d->lo = 0; d->hi = TOP; d->r = r;
    d->v = br_get(r, 32);
}

static uint64_t dec_target(Decoder *d, uint64_t tot) {
    uint64_t rng = d->hi - d->lo + 1;
    return ((d->v - d->lo + 1) * tot - 1) / rng;
}

static void dec_consume(Decoder *d, uint64_t cl, uint64_t ch, uint64_t tot) {
    uint64_t rng = d->hi - d->lo + 1;
    d->hi = d->lo + rng * ch / tot - 1;
    d->lo = d->lo + rng * cl / tot;
    for (;;) {
        if (d->hi < HALF) {
            // nothing to subtract
        } else if (d->lo >= HALF) {
            d->lo -= HALF; d->hi -= HALF; d->v -= HALF;
        } else if (d->lo >= Q1 && d->hi < Q3) {
            d->lo -= Q1; d->hi -= Q1; d->v -= Q1;
        } else {
            break;
        }
        d->lo <<= 1;
        d->hi = (d->hi << 1) | 1;
        d->v = (d->v << 1) | (uint64_t)br_bit(d->r);
    }
}

// ------------------------------------------------------------------------
// The model

enum {
    NORD = 6,          // hashed byte contexts
    NM = NORD + 3,     // + order 0, word, word pair
    NIN = NM + 2,      // + match input, bias
    TBITS = 22,
    APM_ROWS = 256 * 256,
    MTAB_BITS = 20
};
static const int ORDERS[NORD] = {1, 2, 3, 4, 6, 8};
static const uint32_t MASK = (1u << TBITS) - 1;
static const int LIMIT_LO = 60, LIMIT_HI = 255;
static const double APM_W = 0.9;
static const double LR = 0.008;

struct Model {
    uint16_t *prob[NM];
    uint8_t *cnt[NM];
    int limits[NM];
    double (*w)[NIN];          // 256 partial bytes x 4 match buckets
    int32_t *apm;              // APM_ROWS rows of 33 entries
    uint8_t *buf;              // everything seen so far
    size_t n;
    uint32_t c0;
    int bitpos;
    uint32_t h[NM];
    uint32_t word, pword;
    int32_t *mtab;
    int64_t mptr, mlen;
    uint16_t mm[64];
    int last;
    // per-bit state shared by predict() and update()
    uint32_t idx[NM];
    double st[NIN];
    int mexp, mk;
    double *wsel;
    double pm, af;
    int aj;
    int32_t *arow;
};

static void model_free(Model *m) {
    for (int i = 0; i < NM; i++) { free(m->prob[i]); free(m->cnt[i]); }
    free(m->w); free(m->apm); free(m->buf); free(m->mtab);
    memset(m, 0, sizeof(*m));
}

static void ctx_byte(Model *m) {
    const uint8_t *b = m->buf;
    size_t n = m->n;
    for (int k = 0; k < NORD; k++) {
        int o = ORDERS[k];
        uint32_t x = (uint32_t)(o + 1) * 0x9E3779B1u;
        for (int j = 1; j <= o; j++) {
            uint32_t c = n >= (size_t)j ? b[n - j] : 0;
            x = (x ^ c) * 0x01000193u + (uint32_t)j;
        }
        m->h[k] = x * 0x2545F491u;
    }
    m->h[NORD] = 0;  // order 0
    m->h[NORD + 1] = m->word * 0x6F4F2A35u + 0x51ED27u;
    m->h[NORD + 2] = (m->pword * 0x3B9AC9FBu) ^ (m->word * 0x6F4F2A35u) ^ 0x7A3D1u;
    m->last = n ? b[n - 1] : 0;
}

// Returns 0 on success, -1 if memory ran out.
static int model_init(Model *m, size_t capacity) {
    memset(m, 0, sizeof(*m));
    size_t size = (size_t)1 << TBITS;
    for (int i = 0; i < NM; i++) {
        m->prob[i] = (uint16_t *)malloc(size * sizeof(uint16_t));
        m->cnt[i] = (uint8_t *)calloc(size, 1);
        if (!m->prob[i] || !m->cnt[i]) { model_free(m); return -1; }
        for (size_t k = 0; k < size; k++) m->prob[i][k] = 32768;
        m->limits[i] = i == 0 ? LIMIT_LO : LIMIT_HI;
    }
    m->w = (double (*)[NIN])malloc(sizeof(double) * NIN * 256 * 4);
    m->apm = (int32_t *)malloc(sizeof(int32_t) * 33 * APM_ROWS);
    m->buf = (uint8_t *)malloc(capacity ? capacity : 1);
    m->mtab = (int32_t *)calloc((size_t)1 << MTAB_BITS, sizeof(int32_t));
    if (!m->w || !m->apm || !m->buf || !m->mtab) { model_free(m); return -1; }
    for (int r = 0; r < 256 * 4; r++)
        for (int i = 0; i < NIN; i++) m->w[r][i] = 0.3;
    int32_t row0[33];
    for (int j = 0; j < 33; j++)
        row0[j] = (int32_t)(squash((j - 16) / 2.0) * 16);
    for (int r = 0; r < APM_ROWS; r++)
        memcpy(m->apm + (size_t)r * 33, row0, sizeof(row0));
    m->n = 0;
    m->c0 = 1;
    m->bitpos = 0;
    m->word = m->pword = 0;
    m->mptr = m->mlen = 0;
    for (int i = 0; i < 64; i++) m->mm[i] = 49152;
    ctx_byte(m);
    return 0;
}

// Probability (1..4095, out of 4096) that the next bit is a 1.
static int predict(Model *m) {
    uint32_t c0 = m->c0;
    uint32_t cm = c0 * 0x9E3779B1u;
    for (int i = 0; i < NM; i++) m->idx[i] = (m->h[i] ^ cm) & MASK;
    m->idx[NORD] = c0;
    for (int i = 0; i < NM; i++) m->st[i] = ST[m->prob[i][m->idx[i]] >> 4];

    // long-match input
    m->mexp = -1;
    if (m->mlen > 0 && (size_t)m->mptr < m->n) {
        int pb = m->buf[m->mptr];
        if ((uint32_t)((pb | 256) >> (8 - m->bitpos)) == c0)
            m->mexp = (pb >> (7 - m->bitpos)) & 1;
    }
    if (m->mexp >= 0) {
        m->mk = m->mlen < 63 ? (int)m->mlen : 63;
        int pc = m->mm[m->mk] >> 4;
        m->st[NM] = m->mexp ? ST[pc] : -ST[pc];
    } else {
        m->st[NM] = 0.0;
    }
    m->st[NM + 1] = 0.25;

    int ms = m->mlen == 0 ? 0 : m->mlen < 16 ? 1 : m->mlen < 32 ? 2 : 3;
    m->wsel = m->w[c0 + 256 * ms];
    const double *ws = m->wsel;
    double dot = 0.0;
    for (int i = 0; i < NIN; i++) dot += ws[i] * m->st[i];
    m->pm = squash(dot);

    // secondary estimate indexed by previous byte and partial byte,
    // interpolated on stretch(p)
    double s = dot < 7.999 ? dot : 7.999;
    s = s > -7.999 ? s : -7.999;
    double pos = (s + 8.0) * 2.0;
    int j = (int)pos;
    double f = pos - j;
    m->aj = j;
    m->af = f;
    m->arow = m->apm + (size_t)(c0 + 256 * (uint32_t)m->last) * 33;
    double pa = (m->arow[j] * (1 - f) + m->arow[j + 1] * f) / 16.0;
    int p = (int)(m->pm * (1 - APM_W) + pa * APM_W);
    return p < 1 ? 1 : (p > 4095 ? 4095 : p);
}

static void update(Model *m, int y) {
    double err = (y - m->pm / 4096.0) * LR;
    double *ws = m->wsel;
    for (int i = 0; i < NIN; i++) ws[i] += err * m->st[i];

    int tgt = y ? 65535 : 0;
    for (int i = 0; i < NM; i++) {
        uint16_t *P = m->prob[i];
        uint8_t *C = m->cnt[i];
        uint32_t k = m->idx[i];
        int n = C[k];
        P[k] = (uint16_t)(P[k] + (((int64_t)(tgt - P[k]) * RT[n]) >> 16));
        if (n < m->limits[i]) C[k] = (uint8_t)(n + 1);
    }
    if (m->mexp >= 0) {
        int v = m->mm[m->mk];
        int hit = y == m->mexp ? 65535 : 0;
        m->mm[m->mk] = (uint16_t)(v + ((hit - v) >> 5));
    }
    int32_t *row = m->arow;
    int j = m->aj;
    int t16 = y ? 65535 : 0;
    row[j] += (int32_t)((int64_t)((t16 - row[j]) * (1 - m->af)) >> 6);
    row[j + 1] += (int32_t)((int64_t)((t16 - row[j + 1]) * m->af) >> 6);

    m->c0 = (m->c0 << 1) | (uint32_t)y;
    if (++m->bitpos == 8) {
        int byte = (int)(m->c0 & 255);
        m->buf[m->n++] = (uint8_t)byte;
        m->c0 = 1;
        m->bitpos = 0;
        // word context: letters only, case folded
        if ((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122)) {
            m->word = (m->word * 0x2C9277B5u) ^ (uint32_t)(byte | 32);
        } else {
            if (m->word) m->pword = m->word;
            m->word = 0;
        }
        // long-match bookkeeping (hash of last 6 bytes -> next position)
        int64_t n = (int64_t)m->n;
        const uint8_t *b = m->buf;
        if (m->mlen > 0 && m->mptr < n - 1 && b[m->mptr] == byte) {
            m->mptr++;
            m->mlen++;
        } else {
            m->mlen = 0;
        }
        if (n >= 6) {
            uint32_t hh = 0;
            for (int j2 = 1; j2 <= 6; j2++)
                hh = (uint32_t)(((uint64_t)hh * 0x2F0B4A3u + b[n - j2]) & 0xFFFFF);
            if (m->mlen == 0) {
                int64_t cand = m->mtab[hh];
                if (cand > 0) {
                    // verify: count how many bytes really match
                    int L = 0;
                    while (L < 32 && cand - 1 - L >= 0 && b[cand - 1 - L] == b[n - 1 - L])
                        L++;
                    if (L >= 6) { m->mptr = cand; m->mlen = L; }
                }
            }
            m->mtab[hh] = (int32_t)n;
        }
        ctx_byte(m);
    }
}

// ------------------------------------------------------------------------
// Compress and decompress whole buffers

static const size_t MAX_INPUT = 0x7FFFFFFF;  // 2 GB - 1

// Returns 0 on success, -1 if memory ran out, -2 if the file is not valid.
static int hcm_decompress(const uint8_t *blob, size_t blobn, uint8_t **out, size_t *outn) {
    BitReader r = {blob, (uint64_t)blobn * 8, 0};
    if (blobn < 4) return -2;
    uint32_t n = br_get(&r, 32);
    if (n > MAX_INPUT) return -2;
    // each bit costs at least log2(4096/4095) bits, so a real file can
    // never expand by more than about 2,840 times
    if ((uint64_t)n > ((uint64_t)blobn + 16) * 4096) return -2;
    Decoder d;
    dec_init(&d, &r);
    Model *m = (Model *)malloc(sizeof(Model));
    if (!m || model_init(m, n)) { free(m); return -1; }
    for (uint32_t i = 0; i < n; i++) {
        for (int k = 0; k < 8; k++) {
            int p = predict(m);
            uint64_t t = dec_target(&d, 4096);
            int y;
            if (t < (uint64_t)p) { dec_consume(&d, 0, (uint64_t)p, 4096); y = 1; }
            else { dec_consume(&d, (uint64_t)p, 4096, 4096); y = 0; }
            update(m, y);
        }
    }
    // A genuine v2 stream ends with the decoder 23-30 bits past the last stored bit
    // (32-bit look-ahead minus the flush). Anything else is not an HCM v2 file.
    if (r.pos < r.nbits || r.pos > r.nbits + 40) { model_free(m); return -5; }
    *out = m->buf;  // the model's history is the restored file
    *outn = m->n;
    m->buf = NULL;
    model_free(m);
    free(m);
    return 0;
}

}  // namespace v2

// ========================================================================
// HCM v3 engine
//
// Same ideas as v2 (per-context 0/1 balances, long match, learned mixer,
// refinement stage, arithmetic coder), rebuilt for speed and portability:
//   * Each context's balances for one half-byte (nibble) share a single
//     64-byte block of memory, so a byte needs 2 memory lookups per
//     context instead of 8. Slot 0 of each block holds a check value of
//     the context that owns it; a block is chosen from 2 neighbours and
//     the less-used one is recycled when a new context needs room.
//   * All arithmetic in the coding loop is integer, so every computer
//     makes identical predictions.
//   * The mixer chooses its weights by partial byte, match length and
//     whether the previous byte was a letter. Max mode adds a second
//     mixer chosen by the classes of the previous two bytes, and averages
//     the two.
// File: "HCM3", mode byte, 32-bit length, 32-bit CRC-32, then the coded
// bits.
// ========================================================================

#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define HCM_SSE2 1
#else
#define HCM_SSE2 0
#endif

namespace v3 {

enum { NORD = 6, NH = NORD + 2, NM = NH + 1, NIN = NM + 2, NINP = 12,
       APM_ROWS = 65536, MTAB_BITS = 20 };
static const int ORDERS[NORD] = {1, 2, 3, 4, 6, 8};
static const int LRSHIFT = 11, APMRATE = 13, WINIT = 19661;
// v3 settings / v3.3 settings (chosen per file by the compressor)
static const int APMW_V3 = 230, APMW_V33 = 170, MMR_V3 = 5, MMR_V33 = 6;

static int16_t STI[4096];   // stretch(p / 4096) * 256, clamped to +-2047
static int16_t SQI[4096];   // squash(x / 256) * 4096 for x = -2048..2047
static int RT15[256];       // adaptation rate by count: 32768 / (n + 1.6)
static uint8_t NEXT60[256], NEXT255[256];

static void init_tables(void) {
    for (int p = 0; p < 4096; p++) {
        double st = hcm_log((double)(p ? p : 1) / (double)(4096 - (p ? p : 1)));
        double v = st * 256.0;
        int s = (int)(v < 0 ? v - 0.5 : v + 0.5);
        if (s > 2047) s = 2047;
        if (s < -2047) s = -2047;
        STI[p] = (int16_t)s;
    }
    for (int x = -2048; x < 2048; x++) {
        double xx = x / 256.0;
        int p = (int)(4096.0 / (1.0 + hcm_exp(-xx)));
        SQI[x + 2048] = (int16_t)(p < 1 ? 1 : (p > 4095 ? 4095 : p));
    }
    for (int n = 0; n < 256; n++) {
        RT15[n] = (int)(32768.0 / ((double)n + 1.6));
        NEXT60[n] = (uint8_t)(n < 60 ? n + 1 : n);
        NEXT255[n] = (uint8_t)(n < 255 ? n + 1 : n);
    }
}

static inline int cls(int c) {
    if ((c | 32) >= 'a' && (c | 32) <= 'z') return 1;
    if (c >= '0' && c <= '9') return 2;
    if (c == ' ') return 3;
    if (c < 32 || c > 126) return 4;
    return 0;
}
static uint8_t CLS[256];   // cls() as a table

// ---- Harrington Insights (v3.3), used only when the file header says so
// Binary check: more than 1 byte in 10 is a control byte (text never has these).
static int looks_binary(const uint8_t *d, size_t n) {
    size_t ctl = 0;
    for (size_t i = 0; i < n; i++) { unsigned c = d[i]; ctl += c < 9 || (c > 13 && c < 32) || c == 127; }
    return n > 0 && ctl * 10 > n;
}

// Not clearly binary, but not plain ASCII text either: 5% or more of the bytes are >= 128.
// Such files (image rows, audio, non-English UTF-8 text) are compressed both ways.
static int looks_ambiguous(const uint8_t *d, size_t n) {
    size_t hi = 0;
    for (size_t i = 0; i < n; i++) hi += d[i] >= 128;
    return n > 0 && hi * 20 >= n;
}
// Record detection: a byte value seen at equal spacing several times in a
// row votes for that spacing; enough votes make it the record length R.
static const int RECMIN = 3;
struct RecState { int32_t lp[256][3]; int cand, cnt, R; };
static void rec_init(RecState *r) { memset(r, 0, sizeof(*r)); for (int c = 0; c < 256; c++) for (int k = 0; k < 3; k++) r->lp[c][k] = -1; }
static inline void rec_update(RecState *r, const uint8_t *b, size_t n) {
    int c = b[n - 1]; int32_t p = (int32_t)(n - 1);
    int32_t *l = r->lp[c];
    int d1 = l[0] >= 0 ? p - l[0] : 0, d2 = l[1] >= 0 ? l[0] - l[1] : -1, d3 = l[2] >= 0 ? l[1] - l[2] : -2;
    if (d1 > 1 && d1 == d2 && d1 == d3 && d1 < 65536) {
        if (d1 == r->cand) { if (r->cnt < 64) r->cnt++; }
        else if (r->cnt > 0) r->cnt--;
        else { r->cand = d1; r->cnt = 1; }
        if (r->cnt >= RECMIN && r->cand != r->R) r->R = r->cand;
    }
    l[2] = l[1]; l[1] = l[0]; l[0] = p;
}
static inline uint32_t hfin(uint32_t x, uint32_t salt) { x = (x ^ salt) * 0x2545F491u; x ^= x >> 15; x *= 0x2C1B3C6Du; return x ^ (x >> 13); }
// Binary files: once a record length is known, the two word slots and the
// order-8 slot become record contexts (same number of contexts, same work).
static inline void rec_swap(uint32_t *h, const uint8_t *b, size_t n, const RecState *r) {
    size_t R = (size_t)r->R;
    if (!R) return;
    uint32_t up = n >= R ? b[n - R] : 256, upr = (n + 1 >= R && R > 1) ? b[n - R + 1] : 256, up2 = n >= 2 * R ? b[n - 2 * R] : 256;
    uint32_t up3 = n >= 3 * R ? b[n - 3 * R] : 256, c1 = n >= 1 ? b[n - 1] : 0;
    uint32_t RR = (uint32_t)R * 0x9E3779B1u;
    h[NORD] = hfin(RR + up * 0x1000193u + upr, 0x11);                                    // row above + above-right
    h[NORD + 1] = hfin(RR + up * 0x1000193u + up2 * 0x3B9AC9FBu + up3 * 0x6F4F2A35u, 0xAA);   // rows above 1-3
    h[NORD - 1] = hfin(RR + up * 0x1000193u + c1 * 0x3B9AC9FBu, 0x22);                    // row above + last byte
}
// v3.4 numeric context: treats the data as 16-bit little-endian samples, one sample
// every 2 bytes per channel (R = 4 gives stereo), and predicts the next byte from the
// linear extrapolation 2a - b of the last two samples of the same channel.
static inline uint32_t num_ctx(const uint8_t *b, size_t n, int R) {
    size_t st = (R == 4 || R == 6 || R == 8) ? (size_t)R : 2;
    if (n < 2 * st + 2) return 0x51A7u;
    int ph = (int)(n & 1);                          // 0: next byte is a low byte, 1: high byte
    size_t s0 = n - ph;                             // start of the sample being coded
    int a = (int16_t)(b[s0 - st] | (b[s0 - st + 1] << 8)), c = (int16_t)(b[s0 - 2 * st] | (b[s0 - 2 * st + 1] << 8));
    int pred = 2 * a - c, d = a - c; d = d < 0 ? -d : d;
    int lvl = d < 2 ? 0 : d < 8 ? 1 : d < 32 ? 2 : d < 128 ? 3 : d < 512 ? 4 : 5;
    if (ph == 0) return hfin((uint32_t)(pred & 0xFF) * 0x1000193u + (uint32_t)lvl * 0x3B9AC9FBu, 0x77);
    int lo = b[n - 1];                              // the low byte just coded
    return hfin((uint32_t)(((pred - lo + 128) >> 8) & 0xFF) * 0x9E3779B1u + (uint32_t)lvl * 0x6F4F2A35u + 0x100u, 0x78);
}
// rolling form of the 6-byte match hash (same value, two multiplies)
static const uint64_t KINV20 = 0xd9f0bULL, K5_20 = 0xe3a13ULL;

// ---- bit output and input
struct BitWriter { uint8_t *data; size_t size, cap; uint32_t cur; int nbits; int failed; };
static void bw_init(BitWriter *w, size_t guess) {
    w->cap = guess < 64 ? 64 : guess;
    w->data = (uint8_t *)malloc(w->cap);
    w->size = 0; w->cur = 0; w->nbits = 0; w->failed = w->data == NULL;
}
static inline void bw_bit(BitWriter *w, int bit) {
    w->cur = (w->cur << 1) | (uint32_t)bit;
    if (++w->nbits == 8) {
        if (w->size == w->cap && !w->failed) {
            uint8_t *nd = (uint8_t *)realloc(w->data, w->cap * 2);
            if (nd) { w->data = nd; w->cap *= 2; } else w->failed = 1;
        }
        if (!w->failed) w->data[w->size++] = (uint8_t)w->cur;
        w->cur = 0; w->nbits = 0;
    }
}
static void bw_put(BitWriter *w, uint32_t v, int n) { for (int k = n - 1; k >= 0; k--) bw_bit(w, (v >> k) & 1); }
static void bw_flush(BitWriter *w) { while (w->nbits) bw_bit(w, 0); }
struct BitReader { const uint8_t *data; uint64_t nbits, pos; };
static inline int br_bit(BitReader *r) {
    int b = 0;
    if (r->pos < r->nbits) b = (r->data[r->pos >> 3] >> (7 - (r->pos & 7))) & 1;
    r->pos++;
    return b;
}
static uint32_t br_get(BitReader *r, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v = (v << 1) | (uint32_t)br_bit(r); return v; }

// ---- 32-bit binary arithmetic coder (same coder as v2, without division)
static const uint64_t TOP = 0xFFFFFFFFULL, HALF = 1ULL << 31, Q1 = 1ULL << 30, Q3 = 3ULL << 30;
struct Coder { uint64_t lo, hi, pend, v; BitWriter *w; BitReader *r; };
static inline void enc_out(Coder *e, int bit) { bw_bit(e->w, bit); for (; e->pend; e->pend--) bw_bit(e->w, 1 - bit); }
static inline void enc_bit(Coder *e, int y, uint64_t p) {   // y = 1 takes [0, p), y = 0 takes [p, 4096)
    uint64_t rng = e->hi - e->lo + 1;
    if (y) e->hi = e->lo + ((rng * p) >> 12) - 1;
    else e->lo = e->lo + ((rng * p) >> 12);
    for (;;) {
        if (e->hi < HALF) enc_out(e, 0);
        else if (e->lo >= HALF) { enc_out(e, 1); e->lo -= HALF; e->hi -= HALF; }
        else if (e->lo >= Q1 && e->hi < Q3) { e->pend++; e->lo -= Q1; e->hi -= Q1; }
        else break;
        e->lo <<= 1; e->hi = (e->hi << 1) | 1;
    }
}
static void enc_finish(Coder *e) { e->pend++; enc_out(e, e->lo < Q1 ? 0 : 1); }
static inline int dec_bit(Coder *d, uint64_t p) {
    uint64_t rng = d->hi - d->lo + 1;
    int y = ((d->v - d->lo + 1) * 4096 - 1) < p * rng;
    if (y) d->hi = d->lo + ((rng * p) >> 12) - 1;
    else d->lo = d->lo + ((rng * p) >> 12);
    for (;;) {
        if (d->hi < HALF) {}
        else if (d->lo >= HALF) { d->lo -= HALF; d->hi -= HALF; d->v -= HALF; }
        else if (d->lo >= Q1 && d->hi < Q3) { d->lo -= Q1; d->hi -= Q1; d->v -= Q1; }
        else break;
        d->lo <<= 1; d->hi = (d->hi << 1) | 1; d->v = (d->v << 1) | (uint64_t)br_bit(d->r);
    }
    return y;
}

// ---- tables
static void *alloc64(size_t bytes, void **raw) {
    *raw = calloc(bytes + 64, 1);
    return *raw ? (void *)(((uintptr_t)*raw + 63) & ~(uintptr_t)63) : NULL;
}

struct Tables {
    int bits;                  // 2^bits blocks per context (up to 16 fast, 17 max)
    uint32_t *T[NH]; void *Traw[NH];
    uint32_t T0[256];          // order 0: directly indexed
    int32_t *w;  void *wraw;   // 2048 weight sets (partial byte x match bucket x letter)
    int32_t *w2; void *w2raw;  // max mode: 6400 weight sets (partial byte x classes)
    uint16_t *apm; uint8_t *apm_ok; uint16_t row0[33];
    int32_t *mtab;
    uint32_t mmask;            // match table size - 1
    // v3.5 sections: the file is split into text and binary stretches; one model runs
    // through the whole file and only the text/binary contexts switch at each boundary.
    int nsec; const uint32_t *sec_start; const uint8_t *sec_bin;
};

static void tables_free(Tables *t) {
    for (int i = 0; i < NH; i++) free(t->Traw[i]);
    free(t->wraw); free(t->w2raw); free(t->apm); free(t->apm_ok); free(t->mtab);
}

static int floor_log2(size_t n) { int k = 0; while (n > 1) { n >>= 1; k++; } return k; }

// Table sizes follow the file size (small files need small tables and start
// faster); the decoder reads the size from the header and does the same.
static int tables_init(Tables *t, int maxbits, int twomix, size_t n, int v33) {
    memset(t, 0, sizeof(*t));
    int bits = floor_log2(n) + 2;
    bits = bits < 10 ? 10 : (bits > maxbits ? maxbits : bits);
    int mbits = floor_log2(n) + 3;
    mbits = mbits < 10 ? 10 : (mbits > MTAB_BITS ? MTAB_BITS : mbits);
    t->bits = bits;
    t->mmask = ((uint32_t)1 << mbits) - 1;
    for (int i = 0; i < NH; i++) {
        t->T[i] = (uint32_t *)alloc64(((size_t)16 << bits) * 4, &t->Traw[i]);
        if (!t->T[i]) { tables_free(t); return -1; }
    }
    for (int k = 0; k < 256; k++) t->T0[k] = 0x8000;
    const int wsets = v33 ? 8192 : 2048, wstride = v33 ? 16 : NINP;
    t->w = (int32_t *)alloc64(sizeof(int32_t) * wstride * wsets, &t->wraw);
    if (twomix) t->w2 = (int32_t *)alloc64(sizeof(int32_t) * NINP * 256 * 25, &t->w2raw);
    t->apm = (uint16_t *)malloc(sizeof(uint16_t) * 33 * APM_ROWS);
    t->apm_ok = (uint8_t *)calloc(APM_ROWS, 1);
    t->mtab = (int32_t *)calloc((size_t)t->mmask + 1, 4);
    if (!t->w || (twomix && !t->w2) || !t->apm || !t->apm_ok || !t->mtab) { tables_free(t); return -1; }
    for (int r = 0; r < wsets; r++)
        for (int i = 0; i < NINP; i++) t->w[(size_t)r * wstride + i] = i < NIN ? WINIT : 0;
    if (twomix)
        for (int r = 0; r < 256 * 25; r++)
            for (int i = 0; i < NINP; i++) t->w2[(size_t)r * NINP + i] = i < NIN ? WINIT : 0;
    for (int j = 0; j < 33; j++) {
        int x = j * 128 - 2048;
        if (x > 2047) x = 2047;
        t->row0[j] = (uint16_t)(SQI[x + 2048] * 16);
    }
    return 0;
}

static inline uint32_t bucket_mix(uint32_t h, uint32_t c0) {
    uint32_t x = h + c0 * 0x9E3779B1u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return x;
}
static inline uint32_t *bucket_addr(uint32_t *T, uint32_t x, int bits) {
    return T + ((size_t)((x >> (32 - bits)) & ~1u) << 4);
}
static inline void bucket_reset(uint32_t *b, uint32_t chk) {
#if HCM_SSE2
    __m128i v = _mm_set1_epi32(0x8000);
    for (int k = 0; k < 16; k += 4) _mm_store_si128((__m128i *)(b + k), v);
#else
    for (int k = 0; k < 16; k++) b[k] = 0x8000;
#endif
    b[0] = chk;
}
// Two neighbouring blocks may hold this context; take the one whose check
// value matches, or recycle the less-used one.
static inline uint32_t *bucket_find(uint32_t *T, uint32_t x, int bits) {
    uint32_t *b = bucket_addr(T, x, bits);
    uint32_t chk = (x & 0xFFFF) | 0x10000u;
    if (b[0] == chk) return b;
    uint32_t *b1 = b + 16;
    if (b1[0] == chk) return b1;
    uint32_t *v = (b[1] >> 16) <= (b1[1] >> 16) ? b : b1;
    bucket_reset(v, chk);
    return v;
}

static inline void prefetch(const void *p) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(p);
#else
    (void)p;
#endif
}

// context hashes for the byte just completed (b[0..n-1])
static inline void ctx_hashes(uint32_t *h, const uint8_t *b, size_t n, uint32_t word, uint32_t pword) {
    uint32_t x = 0x9E3779B1u;
    int k = 0;
    for (int j = 1; j <= 8; j++) {
        uint32_t c = n >= (size_t)j ? b[n - j] : 0;
        x = (x ^ c) * 0x01000193u + (uint32_t)j;
        if (j == ORDERS[k]) { h[k] = (x + (uint32_t)j * 0x6A09E667u) * 0x2545F491u; k++; }
    }
    h[NORD] = word * 0x6F4F2A35u + 0x51ED27u;
    h[NORD + 1] = (pword * 0x3B9AC9FBu) ^ (word * 0x6F4F2A35u) ^ 0x7A3D1u;
}

// w[i] += (x[i] * err + 1024) >> 11 for 12 inputs
static inline void train(int32_t *w, const int32_t *x, int err) {
#if HCM_SSE2
    __m128i ev = _mm_set1_epi32(err & 0xFFFF), rv = _mm_set1_epi32(1 << (LRSHIFT - 1));
    for (int i = 0; i < NINP; i += 4) {
        __m128i xv = _mm_load_si128((const __m128i *)(x + i));
        __m128i pv = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(xv, ev), rv), LRSHIFT);
        __m128i wv = _mm_load_si128((const __m128i *)(w + i));
        _mm_store_si128((__m128i *)(w + i), _mm_add_epi32(wv, pv));
    }
#else
    for (int i = 0; i < NINP; i++) w[i] += (x[i] * err + (1 << (LRSHIFT - 1))) >> LRSHIFT;
#endif
}

template <int DEC, int TWOMIX, int V33>
static void run(Tables *t, uint8_t *buf, size_t n, Coder *cd, int isbin, int v34) {
    const int APMW = V33 ? APMW_V33 : APMW_V3, MMR = V33 ? MMR_V33 : MMR_V3, WSTRIDE = V33 ? 16 : NINP;
    static thread_local RecState rs;
    int si = 0;  // v3.5: current section
    if (V33) { rec_init(&rs); for (int c = 0; c < 256; c++) CLS[c] = (uint8_t)cls(c); }
    int wl = 0;                  // letters so far in the current word
    int64_t lnl = -1;            // position of the last newline
    uint32_t hroll = 0;
    const int bits = t->bits;
    uint32_t h[NH], *base[NH], *slot[NM];
    uint32_t word = 0, pword = 0;
    int64_t mptr = 0, mlen = 0;
    uint16_t mm[64];
    v34 = V33 ? v34 : 0;
    for (int i = 0; i < 64; i++) mm[i] = 49152;
#if defined(_MSC_VER)
    __declspec(align(16)) int32_t x[NINP];
#else
    int32_t x[NINP] __attribute__((aligned(16)));
#endif
    for (int i = 0; i < NINP; i++) x[i] = 0;
    int last = 0, last2 = 0;
    ctx_hashes(h, buf, 0, 0, 0);
    for (size_t pos = 0; pos < n; pos++) {
        int c = DEC ? 0 : buf[pos];
        int mbyte = (mlen > 0 && (size_t)mptr < pos) ? buf[mptr] : -1;   // byte the match expects
        int mk = mlen < 63 ? (int)mlen : 63;
        int ms = mlen == 0 ? 0 : mlen < 16 ? 1 : mlen < 32 ? 2 : 3;
        int letter = ((last | 32) >= 'a' && (last | 32) <= 'z') ? 1 : 0;
        if (V33) {   // mixer weights chosen by word position
            int cl = CLS[last];
            letter = wl == 0 ? (cl == 3 ? 0 : cl == 2 ? 1 : cl == 4 ? 2 : 3) : wl == 1 ? 4 : wl == 2 ? 5 : wl < 5 ? 6 : 7;
        }
        int sel2 = cls(last) * 5 + cls(last2);
        uint32_t c0 = 1, nib = 1;
        {
            uint32_t xs[NH];
            for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], 1); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
            for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i], bits);
        }
        for (int bit = 7; bit >= 0; bit--) {
            if (bit == 3) {   // second nibble: new blocks, chosen by the first nibble
                nib = 1;
                uint32_t xs[NH];
                for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], c0); prefetch(bucket_addr(t->T[i], xs[i], bits)); }
                for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i], bits);
            }
            for (int i = 0; i < NH; i++) slot[i] = base[i] + nib;
            slot[NH] = t->T0 + c0;
            for (int i = 0; i < NM; i++) x[i] = STI[(*slot[i] & 0xFFFF) >> 4];
            int mexp = -1;
            if (mbyte >= 0) {
                if ((uint32_t)((mbyte | 256) >> (bit + 1)) == c0) mexp = (mbyte >> bit) & 1;
                else mbyte = -1;
            }
            if (mexp >= 0) { int s = STI[mm[mk] >> 4]; x[NM] = mexp ? s : -s; } else x[NM] = 0;
            x[NM + 1] = 64;
            int32_t *ws = t->w + (size_t)(c0 + 256 * ms + 1024 * letter) * WSTRIDE;
            int64_t dot = 0;
            for (int i = 0; i < NIN; i++) dot += (int64_t)ws[i] * x[i];
            int s, pm1 = 0, pm2 = 0;
            int32_t *ws2 = NULL;
            if (TWOMIX) {
                ws2 = t->w2 + (size_t)(c0 + 256 * sel2) * NINP;
                int64_t dot2 = 0;
                for (int i = 0; i < NIN; i++) dot2 += (int64_t)ws2[i] * x[i];
                int s1 = (int)(dot >> 16), s2 = (int)(dot2 >> 16);
                s1 = s1 > 2047 ? 2047 : (s1 < -2047 ? -2047 : s1);
                s2 = s2 > 2047 ? 2047 : (s2 < -2047 ? -2047 : s2);
                pm1 = SQI[s1 + 2048]; pm2 = SQI[s2 + 2048];
                s = (s1 + s2) >> 1;
            } else {
                s = (int)(dot >> 16);
            }
            s = s > 2047 ? 2047 : (s < -2047 ? -2047 : s);
            int pm = SQI[s + 2048];
            // refinement stage, indexed by partial byte and previous byte
            int arow_i = (int)(c0 + 256 * (uint32_t)last);
            uint16_t *row = t->apm + (size_t)arow_i * 33;
            if (!t->apm_ok[arow_i]) { memcpy(row, t->row0, sizeof(t->row0)); t->apm_ok[arow_i] = 1; }
            if (bit > 0) prefetch(t->apm + (size_t)(2 * c0 + 256 * (uint32_t)last) * 33);
            int apos = s + 2048, j = apos >> 7, wt = apos & 127;
            int pa = (row[j] * (128 - wt) + row[j + 1] * wt) >> 11;
            int p = (pm * (256 - APMW) + pa * APMW) >> 8;
            p = p < 1 ? 1 : (p > 4095 ? 4095 : p);
            int y;
            if (DEC) y = dec_bit(cd, (uint64_t)p);
            else { y = (c >> bit) & 1; enc_bit(cd, y, (uint64_t)p); }
            // learn
            if (TWOMIX) { train(ws2, x, (y << 12) - pm2); train(ws, x, (y << 12) - pm1); }
            else train(ws, x, (y << 12) - pm);
            int tgt = y ? 65535 : 0;
            for (int i = 0; i < NM; i++) {
                uint32_t e = *slot[i];
                int pp = (int)(e & 0xFFFF), nn = (int)(e >> 16);
                pp += ((tgt - pp) * RT15[nn]) >> 15;
                nn = i == 0 ? NEXT60[nn] : NEXT255[nn];
                *slot[i] = (uint32_t)pp | ((uint32_t)nn << 16);
            }
            if (mexp >= 0) { int v = mm[mk]; int hit = y == mexp ? 65535 : 0; mm[mk] = (uint16_t)(v + ((hit - v) >> MMR)); }
            int t16 = y ? 65535 : 0;
            row[j] = (uint16_t)(row[j] + (((t16 - row[j]) * (128 - wt)) >> APMRATE));
            row[j + 1] = (uint16_t)(row[j + 1] + (((t16 - row[j + 1]) * wt) >> APMRATE));
            c0 = (c0 << 1) | (uint32_t)y;
            nib = (nib << 1) | (uint32_t)y;
        }
        int byte = (int)(c0 & 255);
        buf[pos] = (uint8_t)byte;
        size_t nb = pos + 1;
        if ((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122)) { word = (word * 0x2C9277B5u) ^ (uint32_t)(byte | 32); wl++; }
        else { if (word) pword = word; word = 0; wl = 0; }
        if (mlen > 0 && (size_t)mptr < nb - 1 && buf[mptr] == byte) { mptr++; mlen++; }
        else if (v34 && mlen >= 24 && (size_t)mptr < nb - 1) { mptr++; mlen = 16; }   // v3.4: a long match survives one wrong byte
        else mlen = 0;
        if (nb >= 6) {
            uint32_t hh;
            if (V33 && nb >= 7) {
                hroll = (uint32_t)((((uint64_t)((hroll - buf[nb - 7] + 0x100000u) & 0xFFFFF) * KINV20) + (uint64_t)buf[nb - 1] * K5_20) & 0xFFFFF);
                hh = hroll;
            } else {
                hh = 0;
                for (int j2 = 1; j2 <= 6; j2++) hh = (uint32_t)(((uint64_t)hh * 0x2F0B4A3u + buf[nb - j2]) & 0xFFFFF);
                hroll = hh;
            }
            hh &= t->mmask;
            if (mlen == 0) {
                int64_t cand = t->mtab[hh];
                if (cand > 0) {
                    int L = 0;
                    while (L < 32 && cand - 1 - L >= 0 && buf[cand - 1 - L] == buf[nb - 1 - L]) L++;
                    if (L >= 6) { mptr = cand; mlen = L; }
                }
            }
            t->mtab[hh] = (int32_t)nb;
        }
        last2 = last;
        last = byte;
        ctx_hashes(h, buf, nb, word, pword);
        if (V33) {
            if (t->nsec) {   // v3.5: switch text/binary contexts at section boundaries
                while (si + 1 < t->nsec && nb >= t->sec_start[si + 1]) si++;
                isbin = t->sec_bin[si];
                if (!isbin) rec_update(&rs, buf, nb);   // keep record detection warm through text
            }
            if (isbin) {
                rec_update(&rs, buf, nb); rec_swap(h, buf, nb, &rs);
                if (v34) h[4] = num_ctx(buf, nb, rs.R);   // v3.4: the order-6 slot predicts 16-bit samples
            }
            else {   // text: the order-6 slot becomes a line-position context
                if (byte == '\n') lnl = (int64_t)nb - 1;
                uint32_t col = (uint32_t)((int64_t)nb - 1 - lnl); col = col < 255 ? col : 255;
                h[4] = hfin((uint32_t)wl * 0x3B9AC9FBu + (uint32_t)CLS[byte] * 0x6F4F2A35u + col * 0x2545F491u, 0x5A);
            }
        }
    }
}

static uint32_t CRC_T[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        CRC_T[i] = c;
    }
}
static uint32_t crc32(const uint8_t *d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = CRC_T[(c ^ d[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

enum { MODE_FAST = 0, MODE_MAX = 1 };
// mode byte: bit 0 = max mode, bit 4 = binary file, bit 5 = v3.3 model.
// Plain v3 files (bits 4-5 clear) decode exactly as before.
// bit 1 = v3.4 model (match recovery; numeric contexts for binary files), bit 3 = stored raw.
enum { FLAG_BIN = 16, FLAG_V33 = 32, FLAG_V34 = 2, FLAG_RAW = 8, FLAG_SEG = 64 };
// bit 6 (v3.5) = sections: a table of text/binary stretches follows the header.
static const int HEADER = 13;

static inline void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int is_v3(const uint8_t *blob, size_t n) {
    return n >= (size_t)HEADER && blob[0] == 'H' && blob[1] == 'C' && blob[2] == 'M' && blob[3] == '3';
}

template <int DEC>
static void run_any(Tables *t, uint8_t *buf, size_t n, Coder *cd, int mb) {
    int mx = mb & 1, v33 = (mb & FLAG_V33) != 0, bin = (mb & FLAG_BIN) != 0, v34 = (mb & FLAG_V34) != 0;
    if (v33) { if (mx) run<DEC, 1, 1>(t, buf, n, cd, bin, v34); else run<DEC, 0, 1>(t, buf, n, cd, bin, v34); }
    else     { if (mx) run<DEC, 1, 0>(t, buf, n, cd, 0, 0); else run<DEC, 0, 0>(t, buf, n, cd, 0, 0); }
}

static int encode_one(const uint8_t *data, size_t n, int mb, uint8_t **out, size_t *outn,
                      int nsec = 0, const uint32_t *ss = NULL, const uint8_t *sb = NULL) {
    Tables *t = (Tables *)malloc(sizeof(Tables));
    if (!t || tables_init(t, (mb & 1) ? 17 : 16, mb & 1, n, (mb & FLAG_V33) != 0)) { free(t); return -1; }
    t->nsec = (mb & FLAG_SEG) ? nsec : 0; t->sec_start = ss; t->sec_bin = sb;
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    if (!buf) { tables_free(t); free(t); return -1; }
    memcpy(buf, data, n);
    BitWriter w;
    bw_init(&w, n / 3 + 64);
    uint8_t hdr[HEADER] = {'H', 'C', 'M', '3', (uint8_t)mb};
    put32(hdr + 5, (uint32_t)n);
    put32(hdr + 9, crc32(data, n));
    for (int i = 0; i < HEADER; i++) bw_put(&w, hdr[i], 8);
    if (mb & FLAG_SEG) {   // v3.5 section table: count, then start (32 bits) and binary flag (8 bits)
        bw_put(&w, (uint32_t)nsec >> 8, 8); bw_put(&w, (uint32_t)nsec & 255, 8);
        for (int i = 0; i < nsec; i++) {
            for (int k = 24; k >= 0; k -= 8) bw_put(&w, (ss[i] >> k) & 255, 8);
            bw_put(&w, sb[i], 8);
        }
    }
    Coder e = {0, TOP, 0, 0, &w, NULL};
    run_any<0>(t, buf, n, &e, mb);
    enc_finish(&e);
    bw_flush(&w);
    tables_free(t); free(t); free(buf);
    if (w.failed) { free(w.data); return -1; }
    *out = w.data;
    *outn = w.size;
    return 0;
}

// Returns 0 on success, -1 if memory ran out. The v3.3 model is tried and
// kept only if it beats plain v3 on this file, so no file gets bigger.
static int compress(const uint8_t *data, size_t n, int mode, uint8_t **out, size_t *outn) {
    uint8_t *a, *b; size_t an, bn;
    if (encode_one(data, n, mode, &a, &an)) return -1;
    int bin = looks_binary(data, n);
    int mb = mode | FLAG_V33 | (bin ? FLAG_BIN : 0);
    if (encode_one(data, n, mb, &b, &bn)) { free(a); return -1; }
    if (bn < an) { free(a); a = b; an = bn; } else free(b);
    // Ambiguous files (few control bytes but many bytes >= 128, such as image rows or
    // audio samples) are also tried in binary mode, and the smaller result is kept.
    int amb = !bin && looks_ambiguous(data, n);
    if (amb) {
        if (encode_one(data, n, mode | FLAG_V33 | FLAG_BIN, &b, &bn)) { free(a); return -1; }
        if (bn < an) { free(a); a = b; an = bn; } else free(b);
    }
    // v3.4: match recovery, plus numeric contexts for binary files
    if (encode_one(data, n, mode | FLAG_V33 | FLAG_V34 | (bin ? FLAG_BIN : 0), &b, &bn)) { free(a); return -1; }
    if (bn < an) { free(a); a = b; an = bn; } else free(b);
    if (amb) {
        if (encode_one(data, n, mode | FLAG_V33 | FLAG_V34 | FLAG_BIN, &b, &bn)) { free(a); return -1; }
        if (bn < an) { free(a); a = b; an = bn; } else free(b);
    }
    // v3.5 sections: mark 64 KB windows as text or binary, absorb stretches shorter than
    // 256 KB into the one before, and if the file really mixes both, try one pass that
    // switches contexts at each boundary. Kept only if smaller.
    {
        const size_t W = 65536, MINSEC = 4 * W;
        size_t nw = (n + W - 1) / W;
        if (nw >= 8) {
            uint32_t *ss = (uint32_t *)malloc(nw * 4); uint8_t *sb = (uint8_t *)malloc(nw);
            int ns = 0;
            for (size_t i = 0; i < nw; i++) {
                size_t a0 = i * W, l = n - a0 < W ? n - a0 : W;
                uint8_t f = (uint8_t)(looks_binary(data + a0, l) != 0);
                if (ns && sb[ns - 1] == f) continue;
                if (ns && (size_t)(a0 - ss[ns - 1]) < MINSEC) {     // previous stretch too short:
                    if (ns >= 2 && sb[ns - 2] == f) { ns--; continue; }  // merge back into the one before
                    sb[ns - 1] = f; if (ns == 1) ss[0] = 0; continue;
                }
                ss[ns] = (uint32_t)a0; sb[ns] = f; ns++;
            }
            if (ns && n - ss[ns - 1] < MINSEC && ns >= 2) ns--;     // short tail joins the previous stretch
            if (ns >= 2) {
                if (encode_one(data, n, mode | FLAG_V33 | FLAG_V34 | FLAG_SEG, &b, &bn, ns, ss, sb) == 0) {
                    if (bn < an) { free(a); a = b; an = bn; } else free(b);
                }
            }
            free(ss); free(sb);
        }
    }
    // incompressible: store the bytes unchanged (13-byte header only)
    if (an >= (size_t)HEADER + n) {
        free(a);
        a = (uint8_t *)malloc(HEADER + (n ? n : 1)); if (!a) return -1;
        a[0] = 'H'; a[1] = 'C'; a[2] = 'M'; a[3] = '3'; a[4] = (uint8_t)(FLAG_RAW | (mode & 1));
        put32(a + 5, (uint32_t)n); put32(a + 9, crc32(data, n)); memcpy(a + HEADER, data, n); an = HEADER + n;
    }
    *out = a; *outn = an;
    return 0;
}

// Returns 0 on success, -1 if memory ran out, -2 if the file is not valid,
// -5 if the contents do not match the stored checksum (damaged file).
static int decompress(const uint8_t *blob, size_t bn, uint8_t **out, size_t *outn) {
    if (!is_v3(blob, bn)) return -2;
    int mb = blob[4];
    if (mb & ~(1 | FLAG_BIN | FLAG_V33 | FLAG_V34 | FLAG_RAW | FLAG_SEG)) return -2;
    if ((mb & (FLAG_BIN | FLAG_V34 | FLAG_SEG)) && !(mb & FLAG_V33)) return -2;
    uint32_t n = get32(blob + 5), crc = get32(blob + 9);
    if (mb & FLAG_RAW) {   // stored unchanged
        if (mb & ~(FLAG_RAW | 1) || bn != (size_t)HEADER + n) return -2;
        uint8_t *buf = (uint8_t *)malloc(n ? n : 1); if (!buf) return -1;
        memcpy(buf, blob + HEADER, n);
        if (crc32(buf, n) != crc) { free(buf); return -5; }
        *out = buf; *outn = n; return 0;
    }
    if (n > 0x7FFFFFFFu || (uint64_t)n > ((uint64_t)bn + 16) * 4096) return -2;
    BitReader r = {blob + HEADER, (uint64_t)(bn - HEADER) * 8, 0};
    Coder d = {0, TOP, 0, 0, NULL, &r};
    int nsec = 0; uint32_t *ss = NULL; uint8_t *sb = NULL;
    if (mb & FLAG_SEG) {
        nsec = (int)(br_get(&r, 8) << 8); nsec |= (int)br_get(&r, 8);
        if (nsec < 1 || (size_t)nsec * 5 + 2 > bn) return -2;
        ss = (uint32_t *)malloc((size_t)nsec * 4); sb = (uint8_t *)malloc((size_t)nsec);
        if (!ss || !sb) { free(ss); free(sb); return -1; }
        for (int i = 0; i < nsec; i++) { ss[i] = (uint32_t)br_get(&r, 32); sb[i] = (uint8_t)br_get(&r, 8); }
    }
    d.v = br_get(&r, 32);
    Tables *t = (Tables *)malloc(sizeof(Tables));
    if (!t || tables_init(t, (mb & 1) ? 17 : 16, mb & 1, n, (mb & FLAG_V33) != 0)) { free(t); free(ss); free(sb); return -1; }
    t->nsec = nsec; t->sec_start = ss; t->sec_bin = sb;
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    if (!buf) { tables_free(t); free(t); return -1; }
    run_any<1>(t, buf, n, &d, mb);
    tables_free(t); free(t); free(ss); free(sb);
    if (crc32(buf, n) != crc) { free(buf); return -5; }
    *out = buf;
    *outn = n;
    return 0;
}

}  // namespace v3
// ------------------------------------------------------------------------
// Files and names (wide-character paths on Windows)

#ifdef _WIN32
typedef wchar_t pchar;
#define PS "%ls"
static FILE *open_file(const pchar *p, const char *mode) {
    return _wfopen(p, mode[0] == 'r' ? L"rb" : L"wb");
}
static size_t plen(const pchar *s) { return wcslen(s); }
static pchar plower(pchar c) { return (c >= L'A' && c <= L'Z') ? (pchar)(c + 32) : c; }
#else
typedef char pchar;
#define PS "%s"
static FILE *open_file(const pchar *p, const char *mode) {
    return fopen(p, mode[0] == 'r' ? "rb" : "wb");
}
static size_t plen(const pchar *s) { return strlen(s); }
static pchar plower(pchar c) { return (c >= 'A' && c <= 'Z') ? (pchar)(c + 32) : c; }
#endif

static int file_exists(const pchar *p) {
    FILE *f = open_file(p, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

// Reads a whole file. Returns 0, -1 (cannot open/read), -3 (too large),
// -4 (out of memory).
static int read_file(const pchar *p, uint8_t **data, size_t *n) {
    FILE *f = open_file(p, "r");
    if (!f) return -1;
    size_t cap = 1 << 20, size = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) { fclose(f); return -4; }
    for (;;) {
        if (size == cap) {
            if (cap > v2::MAX_INPUT) { free(buf); fclose(f); return -3; }
            size_t ncap = cap * 2;
            uint8_t *nb = (uint8_t *)realloc(buf, ncap);
            if (!nb) { free(buf); fclose(f); return -4; }
            buf = nb; cap = ncap;
        }
        size_t got = fread(buf + size, 1, cap - size, f);
        size += got;
        if (got == 0) break;
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) { free(buf); return -1; }
    if (size > v2::MAX_INPUT) { free(buf); return -3; }
    *data = buf;
    *n = size;
    return 0;
}

static int write_file(const pchar *p, const uint8_t *data, size_t n) {
    FILE *f = open_file(p, "w");
    if (!f) return -1;
    size_t put = fwrite(data, 1, n, f);
    int bad = fclose(f) != 0 || put != n;
    return bad ? -1 : 0;
}

enum { PATHMAX = 4096 };

static void pcopy(pchar *dst, const pchar *src, size_t n) {
    memcpy(dst, src, n * sizeof(pchar));
    dst[n] = 0;
}

static void pappend_ascii(pchar *dst, const char *s) {
    size_t n = plen(dst);
    while (*s && n < PATHMAX - 1) dst[n++] = (pchar)*s++;
    dst[n] = 0;
}

static int ends_with_hcm(const pchar *p) {
    size_t n = plen(p);
    return n > 4 && p[n - 4] == '.' && plower(p[n - 3]) == 'h' &&
           plower(p[n - 2]) == 'c' && plower(p[n - 1]) == 'm';
}

static const pchar *base_name(const pchar *p) {
    const pchar *b = p;
    for (const pchar *s = p; *s; s++)
        if (*s == '/' || *s == '\\') b = s + 1;
    return b;
}

// NAME.hcm, or NAME (2).hcm, NAME (3).hcm ... if that is taken.
static int name_for_compressed(const pchar *in, pchar *out) {
    size_t n = plen(in);
    if (n + 16 >= PATHMAX) return -1;
    for (int k = 1; k < 1000; k++) {
        pcopy(out, in, n);
        if (k > 1) {
            char tag[16];
            snprintf(tag, sizeof tag, " (%d)", k);
            pappend_ascii(out, tag);
        }
        pappend_ascii(out, ".hcm");
        if (!file_exists(out)) return 0;
    }
    return -1;
}

// NAME.ext.hcm -> NAME.ext, or NAME (restored).ext if that is taken.
static int name_for_restored(const pchar *in, pchar *out) {
    size_t n = plen(in) - 4;
    if (n + 32 >= PATHMAX) return -1;
    pcopy(out, in, n);
    if (!file_exists(out)) return 0;
    const pchar *b = base_name(out);
    size_t dot = n;
    for (size_t i = (size_t)(b - out); i < n; i++)
        if (out[i] == '.') dot = i;
    if (dot == (size_t)(b - out)) dot = n;  // a name that starts with a dot
    static pchar ext[PATHMAX];  // static: keeps stack frames small
    pcopy(ext, out + dot, n - dot);
    for (int k = 1; k < 1000; k++) {
        pcopy(out, in, dot);
        char tag[32];
        if (k == 1) snprintf(tag, sizeof tag, " (restored)");
        else snprintf(tag, sizeof tag, " (restored %d)", k);
        pappend_ascii(out, tag);
        size_t m = plen(out);
        if (m + (n - dot) >= PATHMAX) return -1;
        pcopy(out + m, ext, n - dot);
        if (!file_exists(out)) return 0;
    }
    return -1;
}

// 1234567 -> "1,234,567"
static const char *commas(uint64_t v, char *buf) {
    char tmp[32];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    int k = 0;
    for (int i = n - 1; i >= 0; i--) {
        buf[k++] = tmp[i];
        if (i > 0 && i % 3 == 0) buf[k++] = ',';
    }
    buf[k] = 0;
    return buf;
}

static double seconds_since(clock_t t0) {
    return (double)(clock() - t0) / CLOCKS_PER_SEC;
}

// ---- v3.5 parallel blocks: "HCMP" + 32-bit block size + 32-bit block count, then each
// block's compressed length (32 bits) and its ordinary HCM3 stream. Blocks are compressed
// and decompressed on separate processor cores; each block has its own model.
static int par_threads(void) { unsigned h = std::thread::hardware_concurrency(); return h ? (int)h : 2; }
static int compress_par(const uint8_t *data, size_t n, int mode, size_t B, uint8_t **out, size_t *outn) {
    size_t nb = n ? (n + B - 1) / B : 1;
    std::vector<uint8_t *> bl(nb, (uint8_t *)NULL); std::vector<size_t> bln(nb, 0); std::vector<int> rc(nb, 0);
    int T = par_threads();
    for (size_t s = 0; s < nb; s += (size_t)T) {
        std::vector<std::thread> th;
        for (size_t i = s; i < nb && i < s + (size_t)T; i++)
            th.emplace_back([&, i] { size_t a = i * B, l = n - a < B ? n - a : B; rc[i] = v3::compress(data + a, l, mode, &bl[i], &bln[i]); });
        for (auto &x : th) x.join();
    }
    size_t tot = 12 + 4 * nb; for (size_t i = 0; i < nb; i++) { if (rc[i]) return -1; tot += bln[i]; }
    uint8_t *o = (uint8_t *)malloc(tot); if (!o) return -1;
    o[0] = 'H'; o[1] = 'C'; o[2] = 'M'; o[3] = 'P'; v3::put32(o + 4, (uint32_t)B); v3::put32(o + 8, (uint32_t)nb);
    size_t p = 12 + 4 * nb;
    for (size_t i = 0; i < nb; i++) { v3::put32(o + 12 + 4 * i, (uint32_t)bln[i]); memcpy(o + p, bl[i], bln[i]); p += bln[i]; free(bl[i]); }
    *out = o; *outn = tot; return 0;
}
static int decompress_par(const uint8_t *blob, size_t bn, uint8_t **out, size_t *outn) {
    if (bn < 12) return -2;
    size_t nb = v3::get32(blob + 8); if (nb == 0 || 12 + 4 * nb > bn) return -2;
    std::vector<size_t> off(nb), len(nb); size_t p = 12 + 4 * nb;
    for (size_t i = 0; i < nb; i++) { len[i] = v3::get32(blob + 12 + 4 * i); off[i] = p; p += len[i]; if (p > bn) return -2; }
    std::vector<uint8_t *> o(nb, (uint8_t *)NULL); std::vector<size_t> on(nb, 0); std::vector<int> rc(nb, 0);
    int T = par_threads();
    for (size_t s = 0; s < nb; s += (size_t)T) {
        std::vector<std::thread> th;
        for (size_t i = s; i < nb && i < s + (size_t)T; i++)
            th.emplace_back([&, i] { rc[i] = v3::is_v3(blob + off[i], len[i]) ? v3::decompress(blob + off[i], len[i], &o[i], &on[i]) : -2; });
        for (auto &x : th) x.join();
    }
    size_t tot = 0; int bad = 0;
    for (size_t i = 0; i < nb; i++) { if (rc[i]) bad = rc[i]; tot += on[i]; }
    if (bad) { for (size_t i = 0; i < nb; i++) free(o[i]); return bad; }
    uint8_t *r = (uint8_t *)malloc(tot ? tot : 1); if (!r) return -1;
    size_t q = 0; for (size_t i = 0; i < nb; i++) { memcpy(r + q, o[i], on[i]); q += on[i]; free(o[i]); }
    *out = r; *outn = tot; return 0;
}

// Restores either format. Returns 0, -1 (memory), -2 (not HCM), -5 (damaged).
static int decompress_any(const uint8_t *blob, size_t bn, uint8_t **out, size_t *outn) {
    if (bn >= 4 && blob[0] == 'H' && blob[1] == 'C' && blob[2] == 'M' && blob[3] == 'P') return decompress_par(blob, bn, out, outn);
    if (v3::is_v3(blob, bn)) return v3::decompress(blob, bn, out, outn);
    return v2::hcm_decompress(blob, bn, out, outn);
}

static void report_error(int code) {
    if (code == -1) printf("  Could not open or read the file.\n");
    else if (code == -3) printf("  The file is too large (the limit is 2 GB).\n");
    else if (code == -4) printf("  Not enough memory.\n");
}

// ------------------------------------------------------------------------
// Compress one file to OUT (or to an automatic name when OUT is NULL),
// checking that it decodes back exactly before saving it.

static size_t g_par_block = 0;   // v3.5: block size for parallel mode (0 = single stream)
static int do_compress(const pchar *in, const pchar *out_given, int mode) {
    char a[32], b[32];
    uint8_t *data = NULL, *blob = NULL, *back = NULL;
    size_t n = 0, bn = 0, backn = 0;
    printf("Compressing " PS "  (%s mode)\n", base_name(in), mode == v3::MODE_MAX ? "max" : "fast");
    int rc = read_file(in, &data, &n);
    if (rc) { report_error(rc); return 1; }
    clock_t t0 = clock();
    if ((g_par_block && n > g_par_block) ? compress_par(data, n, mode, g_par_block, &blob, &bn)
                                         : v3::compress(data, n, mode, &blob, &bn)) { printf("  Not enough memory.\n"); free(data); return 1; }
    double tc = seconds_since(t0);
    t0 = clock();
    rc = decompress_any(blob, bn, &back, &backn);
    double td = seconds_since(t0);
    int same = rc == 0 && backn == n && (n == 0 || memcmp(back, data, n) == 0);
    free(back);
    free(data);
    if (!same) {
        printf("  ERROR: the check decode did not match. Nothing was saved.\n");
        free(blob);
        return 1;
    }
    static pchar outname[PATHMAX];
    if (out_given) {
        size_t k = plen(out_given);
        if (k >= PATHMAX) { free(blob); return 1; }
        pcopy(outname, out_given, k);
    } else if (name_for_compressed(in, outname)) {
        printf("  Could not choose a name for the output file.\n");
        free(blob);
        return 1;
    }
    if (write_file(outname, blob, bn)) {
        printf("  Could not write " PS "\n", outname);
        free(blob);
        return 1;
    }
    uint64_t per = n ? ((uint64_t)bn * 1000000 + n / 2) / n : 0;
    printf("  %s bytes down to %s bytes\n", commas(n, a), commas(bn, b));
    printf("  1,000,000 bits down to %s bits\n", commas(per, a));
    printf("  Checked: decodes back exactly.\n");
    printf("  Time: %.1f s to compress, %.1f s to check\n", tc, td);
    printf("  Saved as " PS "\n\n", base_name(outname));
    free(blob);
    return 0;
}

static int do_decompress(const pchar *in, const pchar *out_given) {
    char a[32];
    uint8_t *blob = NULL, *data = NULL;
    size_t bn = 0, n = 0;
    printf("Restoring " PS "\n", base_name(in));
    int rc = read_file(in, &blob, &bn);
    if (rc) { report_error(rc); return 1; }
    clock_t t0 = clock();
    rc = decompress_any(blob, bn, &data, &n);
    double td = seconds_since(t0);
    free(blob);
    if (rc == -2) { printf("  This does not look like an HCM file.\n"); return 1; }
    if (rc == -5) { printf("  This file is damaged: its contents do not match its checksum.\n"); return 1; }
    if (rc) { printf("  Not enough memory.\n"); return 1; }
    static pchar outname[PATHMAX];
    if (out_given) {
        size_t k = plen(out_given);
        if (k >= PATHMAX) { free(data); return 1; }
        pcopy(outname, out_given, k);
    } else if (name_for_restored(in, outname)) {
        printf("  Could not choose a name for the output file.\n");
        free(data);
        return 1;
    }
    if (write_file(outname, data, n)) {
        printf("  Could not write " PS "\n", outname);
        free(data);
        return 1;
    }
    printf("  Restored %s bytes in %.1f s\n", commas(n, a), td);
    printf("  Saved as " PS "\n\n", base_name(outname));
    free(data);
    return 0;
}

static int is_word(const pchar *s, char c) {
    return s[0] == (pchar)c && s[1] == 0;
}

static int name_has_max(const pchar *p) {
    const pchar *b = base_name(p);
    for (; b[0] && b[1] && b[2]; b++)
        if (plower(b[0]) == 'm' && plower(b[1]) == 'a' && plower(b[2]) == 'x') return 1;
    return 0;
}

static int run(int argc, pchar **argv, const pchar *self) {
    v2::init_tables();      // v2 tables
    v3::init_tables();
    v3::crc_init();
    // command line: hcm c IN OUT | hcm x IN OUT | hcm d IN OUT
    // v3.5 parallel blocks: p = fast, q = max; block size in MB from HCM_BLOCK_MB (default 16)
    if (argc == 4 && (is_word(argv[1], 'p') || is_word(argv[1], 'q'))) {
        const char *e = getenv("HCM_BLOCK_MB"); long mb = e ? atol(e) : 16; if (mb < 1) mb = 16;
        g_par_block = (size_t)mb << 20;
        return do_compress(argv[2], argv[3], is_word(argv[1], 'q') ? v3::MODE_MAX : v3::MODE_FAST);
    }
    if (argc == 4 && (is_word(argv[1], 'c') || is_word(argv[1], 'x') || is_word(argv[1], 'd'))) {
        if (is_word(argv[1], 'd')) return do_decompress(argv[2], argv[3]);
        return do_compress(argv[2], argv[3], is_word(argv[1], 'x') ? v3::MODE_MAX : v3::MODE_FAST);
    }
    int mode = self && name_has_max(self) ? v3::MODE_MAX : v3::MODE_FAST;
    printf("HCM v3.5  (Harrington Compression Method)  -  %s mode\n", mode == v3::MODE_MAX ? "max" : "fast");
    printf("Copyright (c) 2026 Michael Harrington. MIT License.\n\n");
    int failures = 0;
    if (argc < 2) {
        printf("To compress a file, drag it onto this program's icon.\n");
        printf("It is saved next to the original as NAME.hcm, after a check\n");
        printf("that it decodes back exactly.\n\n");
        printf("To restore a file, drag the .hcm file onto the icon.\n");
        printf("Files from HCM v3 and v2 can be restored too.\n\n");
    } else {
        for (int i = 1; i < argc; i++) {
            if (ends_with_hcm(argv[i])) failures += do_decompress(argv[i], NULL);
            else failures += do_compress(argv[i], NULL, mode);
        }
    }
#ifdef _WIN32
    printf("Press Enter to close this window.");
    fflush(stdout);
    getchar();
#endif
    return failures ? 1 : 0;
}

#ifdef _WIN32
int main(void) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    static wchar_t self[4096];
    DWORD k = GetModuleFileNameW(NULL, self, 4096);
    int rc = run(argc, argv, k ? self : (argc ? argv[0] : NULL));
    LocalFree(argv);
    return rc;
}
#else
int main(int argc, char **argv) { return run(argc, argv, argc ? argv[0] : NULL); }
#endif

/*
MIT License

Copyright (c) 2026 Michael Harrington

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The built-in exp() and log() are ported from Arm Optimized Routines,
Copyright (c) 1999-2022, Arm Limited, used under the MIT License with the
same permission notice as above.
*/
